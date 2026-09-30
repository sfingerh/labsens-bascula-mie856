# LabSens · Báscula instrumentada (MIE 856)

Curso: **Sensores e Instrumentación en Ingeniería (MIE 856), 2026s2**  
LabSens · Escuela de Ingeniería Eléctrica · PUCV

Balanza de dos celdas (trasera / delantera) con dos cadenas de ADC en paralelo:

- INA131 + ADS1115 (16 bit, hasta 750 SPS configurable)
- HX711 (24 bit, ≤80 SPS)

El firmware entrega CSV por USB a 115200 baud. El trabajo práctico pide medir **varios días**, agregar masa (≥8 veces/día, ≥60 s entre eventos), registrar **temperatura** y estimar el monto en monedas a los 4 y 8 días.

## Repos y guías de origen

- Enunciado: [Drive](https://docs.google.com/document/d/1y0MO3ZwaDYJHJphlMgfKBBx9oWNGK4LS/edit)
- Guía de uso (Mario): https://mkgmario.github.io/LabsensBascula/guia_uso_bascula.html
- Guía técnica admin: https://mkgmario.github.io/LabsensBasculaAdmin/Admin.html
- Código HTML origen: [MkgMario/LabsensBascula](https://github.com/MkgMario/LabsensBascula)

## ¿Puedo compilar y cargar desde VS Code?

Sí. Dos caminos:

1. **PlatformIO** (recomendado): abre esta carpeta en VS Code → extensión PlatformIO → elige el env `xiao_c6` o `xiao_s3` → Build → Upload.
2. **Arduino IDE / extensión Arduino**: abre `firmware/bascula_v4/bascula_v4.ino`. Placa Seeed XIAO ESP32-C6 (docs LabSens) o XIAO ESP32-S3 (comentario del `.ino` v3.1).

```bash
git clone git@github.com:sfingerh/labsens-bascula-mie856.git
cd labsens-bascula-mie856
code .
```

## Qué hace el alumno

Ver `docs/ENUNCIADO.md` y `docs/GUIA_LAB.md`.

1. Flash + prueba de 20 min.
2. Tara, calibración con masa conocida.
3. Log de ≥4 y ≥8 días.
4. Temperatura (opcional DS18B20 en D6; sin sensor el CSV sigue en 6 columnas).
5. Informe: sensibilidad, resolución, exactitud, precisión, error, deriva, sync ADS vs HX.

## CSV v4

`t_ms,ads_rear,ads_front,hx1,hx2,sps_ads` — valores con tara. Serial: `T`/`t` retara; `j`/`k` tasa; `R`=750 SPS; `h` ayuda (ver `docs/GUIA_LAB.md`).
Opcional DS18B20 en **D6**: si el sensor responde al arranque, se agrega `,temp_C` (ver `docs/GUIA_LAB.md`).
