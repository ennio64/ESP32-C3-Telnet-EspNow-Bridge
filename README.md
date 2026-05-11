# ESP32‑C3 Serial Bridge (Telnet + ESP‑NOW)
Universal WiFi serial bridge for any UART-based controller

> **Note:** This bridge works with ANY device that communicates via UART:
> CNC machines (Grbl/GrblHAL), 3D printers (Marlin/Klipper),
> microcontrollers, or any serial device.  
> The bridge is protocol‑agnostic and simply forwards data between WiFi clients (Telnet/ESP‑NOW) and UART.

---

## 🔧 Hardware

- ESP32‑C3 SuperMini (or any ESP32‑C3 board)
- Connections:
  - GPIO21 (TX) → RX of target board
  - GPIO20 (RX) → TX of target board
  - GND → GND
  - 3V3 → 3V3
  - RST (optional)
  - One of these: GPIO4, GPIO5, GPIO6, GPIO7, GPIO10 (optional STATE PIN)

---

## 🚀 Features

- Telnet Server on port 23 (up to 4 simultaneous clients)
- Web Interface for configuration (port 80)
- ESP‑NOW wireless pendant support
- Automatic WiFi selection from known networks
- Static IP configuration for STA mode
- Access Point fallback (always available)
- NVS Storage for persistent settings
- Serial bridge between WiFi clients and UART
- **GrblHAL Advanced:** Configurable state pin, client filtering, auto-reset on disconnect
- **Multi-pendant support:** Up to 4 simultaneous ESP‑NOW clients

---

# ⚙️ Two Ways to Configure

## 1️⃣ Compiling from source

1. Edit `include/MyWiFiData.h`:
   ```c
   static const default_network_t default_networks[] = {
       {"Your_SSID", "Your_Password"},
       {"Another_SSID", "Another_Password"},
   };
   ```
2. Compile and flash:
   ```
   pio run --target upload
   ```
3. The bridge will automatically connect to your network  
4. No need to use the web interface

---

## 2️⃣ Using precompiled firmware

1. Flash the firmware  
2. Connect to WiFi: **ESP32-C3-Serial-Bridge**  
3. Password: **12345678**  
4. Open browser: **http://192.168.4.1**  
5. Add your WiFi networks via Web UI  

---

# ⚡ Quick Start (Precompiled Firmware)

1. Download `ESP32-C3-Serial-Bridge.bin`
2. Flash:
   ```
   esptool.py --chip esp32c3 --port COMx write_flash 0x10000 ESP32-C3-Serial-Bridge.bin
   ```
3. Power on the ESP32‑C3  
4. Connect to WiFi AP  
5. Open **http://192.168.4.1**
6. From the Web UI: **Reset to Default → Reboot → Load Configuration → Save**

---

# 🛠️ Building from Source

### Prerequisites
- PlatformIO CLI or VSCode + PlatformIO

### Commands
```
pio run                 # Build
pio run --target upload # Flash
pio device monitor      # Serial monitor
pio run --target erase  # Factory reset
```

---

# 📁 Project Structure

```
ESP32-C3-Serial-Bridge/
│
├── include/                      # Header files
│   ├── config.h                  # System configuration
│   ├── my_logs.h                 # Logging control
│   ├── espnow_config.h           # ESP-NOW settings
│   ├── WiFiSelector.h            # WiFi network selection
│   ├── nvs_storage.h             # NVS configuration storage
│   ├── web_server.h              # Web server interface
│   ├── wifi_manager.h            # WiFi management
│   ├── grblHAL_advanced.h        # GrblHAL advanced features (header)
│   └── MyWiFiData.h              # YOUR networks (edit this!)
│
├── src/                          # Source files
│   ├── main.c                    # Entry point
│   ├── tcp_server.c              # Telnet server
│   ├── espnow_handler.c          # ESP-NOW communication
│   ├── serial_handler.c          # UART serial bridge
│   ├── wifi_manager.c            # WiFi initialization
│   ├── WiFiSelector.c            # Network scanning
│   ├── nvs_storage.c             # NVS operations
│   ├── web_server.c              # HTTP web server
│   └── grblHAL_advanced.c        # GrblHAL advanced features (implementation)
│
├── firmware/                     # Precompiled firmware
│   ├── bootloader.bin            # Bootloader (0x0)
│   ├── partitions.bin            # Partition table (0x8000)
│   ├── ESP32-C3-Serial-Bridge.bin # Main firmware (0x10000)
│   ├── flash.bat                 # Windows flasher
│   └── flash.sh                  # Linux/Mac flasher
│
├── Peer_example/                 # ESP-NOW pendant example
│   ├── Peer_example.ino          # Arduino IDE sketch
│   └── README.md                 # Pendant documentation
│
├── platformio.ini
├── CMakeLists.txt
└── README.md

```

---

# 🌐 Connection Methods

## 🔹 Method 1 — Access Point (Always available)
1. Connect to WiFi: **ESP32-C3-Serial-Bridge**  
2. Password: **12345678**  
3. Browser: **http://192.168.4.1**  
4. Telnet:
   ```
   telnet 192.168.4.1 23
   ```

## 🔹 Method 2 — Home/Work Network (STA mode)
1. Bridge connects automatically  
2. Browser:
   ```
   http://192.168.1.123
   ```
3. Telnet:
   ```
   telnet 192.168.1.123 23
   ```

---

# 🖥️ Web Interface

Access:
- AP mode: **http://192.168.4.1**
- STA mode: **http://192.168.1.123**

Features:
- System status (IP, memory, uptime)
- Add/remove WiFi networks
- Configure AP SSID/password/channel
- Configure static IP
- Debug logs (0/1/2)
- Reset to defaults
- Reboot device

