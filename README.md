# EasyEfx 🎛️

**EasyEfx** es un plugin multiefectos profesional en formato **VST3** (y Standalone) desarrollado en C++20 con el framework JUCE 8. Diseñado para procesar voces, leads, sintetizadores o pistas completas con una matriz de 21 efectos de estudio conmutables, controles macro automatizables y paneles de configuración avanzada inspirados en hardware y plugins clásicos (como Fruity Reeverb 2 y Fruity Delay 3).

---

## 🚀 Descarga e Instalación del Plugin VST3

El instalador oficial para Windows se encarga de instalar automáticamente el plugin VST3 en la carpeta de plugins de tu sistema (`C:\Program Files\Common Files\VST3\`), listo para ser reconocido por FL Studio, Ableton Live, Cubase, Reaper, Studio One, etc.:

👉 **[Descargar EasyEfx_Installer.exe (Instalador Oficial)](https://github.com/MiguelC21/EasyEfx/raw/main/dist/EasyEfx_Installer.exe)**

*(También puedes encontrar el archivo ejecutable del instalador en la carpeta [`dist/EasyEfx_Installer.exe`](dist/EasyEfx_Installer.exe) de este repositorio).*

### ¿Cómo instalarlo?
1. Descarga y ejecuta **`EasyEfx_Installer.exe`**.
2. Sigue los pasos del asistente de instalación. Puedes elegir instalar:
   - **Plugin VST3 (64-bit):** Instalación automática en `C:\Program Files\Common Files\VST3\EasyEfx.vst3`.
   - **Aplicación Standalone:** Para usar el multiefectos directamente con tu interfaz de audio sin abrir ningún DAW.
3. Abre tu DAW favorito, realiza un re-escaneo de plugins y agrega **EasyEfx** a tu cadena de efectos.

---

## ✨ Características Principales

- **Tablero 3x7 de 21 Efectos de Estudio:**
  - Cada efecto cuenta con un botón de encendido/apagado `(I)` que desactiva completamente el procesamiento interno para consumir **0% de CPU** cuando está en bypass.
  - Perillas principales (**Macro 1** y **Macro 2**) totalmente automatizables desde cualquier DAW compatible con VST3.
  - Botón de ajustes `⚙` que abre un inspector de hardware detallado con controles secundarios avanzados.
- **Respuesta en Tiempo Real a 0 ms:**
  - Control de parámetros atómico sin latencia ni retraso al mover perillas o automatizar.
- **Compatibilidad:**
  - Windows 10 / 11 (64-bit).
  - VST3 (FL Studio, Ableton Live, Cubase, Studio One, Reaper, Bitwig, etc.).
  - Standalone (.exe independiente con soporte ASIO / WASAPI / DirectSound).

---

## 🎚️ Catálogo de Efectos (21 Módulos)

| # | Efecto | Descripción & Controles Principales | Ajustes Avanzados (`⚙`) |
|---|---|---|---|
| 1 | **Reverb** | Dattorro Plate Reverb con alta densidad | Decay, Damp, Size, Diffusion, Pre-delay, Hi-Cut, Lo-Cut |
| 2 | **Delay** | Delay profesional con sincronización al tempo | Modos Mono, Stereo y Ping-Pong, Feedback, Cutoff, Res, Smoothing |
| 3 | **Chorus** | Modulación vocal y de sintetizadores exuberante | Rate (Hz o tempo), Depth, Feedback, Voices, Stereo Spread |
| 4 | **Flanger** | Efecto peine con barrido metálico | Rate, Depth, Feedback, Manual Delay Offset |
| 5 | **Phaser** | Filtro de cambio de fase de múltiples etapas | Rate, Depth, Feedback, Center Frequency, Stage count |
| 6 | **Doubler** | Duplicador de voces estéreo con micro-detune | Detune (cents), Delay spread, Stereo Width |
| 7 | **Formant** | Filtro de talkbox / formantes vocales (A, E, I, O, U) | Morphing de vocales, Gender shift, Resonancia de formante |
| 8 | **Pitch** | Pitch shifter granular de alta calidad | Rango seleccionable (±12 o ±24 semitonos), Fine tune, Grain size |
| 9 | **Tremolo** | Modulación de volumen (0% profundidad = transparente) | Rate (Hz o Tempo-sync), Forma de onda (Seno, Triángulo, Cuadrada) |
| 10 | **AutoPan** | Paneo rítmico estéreo con tasas ultra-rápidas | Rate (Hz o Tempo-sync), Modos de fase, Forma de onda |
| 11 | **Stutter** | Glitch rítmico y repetidor de búfer sincronizado | Rate (1/4, 1/8, 1/16, 1/32), Decay/Fade smoothing, Gate length |
| 12 | **Tape Stop** | Simulación de parada y arranque de motor de cinta analógica | Stop time, Start time, Curva de frenado |
| 13 | **Saturator** | Color armónico cálido y agresivo | Modos: Tape, Tube, Solid-State y Wavefolder |
| 14 | **Distortion** | Distorsión multibanda y drive extremo | Modos: Hard Clip, Soft Clip, Germanium Asymmetric, Fuzz |
| 15 | **Bitcrusher** | Reductor digital lo-fi de resolución | Sample Rate Decimator, Bit Depth (2 a 16 bits), Anti-aliasing |
| 16 | **Ring Mod** | Modulación en anillo para texturas metálicas/robóticas | Carrier Frequency (Hz), Dry/Wet, Fine Frequency |
| 17 | **LowPass** | Filtro pasa-bajos resonante analógico | Pendientes 12 dB / 24 dB por octava, Drive, Resonancia |
| 18 | **HighPass** | Filtro pasa-altos resonante analógico | Pendientes 12 dB / 24 dB por octava, Drive, Resonancia |
| 19 | **Widener** | Procesador espacial Mid/Side | Control continuo desde Mono total (0%) hasta Ultra-Stereo (200%) |
| 20 | **Gate** | Puerta de ruido musical para cortes limpios | Threshold, Attack, Hold, Release, Lookahead |
| 21 | **Compressor** | Compresor / Limitador de salida master | Threshold, Ratio, Attack, Release, Modo Limiter / Brickwall Ceiling |

---

## 🛠️ Compilación desde el Código Fuente

### Requisitos:
- **CMake** 3.22 o superior
- **Compilador C++20** (Visual Studio 2022 con MSVC v143 o posterior)
- **JUCE 8** (incluido en la raíz del proyecto)
- **Inno Setup 6** (para generar el instalador ejecutable)

### Pasos:
```bash
# 1. Clonar el repositorio
git clone git@github.com:MiguelC21/EasyEfx.git
cd EasyEfx

# 2. Configurar con CMake en modo Release
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# 3. Compilar VST3 y Standalone
cmake --build build --config Release --parallel

# 4. Generar el instalador .exe
iscc installer/EasyEfx_Setup.iss
```

---

## 📄 Licencia

Desarrollado como proyecto de procesamiento de audio en tiempo real.
