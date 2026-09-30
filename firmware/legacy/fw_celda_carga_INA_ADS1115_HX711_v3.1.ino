// ------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------

// ESTABLE
// ------------------------------------------------------------------------------------
// ESP32-S3: ADS1115 @860SPS (modo continuo) con temporizador HW ~800 Hz
// + Dos HX711 (Rob Tillaart) en lectura independiente (hasta 80 SPS)
// Salida por Serial en CSV: t_ms,ads_rear,ads_front,hx1,hx2,sps_ads
// ------------------------------------------------------------------------------------

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "ads_configs.h"     // ads1115 configs
#include <HX711.h>           // Rob Tillaart

// ----------------------------- Pines HX711 
#define HX1_DAT  D1
#define HX1_CLK  D0
#define HX2_DAT  D2
#define HX2_CLK  D3

// ----------------------------- I2C
#ifndef SDA
  #define SDA  D5     
#endif
#ifndef SCL
  #define SCL  D4   
#endif
#define I2C_FREQ 100'000UL

// ----------------------------- Temporizador a ~800 Hz
// Periodo ≈ 1.25 ms -> 800 Hz
#define TIMER_HZ          800
#define TIMER_PERIOD_US   (1000000UL / TIMER_HZ)

// ----------------------------- Calibración / Tare
#define NUM_CALIB_SAMPLES 300

// ----------------------------- Objetos y estados
static hw_timer_t* s_timer = nullptr;
static SemaphoreHandle_t s_semaSample = nullptr;
static portMUX_TYPE s_timerMux = portMUX_INITIALIZER_UNLOCKED;

// Últimas lecturas (compartidas entre tareas)
volatile int16_t g_adsRear_raw = 0;
volatile int16_t g_adsFront_raw = 0;
volatile long    g_hx1_raw = 0;
volatile long    g_hx2_raw = 0;

// Offsets de tare ADS (en cuentas ADS) e HX711
static int16_t adsRear_offset = 0;
static int16_t adsFront_offset = 0;
static long    hx1_offset = 0;
static long    hx2_offset = 0;

// Estadísticas de Hz ADS
static uint32_t lastSpsTime = 0;
static uint16_t spsCount = 0;
static uint16_t spsADS = 0;

// HX711
HX711 hx1;  // begin(DAT, CLK, gain=128)
HX711 hx2;

// ----------------------------- Utilitarios ADS1115
static inline void adsWriteConfig(uint8_t addr, uint16_t cfg)
{
  Wire.beginTransmission(addr);
  Wire.write(ADS_REG_CONFIG);
  Wire.write((uint8_t)((cfg >> 8) & 0xFF));
  Wire.write((uint8_t)(cfg & 0xFF));
  Wire.endTransmission();
}

static inline int16_t adsReadConv(uint8_t addr)
{
  // Seleccionar puntero a Conversion
  Wire.beginTransmission(addr);
  Wire.write(ADS_REG_CONVERSION);
  Wire.endTransmission(false);

  // Leer 2 bytes
  Wire.requestFrom((int)addr, 2, (int)true);
  if (Wire.available() < 2) return 0;

  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  int16_t v = (int16_t)((hi << 8) | lo);
  return v;
}

// ----------------------------- ISR del temporizador
void IRAM_ATTR onTimer()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_semaSample, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken)
    portYIELD_FROM_ISR();
}

// ----------------------------- Tarea de muestreo ADS @800 Hz
void taskADS(void* pv)
{
  // Configurar ambos ADS en continuo, AIN0-AIN1, PGA ±1.024V, 860 SPS, sin comparador
  adsWriteConfig(ADS_ADDR_REAR,  ADS_CONFIG_WORD_REAR);
  adsWriteConfig(ADS_ADDR_FRONT, ADS_CONFIG_WORD_FRONT);

  // Tare inicial de ADS
  {
    long sumRear = 0, sumFront = 0;
    for (int i = 0; i < NUM_CALIB_SAMPLES; ++i)
    {
      sumRear  += adsReadConv(ADS_ADDR_REAR);
      sumFront += adsReadConv(ADS_ADDR_FRONT);
      delay(2); // ~500 Hz durante tare (no crítico)
    }
    adsRear_offset  = (int16_t)(sumRear / NUM_CALIB_SAMPLES);
    adsFront_offset = (int16_t)(sumFront / NUM_CALIB_SAMPLES);
  }

  lastSpsTime = millis();

  for (;;)
  {
    // Espera el "tick" de 800 Hz disparado por el temporizador
    if (xSemaphoreTake(s_semaSample, portMAX_DELAY) == pdTRUE)
    {
      // Lee conversiones (ya en modo continuo) — NO en ISR
      int16_t r = adsReadConv(ADS_ADDR_REAR);
      int16_t f = adsReadConv(ADS_ADDR_FRONT);

      int16_t r_tared = r - adsRear_offset;
      int16_t f_tared = f - adsFront_offset;

      g_adsRear_raw  = r_tared;
      g_adsFront_raw = f_tared;

      // Contador SPS
      spsCount++;
      uint32_t now = millis();
      if (now - lastSpsTime >= 1000UL)
      {
        spsADS = spsCount;
        spsCount = 0;
        lastSpsTime = now;
      }

      // Emitir línea CSV incluyendo últimas lecturas HX
      long hx1_local, hx2_local;
      noInterrupts();
      hx1_local = g_hx1_raw;
      hx2_local = g_hx2_raw;
      interrupts();

      // t_ms, ads_rear, ads_front, hx1, hx2, sps_ads
      // Serial.print(millis());
      //Serial.print(',');
      // Serial.print("0");
      // Serial.print(",");

      Serial.print(r);
      Serial.print(',');
      Serial.print(f);
      Serial.print(',');

      // Camibar la valores con o sin tara
      Serial.print(hx1_local); // Serial.print(hx1_local - hx1_offset);
      Serial.print(',');
      Serial.println(hx2_local) ; // Serial.print(hx2_local - hx2_offset);
      //Serial.print(',');

      //Serial.println(spsADS);
    }
  }
}

