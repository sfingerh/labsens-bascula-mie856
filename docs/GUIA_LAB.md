# Guía de laboratorio (alumnos)

## Compilar en VS Code
PlatformIO: abrir la raíz del repo, env `xiao_c6` o `xiao_s3`, Build, Upload, monitor 115200.

## Datos
Header (ancho fijo, comas alineadas): `      t_ms,ads_rear,ads_front,       hx1,       hx2,sps_ads`  
Las filas CSV usan el mismo padding (`printf`); parsers toleran espacios alrededor de los campos.  
Tara de sesión: `T` o `t` en el monitor serie (115200).

### Comandos USB (firmware v4.2)

| Tecla | Acción |
|-------|--------|
| `T` / `t` | Retara (offsets de sesión) |
| `j` | Baja la tasa de muestreo ADS (periodo del timer HW); pausa CSV ~2 s |
| `k` | Sube la tasa de muestreo ADS; pausa CSV ~2 s |
| `R` | Restablece la tasa a **750 SPS**; pausa CSV ~2 s |
| `q` / `Q` | Toggle stream CSV: pausa/reanuda transmisión (`# stream=off` / `# stream=on`) |
| `h` / `H` | Ayuda (versión, autor, URL, comandos); pausa CSV ~5 s |

Escala de tasas: `0.5 → 1 → 2 → 5 → 10 → 30` SPS y luego `50…750` en pasos de 50. Al cambiar la tasa, el firmware imprime líneas en blanco y luego `# rate_Hz=…`. La tasa por defecto al arrancar es 750 SPS.

**Filtrado:** todos los mensajes de control empiezan con `#` (ayuda, tasa, stream). En postproceso se pueden descartar con `grep -v '^#'` o equivalente. El stream CSV se pausa brevemente en ayuda/cambio de tasa para que esas líneas no queden intercaladas a 750 SPS; con `q` se puede dejar el stream apagado mientras se lee la ayuda o se ajusta la tasa.

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
  `      t_ms,ads_rear,ads_front,       hx1,       hx2,sps_ads, temp_C`  
  y cada fila agrega la temperatura en °C (2 decimales, ancho 7). Si una lectura puntual falla, esa celda sale como `    nan`.

La temperatura se muestrea ~1 vez por segundo en una tarea aparte; **no** interfiere con el muestreo ADS (tasa configurable, por defecto 750 SPS).

## Ensayo 20 min
Vacío → T. Masa conocida → factor cuentas/gramos. Quitar masa (histéresis). 3–4 monedas con ≥60 s.

## Ensayo 4 y 8 días
USB estable. ≥8 eventos/día. Anotar hora y tipo de moneda. Con DS18B20, la columna `temp_C` queda en el CSV; sin sensor, anotar temperatura ambiente cada 1 h a mano. Tara vive en RAM.

## Informe
Por qué muestrear a cientos de SPS si la masa cambia en minutos (probar `j`/`k`). Decimación. Sync: HX más lento, CSV repite último HX. Deriva térmica (usar `temp_C` si está disponible). LSB ADS 0.03125 mV @ ±1.024 V.
