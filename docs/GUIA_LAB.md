# Guía de laboratorio (alumnos)

## Compilar en VS Code
PlatformIO: abrir la raíz del repo, env `xiao_c6` o `xiao_s3`, Build, Upload, monitor 115200.

## Datos
Header: t_ms,ads_rear,ads_front,hx1,hx2,sps_ads
Tara de sesión: `T` + Enter.

## Ensayo 20 min
Vacío → T. Masa conocida → factor cuentas/gramos. Quitar masa (histéresis). 3–4 monedas con ≥60 s.

## Ensayo 4 y 8 días
USB estable. ≥8 eventos/día. Anotar hora y tipo de moneda. Temperatura cada 1 h (firmware no trae sensor). Tara vive en RAM.

## Informe
Por qué 800 Hz si la masa cambia en minutos. Decimación. Sync: HX más lento, CSV repite último HX. Deriva térmica. LSB ADS 0.03125 mV @ ±1.024 V.