// ----------------------------- Tarea HX711 (independiente, ~80 Hz máx)
void taskHX711(void* pv)
{
  // Arranque HX711
  hx1.begin(HX1_DAT, HX1_CLK, 128); // gain 128 típico
  hx2.begin(HX2_DAT, HX2_CLK, 128);

  hx1.tare(10);
  hx2.tare(10);


  // Si tu módulo tiene pin RATE = 80SPS, colócalo a 80 Hz para máxima velocidad.
  // (La librería Rob Tillaart lee cuando está listo: is_ready())

  // Tare inicial HX711 (media simple sin bloquear)
  {
    const int N = NUM_CALIB_SAMPLES;
    long s1 = 0, s2 = 0;
    int c1 = 0, c2 = 0;
    uint32_t t0 = millis();
    while ((c1 < N || c2 < N) && (millis() - t0) < 5000UL)
    {
      if (c1 < N && hx1.is_ready())
      {
        s1 += hx1.read();
        c1++;
      }
      if (c2 < N && hx2.is_ready())
      {
        s2 += hx2.read();
        c2++;
      }
      // Pequeño yield
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (c1 > 0) hx1_offset = s1 / c1;
    if (c2 > 0) hx2_offset = s2 / c2;
  }

  // Bucle de muestreo HX (no interferir con ADS)
  const TickType_t minDelay = pdMS_TO_TICKS(2); // ~500 Hz yield; lectura real la marca is_ready()
  for (;;)
  {
    if (hx1.is_ready())
    {
      long v1 = hx1.read();  // lectura bruta de 24 bits (signada en 32)
      noInterrupts();
      g_hx1_raw = v1;
      interrupts();
    }
    if (hx2.is_ready())
    {
      long v2 = hx2.read();
      noInterrupts();
      g_hx2_raw = v2;
      interrupts();
    }
    vTaskDelay(minDelay);
  }
}

// ----------------------------- setup / loop
void setup()
{
  // Serial
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  // I2C
  Wire.begin(D4,D5);
  Wire.setClock(I2C_FREQ);

  // Semáforo binario para el tick de muestreo
  s_semaSample = xSemaphoreCreateBinary();

  // Temporizador HW a ~800 Hz (ERROR!!!)
  // s_timer = timerBegin(0, 80, true); // div 80 -> 1 tick = 1us @80MHz APB
  // timerAttachInterrupt(s_timer, &onTimer, true);
  // timerAlarmWrite(s_timer, TIMER_PERIOD_US, true);
  // timerAlarmEnable(s_timer);

  // Temporizador HW a ~800 Hz (COMPILED!!!)
  s_timer = timerBegin(1000000);
  timerAttachInterrupt(s_timer, &onTimer);
  timerAlarm(s_timer, 1250, true, 0);

  // Tareas: prioridad mayor para ADS (tiempo determinista)
  xTaskCreatePinnedToCore(taskADS,   "taskADS",   4096, nullptr, configMAX_PRIORITIES - 2, nullptr, APP_CPU_NUM);
  xTaskCreatePinnedToCore(taskHX711, "taskHX711", 4096, nullptr, 1,                          nullptr, APP_CPU_NUM);

  // Encabezado CSV
  Serial.println(F("t_ms,ads_rear,ads_front,hx1,hx2,sps_ads"));
}

void loop()
{
  // Nada que hacer aquí; todo corre en tareas/ISR
}
