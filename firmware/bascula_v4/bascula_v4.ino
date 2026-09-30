// LabSens PUCV · MIE 856 · báscula v4.2
// ADS1115 muestreo configurable (timer HW) + 2x HX711.
// CSV fijo (campos alineados): t_ms,ads_rear,ads_front,hx1,hx2,sps_ads[,temp_C].
// Extensión opcional: DS18B20 en D6 → columna temp_C si el sensor responde al arranque.
// Serial: T/t tara; j/k tasa; R=750 SPS; q stream on/off; h/H ayuda.
// Mensajes de control con prefijo '#' (filtrables). CSV se pausa en ayuda/cambio de tasa.

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "ads_configs.h"
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define FW_VERSION "v4.2"
#define FW_URL "https://github.com/sfingerh/labsens-bascula-mie856"

#define HX1_DAT D1
#define HX1_CLK D0
#define HX2_DAT D2
#define HX2_CLK D3
// I2C en este hardware LabSens: SDA=D4, SCL=D5.
// Usar siempre Wire.begin(D4, D5); no confiar en macros SDA/SCL del board
// (en algunos cores quedan invertidas respecto al cableado real).
#define I2C_SDA_PIN D4
#define I2C_SCL_PIN D5
// DS18B20 (OneWire): pin libre; D0–D5 ocupados por HX711 e I2C.
#define DS18B20_PIN D6
#define TEMP_SAMPLE_MS 1000UL
#define I2C_FREQ 100000UL
#define NUM_CALIB_SAMPLES 300
#define CSV_PAUSE_HELP_MS 5000UL
#define CSV_PAUSE_RATE_MS 2000UL

// Escala de tasas (SPS). j baja índice, k sube, R → 750.
static const float RATE_LADDER[] = {
  0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f,
  50.0f, 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 350.0f,
  400.0f, 450.0f, 500.0f, 550.0f, 600.0f, 650.0f, 700.0f, 750.0f
};
static const int RATE_LADDER_N = (int)(sizeof(RATE_LADDER) / sizeof(RATE_LADDER[0]));
static const int RATE_IDX_DEFAULT = RATE_LADDER_N - 1;  // 750 SPS

static hw_timer_t* s_timer = nullptr;
static SemaphoreHandle_t s_semaSample = nullptr;
static portMUX_TYPE s_spin = portMUX_INITIALIZER_UNLOCKED;
volatile int16_t g_adsRear = 0, g_adsFront = 0;
volatile long g_hx1 = 0, g_hx2 = 0;
volatile bool g_requestTare = false;
static int16_t adsRear_offset = 0, adsFront_offset = 0;
static long hx1_offset = 0, hx2_offset = 0;
static uint32_t lastSpsTime = 0;
static uint16_t spsCount = 0, spsADS = 0;
static int s_rateIdx = RATE_IDX_DEFAULT;
static float s_rateHz = 750.0f;
HX711 hx1, hx2;

// Stream CSV: q/Q toggle; pausas temporales en ayuda / cambio de tasa.
static volatile bool g_streamOn = true;
static volatile uint32_t g_csvPauseUntil = 0;

// Temperatura: muestreo lento en task propia (no en el camino ADS).
static OneWire oneWire(DS18B20_PIN);
static DallasTemperature dallas(&oneWire);
static bool g_tempEnabled = false;
static volatile float g_tempC = NAN;

static inline void adsWriteConfig(uint8_t addr, uint16_t cfg) {
  Wire.beginTransmission(addr);
  Wire.write(ADS_REG_CONFIG);
  Wire.write((uint8_t)((cfg >> 8) & 0xFF));
  Wire.write((uint8_t)(cfg & 0xFF));
  Wire.endTransmission();
}
static inline int16_t adsReadConv(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(ADS_REG_CONVERSION);
  Wire.endTransmission(false);
  Wire.requestFrom((int)addr, 2, (int)true);
  if (Wire.available() < 2) return 0;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  return (int16_t)((hi << 8) | lo);
}
void IRAM_ATTR onTimer() {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(s_semaSample, &hp);
  if (hp) portYIELD_FROM_ISR();
}

static uint64_t periodUsFromRate(float rateHz) {
  // timerBegin(1e6) → 1 tick = 1 µs. 0.5 SPS → 2_000_000 µs.
  if (rateHz <= 0.0f) return 2000000ULL;
  double us = 1000000.0 / (double)rateHz;
  if (us < 1.0) us = 1.0;
  return (uint64_t)(us + 0.5);
}

static void pauseCsvMs(uint32_t ms) {
  uint32_t until = millis() + ms;
  if (until > g_csvPauseUntil) g_csvPauseUntil = until;
}

