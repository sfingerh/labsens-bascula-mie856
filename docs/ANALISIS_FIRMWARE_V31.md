# Análisis firmware v3.1

Timer 800 Hz → taskADS lee 2×ADS1115 I2C. taskHX711 lee 2×HX711. Tara 300 muestras. Serial 115200.

## Contratos rotos
- Header 6 cols, print 4
- Tara ADS no se imprime
- Tara HX comentada
- Docs gramos ~20 Hz vs cuentas ~800 Hz
- Docs C6 vs comentario S3
- SDA/SCL invertidos en Wire.begin(D4,D5)

## Otros
Header después de crear tareas. Serial en tarea alta prioridad. Mismo core. noInterrupts. I2C 100 kHz justo. El desfase ADS/HX es el ejercicio de sincronización del enunciado.
