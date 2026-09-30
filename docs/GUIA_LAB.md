# Guía de laboratorio (alumnos)

## Compilar en VS Code
PlatformIO: abrir la raíz del repo, env `xiao_c6` o `xiao_s3`, Build, Upload, monitor 115200.

## Datos
Header base: `t_ms,ads_rear,ads_front,hx1,hx2,sps_ads`  
Tara de sesión: `T` + Enter.

## Extensión opcional: temperatura (DS18B20)

Para el ensayo de varios días conviene medir temperatura cerca de las celdas. El firmware ya soporta un **DS18B20** sin obligar a cablearlo.

| Concepto | Valor |
|----------|--------|
| Pin de datos | **D6** (D0–D5 están ocupados: HX711 + I2C) |
| Alimentación | 3V3 y GND del XIAO |
| Pull-up | Resistencia **4,7 kΩ** entre el pin de datos (D6) y 3V3 |
| Librerías | OneWireNg (API OneWire) + DallasTemperature (ya en `platformio.ini`) |

**Comportamiento CSV (compatibilidad):**

- Si al arranque el sensor **no responde**, el log queda igual que siempre: 6 columnas y el header de arriba. Scripts y planillas existentes no se rompen.
- Si el sensor **sí responde**, el header pasa a ser  
  `t_ms,ads_rear,ads_front,hx1,hx2,sps_ads,temp_C`  
  y cada fila agrega la temperatura en °C (2 decimales). Si una lectura puntual falla, esa celda sale como `nan`.

La temperatura se muestrea ~1 vez por segundo en una tarea aparte; **no** interfiere con el muestreo ADS a ~800 Hz.

## Ensayo 20 min
Vacío → T. Masa conocida → factor cuentas/gramos. Quitar masa (histéresis). 3–4 monedas con ≥60 s.

## Ensayo 4 y 8 días
USB estable. ≥8 eventos/día. Anotar hora y tipo de moneda. Con DS18B20, la columna `temp_C` queda en el CSV; sin sensor, anotar temperatura ambiente cada 1 h a mano. Tara vive en RAM.

## Informe
Por qué 800 Hz si la masa cambia en minutos. Decimación. Sync: HX más lento, CSV repite último HX. Deriva térmica (usar `temp_C` si está disponible). LSB ADS 0.03125 mV @ ±1.024 V.