static bool csvTxAllowed() {
  return g_streamOn && (millis() >= g_csvPauseUntil);
}

static void printRateConfirm() {
  // Líneas en blanco antes del marcador (separan del CSV previo).
  Serial.println();
  Serial.println();
  Serial.print(F("# rate_Hz="));
  if (s_rateHz < 1.0f)
    Serial.println(s_rateHz, 1);
  else
    Serial.println(s_rateHz, 0);
}

static void applySampleRateIdx(int idx, bool announce) {
  if (idx < 0) idx = 0;
  if (idx >= RATE_LADDER_N) idx = RATE_LADDER_N - 1;
  s_rateIdx = idx;
  s_rateHz = RATE_LADDER[idx];
  uint64_t periodUs = periodUsFromRate(s_rateHz);
  if (s_timer) {
    timerAlarm(s_timer, periodUs, true, 0);
  }
  if (announce) {
    pauseCsvMs(CSV_PAUSE_RATE_MS);
    printRateConfirm();
  }
}

static void printHelp() {
  Serial.print(F("# LabSens báscula "));
  Serial.println(FW_VERSION);
  Serial.println(F("# Autor: Sebastian Fingerhuth (PUCV / LabSens)"));
  Serial.print(F("# "));
  Serial.println(FW_URL);
  Serial.println(F("# Comandos USB (un carácter):"));
  Serial.println(F("#   T / t  — retara (ADS + offsets de sesión)"));
  Serial.println(F("#   j      — bajar tasa de muestreo ADS (timer)"));
  Serial.println(F("#   k      — subir tasa de muestreo ADS (timer)"));
  Serial.println(F("#   R      — reset tasa a 750 SPS"));
  Serial.println(F("#   q / Q  — pausar/reanudar stream CSV (toggle)"));
  Serial.println(F("#   h / H  — esta ayuda (pausa CSV ~5 s)"));
  Serial.println(F("# Escala: 0.5,1,2,5,10,30 luego 50..750 paso 50 SPS"));
  Serial.println(F("# Mensajes de control: líneas con prefijo '#' (filtrar en postproceso)"));
  printRateConfirm();
}

