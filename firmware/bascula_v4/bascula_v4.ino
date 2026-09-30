// LabSens PUCV · MIE 856 · báscula v4
// ADS1115 ~800 Hz + 2x HX711. CSV t_ms,ads_rear,ads_front,hx1,hx2,sps_ads (con tara).
// Extensión opcional: DS18B20 en D6 → columna temp_C si el sensor responde al arranque.
// Serial T = retara.

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "ads_configs.h"
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define HX1_DAT D1
#define HX1_CLK D0
#define HX2_DAT D2
#define HX2_CLK D3
#ifndef SDA
  #define SDA D5
#endif
#ifndef SCL
  #define SCL D4
#endif
// DS18B20 (OneWire): pin libre; D0–D5 ocupados por HX711 e I2C.
#define DS18B20_PIN D6
#define TEMP_SAMPLE_MS 1000UL
#define I2C_FREQ 100000UL
#define TIMER_HZ 800
#define TIMER_PERIOD_US (1000000UL / TIMER_HZ)
#define NUM_CALIB_SAMPLES 300

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
HX711 hx1, hx2;

// Temperatura: muestreo lento en task propia (no en el camino ADS ~800 Hz).
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
    Serial.print(now); Serial.print(',');
    Serial.print(rT); Serial.print(',');
    Serial.print(fT); Serial.print(',');
    Serial.print(hx1l); Serial.print(',');
    Serial.print(hx2l); Serial.print(',');
    Serial.print(spsADS);
    if (g_tempEnabled) {
      Serial.print(',');
      if (isnan(tempC)) Serial.print(F("nan"));
      else Serial.print(tempC, 2);
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
      if (c == 'T' || c == 't') g_requestTare = true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin(SDA, SCL);
  Wire.setClock(I2C_FREQ);

  // Extensión térmica opcional: si no hay DS18B20, CSV de 6 columnas (compat).
  dallas.begin();
  DeviceAddress addr;
  g_tempEnabled = dallas.getAddress(addr, 0);
  if (g_tempEnabled) {
    dallas.setResolution(addr, 12);
    Serial.println(F("t_ms,ads_rear,ads_front,hx1,hx2,sps_ads,temp_C"));
  } else {
    Serial.println(F("t_ms,ads_rear,ads_front,hx1,hx2,sps_ads"));
  }

  s_semaSample = xSemaphoreCreateBinary();
  s_timer = timerBegin(1000000);
  timerAttachInterrupt(s_timer, &onTimer);
  timerAlarm(s_timer, TIMER_PERIOD_US, true, 0);
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
