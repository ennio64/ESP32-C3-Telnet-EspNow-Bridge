# CNC_Pendant_DEMO – ESP‑NOW CNC Web Pendant

This folder contains a **fully working demonstration** of a wireless CNC pendant built on ESP32, using **ESP‑NOW** for low‑latency communication and a **built‑in Web UI** for real‑time control.

The goal of this demo is to provide a **clean, minimal, and extendable foundation** for building a modern CNC pendant with DRO, G‑code commands, quick moves, and machine control — all accessible from any browser.

---

## 🚀 Features

- ESP‑NOW wireless communication with CNC bridge  
- Real‑time DRO (X, Y, Z, machine state) via WebSocket  
- Dark‑themed Web UI optimized for mobile and vertical layout  
- Machine control buttons (Reset, Hold, Resume, Home, Unlock)  
- Modes: G90 (absolute), G91 (incremental)  
- Quick Moves: G0 X/Y/Z → 0 or +10 mm  
- Manual G‑code input  
- RUN TEST button (sends 400 lines of G‑code)  
- Console log for debugging  
- Clean, minimal, industrial‑style interface  

---

## 🧩 How It Works

### 1. ESP‑NOW Communication
The pendant sends commands wirelessly to a CNC bridge ESP32 using ESP‑NOW.  
The bridge forwards commands to the CNC controller (e.g., Grbl‑HAL).

### 2. Web Interface
The ESP32 hosts a **self‑contained HTML/CSS/JS interface** stored in flash.  
Any device on the same network can access it via:
```cpp
http://<pendant_IP>
```

### 3. Real‑Time DRO
The bridge sends DRO packets back to the pendant, which forwards them to the browser via WebSocket.

---

## 🌐 Web UI Overview

The interface is designed to be:

- Dark themed  
- Minimal and technical  
- Vertical layout  
- Square buttons, no glossy effects  
- CNC‑style panel look  

### Sections included

#### 1. DRO (Digital Read‑Out)
- Machine state (Idle, Run, Alarm…)  
- X/Y/Z coordinates (monospaced font)

#### 2. Usual Commands
- **Machine Control**: Reset, Hold, Resume, Home, Unlock  
- **Modes**: G90 / G91  
- **Quick Moves**: G0 X0, X10, Y0, Y10, Z0, Z10  

#### 3. Test & G‑code
- RUN TEST (400 lines)  
- Custom G‑code input + Send button  

#### 4. Console
- Shows last commands sent  
- Monospaced dark terminal style  

---

## 🛠 Requirements

- ESP32 (C3, S3, WROOM — all supported)  
- PlatformIO or Arduino IDE  
- CNC controller running Grbl‑HAL  
- ESP‑NOW compatible bridge firmware  

---

## ▶️ Uploading the Demo

1. Open **CNC_Pendant_DEMO.ino** in Arduino IDE or PlatformIO  
2. Flash it to your ESP32 pendant  
3. Power on the CNC bridge ESP32  
4. Wait for **PAIR_OK**  
5. Open the pendant’s IP in a browser  
6. Start controlling your CNC wirelessly  

---

## 🧱 Extendability

You can easily add:

- Jogging wheel / encoder  
- Physical buttons  
- Touchscreen  
- SD card G‑code loader  
- MQTT logging  
- Multi‑pendant support  
- Battery + sleep mode  

This demo is intentionally minimal to keep the architecture clean and easy to extend.

---

## 🎯 Purpose of This Demo

This project is meant to be:

- A reference implementation  
- A starting point for custom CNC pendants  
- A testbed for ESP‑NOW + WebSocket integration  
- A clean example of embedded Web UI design  

**Use it as a starting point — and create your definitive solution.**

---

## 📄 License

Released under the **MIT License**.  
You are free to use, modify, and distribute this demo.