static void tareADS(int n) {
  long sR = 0, sF = 0;
  for (int i = 0; i < n; ++i) {
    sR += adsReadConv(ADS_ADDR_REAR);
    sF += adsReadConv(ADS_ADDR_FRONT);
    delay(2);
  }
  adsRear_offset = (int16_t)(sR / n);
  adsFront_offset = (int16_t)(sF / n);
}
void taskADS(void*) {
  adsWriteConfig(ADS_ADDR_REAR, ADS_CONFIG_WORD_REAR);
  adsWriteConfig(ADS_ADDR_FRONT, ADS_CONFIG_WORD_FRONT);
  tareADS(NUM_CALIB_SAMPLES);
  lastSpsTime = millis();
  for (;;) {
    if (xSemaphoreTake(s_semaSample, portMAX_DELAY) != pdTRUE) continue;
    if (g_requestTare) { tareADS(80); g_requestTare = false; }
    int16_t r = adsReadConv(ADS_ADDR_REAR);
    int16_t f = adsReadConv(ADS_ADDR_FRONT);
    int16_t rT = r - adsRear_offset, fT = f - adsFront_offset;
    long hx1l, hx2l;
    float tempC = NAN;
    portENTER_CRITICAL(&s_spin);
    g_adsRear = rT; g_adsFront = fT; hx1l = g_hx1; hx2l = g_hx2;
    if (g_tempEnabled) tempC = g_tempC;
    portEXIT_CRITICAL(&s_spin);
    spsCount++;
    uint32_t now = millis();
    if (now - lastSpsTime >= 1000UL) { spsADS = spsCount; spsCount = 0; lastSpsTime = now; }
    if (!csvTxAllowed()) continue;
    // Campos de ancho fijo → comas alineadas (tabla legible en monitor serie).
    // Anchos: t_ms=10, ads=8/9, hx=10/10, sps=7 [, temp=7]
    Serial.printf("%10lu,%8d,%9d,%10ld,%10ld,%7u",
                  (unsigned long)now, (int)rT, (int)fT,
                  (long)hx1l, (long)hx2l, (unsigned)spsADS);
    if (g_tempEnabled) {
      if (isnan(tempC)) Serial.print(F(",    nan"));
      else Serial.printf(",%7.2f", tempC);
    }
    Serial.println();
  }
}
void taskHX711(void*) {
  // HX711 0.3.x: begin(data, clock) only; gain via set_gain()
  hx1.begin(HX1_DAT, HX1_CLK);
  hx2.begin(HX2_DAT, HX2_CLK);
  hx1.set_gain(128);
  hx2.set_gain(128);
  hx1.tare(10); hx2.tare(10);
  const int N = NUM_CALIB_SAMPLES;
  long s1 = 0, s2 = 0; int c1 = 0, c2 = 0;
  uint32_t t0 = millis();
  while ((c1 < N || c2 < N) && (millis() - t0) < 5000UL) {
    if (c1 < N && hx1.is_ready()) { s1 += hx1.read(); c1++; }
    if (c2 < N && hx2.is_ready()) { s2 += hx2.read(); c2++; }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (c1 > 0) hx1_offset = s1 / c1;
  if (c2 > 0) hx2_offset = s2 / c2;
  for (;;) {
    if (hx1.is_ready()) {
      long v = hx1.read() - hx1_offset;
      portENTER_CRITICAL(&s_spin); g_hx1 = v; portEXIT_CRITICAL(&s_spin);
    }
    if (hx2.is_ready()) {
      long v = hx2.read() - hx2_offset;
      portENTER_CRITICAL(&s_spin); g_hx2 = v; portEXIT_CRITICAL(&s_spin);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
void taskTemp(void*) {
  // DS18B20: conversión ~750 ms @ 12 bit; muestreo ~1 Hz fuera del path ADS.
  for (;;) {
    dallas.requestTemperatures();
    float t = dallas.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C) t = NAN;
    portENTER_CRITICAL(&s_spin);
    g_tempC = t;
    portEXIT_CRITICAL(&s_spin);
    vTaskDelay(pdMS_TO_TICKS(TEMP_SAMPLE_MS));
  }
}
void taskSerialCmd(void*) {
  for (;;) {
    if (Serial.available()) {
      char c = (char)Serial.read();
      if (c == 'T' || c == 't') {
        g_requestTare = true;
      } else if (c == 'j') {
        applySampleRateIdx(s_rateIdx - 1, true);
      } else if (c == 'k') {
        applySampleRateIdx(s_rateIdx + 1, true);
      } else if (c == 'R') {
        applySampleRateIdx(RATE_IDX_DEFAULT, true);
      } else if (c == 'q' || c == 'Q') {
        g_streamOn = !g_streamOn;
        Serial.print(F("# stream="));
        Serial.println(g_streamOn ? F("on") : F("off"));
      } else if (c == 'h' || c == 'H') {
        pauseCsvMs(CSV_PAUSE_HELP_MS);
        printHelp();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);  // D4=SDA, D5=SCL (cableado LabSens)
  Wire.setClock(I2C_FREQ);

  // Extensión térmica opcional: si no hay DS18B20, CSV de 6 columnas (compat).
  dallas.begin();
  DeviceAddress addr;
  g_tempEnabled = dallas.getAddress(addr, 0);
  if (g_tempEnabled) {
    dallas.setResolution(addr, 12);
    Serial.println(F("      t_ms,ads_rear,ads_front,       hx1,       hx2,sps_ads, temp_C"));
  } else {
    Serial.println(F("      t_ms,ads_rear,ads_front,       hx1,       hx2,sps_ads"));
  }

  s_semaSample = xSemaphoreCreateBinary();
  s_timer = timerBegin(1000000);
  timerAttachInterrupt(s_timer, &onTimer);
  s_rateIdx = RATE_IDX_DEFAULT;
  s_rateHz = RATE_LADDER[s_rateIdx];
  timerAlarm(s_timer, periodUsFromRate(s_rateHz), true, 0);
#if CONFIG_FREERTOS_UNICORE
  // ESP32-C6 (and other single-core): APP_CPU_NUM is undefined; no pin needed
  xTaskCreate(taskADS, "ads", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr);
  xTaskCreate(taskHX711, "hx", 4096, nullptr, 1, nullptr);
  xTaskCreate(taskSerialCmd, "cmd", 2048, nullptr, 1, nullptr);
  if (g_tempEnabled)
    xTaskCreate(taskTemp, "temp", 3072, nullptr, 1, nullptr);
#else
  xTaskCreatePinnedToCore(taskADS, "ads", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, APP_CPU_NUM);
  xTaskCreatePinnedToCore(taskHX711, "hx", 4096, nullptr, 1, nullptr, APP_CPU_NUM);
  xTaskCreatePinnedToCore(taskSerialCmd, "cmd", 2048, nullptr, 1, nullptr, APP_CPU_NUM);
  if (g_tempEnabled)
    xTaskCreatePinnedToCore(taskTemp, "temp", 3072, nullptr, 1, nullptr, APP_CPU_NUM);
#endif
}
void loop() {}