### **GrblHAL Configuration Tab**
- State Pin selection (GPIO4,5,6,7,10 or Disabled)
- Signal Polarity (LOW/HIGH when connected)
- Active Clients filter (Any / Telnet Only / ESP‑NOW Only)
- Reset on Telnet Disconnect toggle

---

# 📡 ESP‑NOW Pendant Support

ESP‑NOW advantages:
- No router required  
- Ultra‑low latency (<10ms)  
- Low power  
- Simple pairing  
- Robust peer‑to‑peer communication  

How it works:
1. Pendant sends "PAIR"
2. Bridge replies "PAIR_OK"
3. Pendant sends G‑code packets with sequence numbers
4. Bridge ACKs each packet
5. Responses are sent back via ESP‑NOW

Comparison:

| Feature           | ESP‑NOW Pendant | Telnet |
|------------------|-----------------|--------|
| Latency          | <10ms           | 50–200ms |
| Router required  | No              | Yes |
| Power usage      | Very low        | High |
| Range            | ~100m LOS       | Router‑dependent |
| Best use case    | Manual control  | PC G‑code streaming |

---

# 🎛️ GrblHAL Advanced Features

The bridge includes special features designed for CNC machines running GrblHAL:

### 🔌 State Pin (Output)
- Configurable GPIO pin that signals connection status  
- **Safe pins:** GPIO4, GPIO5, GPIO6, GPIO7, GPIO10  
- **Signal Polarity:** Choose LOW or HIGH when client(s) connected  

### 👥 Active Clients Filter
| Mode | Description |
|------|-------------|
| **Any** | Telnet OR ESP‑NOW (default) |
| **Telnet Only** | Only wireless network clients |
| **ESP‑NOW Only** | Only wireless pendants |

### 🔄 Reset on Disconnect
- Automatically sends Ctrl‑X (0x18) to GrblHAL when a Telnet client disconnects  
- Prevents machine from staying in alarm state  
- Configurable via Web UI

---

# 🎮 Pendant Example (Peer_example/)

Features:
- Auto pairing
- Channel scanning
- G‑code sending
- Real‑time commands (reset, start, hold)
- ACK‑based reliability
- Heartbeat timeout

Usage:
1. Open `Peer_example.ino` in Arduino IDE  
2. Select **ESP32‑S3 Dev Module**  
3. Upload  
4. Open Serial Monitor (115200)  
5. Pendant pairs automatically  

---

# 🧩 Default Configuration

| Setting | Value |
|--------|--------|
| AP SSID | ESP32-C3-Serial-Bridge |
| AP Password | 12345678 |
| AP Channel | 6 |
| AP IP | 192.168.4.1 |
| STA Static IP | 192.168.1.123 |
| Gateway | 192.168.1.1 |
| Netmask | 255.255.255.0 |
| Telnet Port | 23 |
| Web Port | 80 |
| UART Baud | 115200 |
| UART TX Pin | GPIO21 |
| UART RX Pin | GPIO20 |
| Debug Level | 0 |
| **State Pin** | Disabled |
| **Signal Polarity** | HIGH when connected |
| **Active Clients** | Any |
| **Reset on Telnet Disconnect** | Enabled |

---

# 🧪 Troubleshooting

### Can't connect to web interface
1. Try AP mode: **http://192.168.4.1**
2. Try Telnet: `telnet 192.168.4.1 23`
3. Check serial logs
4. Ensure you're using **http://** not https

### WiFi not connecting
1. Connect to AP  
2. Add your network in Web UI  
3. Ensure static IP matches your LAN  
4. Save + Reboot  

### ESP‑NOW pairing fails
1. Ensure pendant uses same WiFi channel  
2. Check serial log for: `📡 Canale ESP-NOW: X`  
3. Retry pairing  

### State pin not working
1. Ensure pin is not used by other hardware  
2. Check polarity setting  
3. Verify Active Clients filter  
4. Test with LED or multimeter  

### Multiple ESP‑NOW pendants
- Supports up to 4 pendants  
- Each must send "PAIR"  
- Use `espnow_clear_all_peers()` to reset all connections  

### Reset on disconnect not working
1. Ensure feature is enabled  
2. Works only for Telnet  
3. Verify GrblHAL accepts Ctrl‑X  

---

# 🔌 API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| / | GET | Web UI |
| /api/status | GET | WiFi + ESP‑NOW status |
| /api/sysinfo | GET | Memory, version |
| /api/networks | GET | Known WiFi networks |
| /api/settings | GET | Returns: ap_ssid, ap_password, ap_channel, static_ip, use_static_ip, **state_pin**, **state_pin_mode**, **client_mode**, **reset_on_disconnect** |
| /api/debug | GET | Debug level |
| /api/networks/add | POST | Add WiFi network |
| /api/networks/remove | POST | Remove WiFi network |
| /api/settings | POST | Save settings |
| /api/debug | POST | Set debug level |
| /api/reset | POST | Factory reset |
| /api/reboot | POST | Reboot |

---

# 🐞 Debug Logs

| Level | Name | Description |
|-------|------|-------------|
| 0 | OFF | No logs |
| 1 | BASIC | Errors + key events |
| 2 | VERBOSE | Full WiFi + ESP‑NOW logs |

Enable via Web UI or edit in `config.h`:
```c
#define ENABLE_DEBUG_LOGS 0
```

---

# 📄 License (MIT)

Copyright (c) 2026  
Permission is hereby granted, free of charge, to any person obtaining a copy  
of this software and associated documentation files (the "Software"), to deal  
in the Software without restriction…
