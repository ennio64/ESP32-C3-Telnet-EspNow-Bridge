// ============================================================================
//    ESP32‑S3 Peer (ESP‑NOW)
// ============================================================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <vector>

#define SERIAL_BAUD 115200

// --- COMANDI REALTIME GRBLHAL ---
#define CMD_RESET        0x18
#define CMD_CYCLE_START  0x81
#define CMD_FEED_HOLD    0x82

// --- TIMEOUT E RETRY ---
#define HEARTBEAT_TIMEOUT_MS    3000   // 3 secondi senza dati = bridge perso
#define PAIR_RETRY_INTERVAL_MS  5000   // 5 secondi tra tentativi pairing

// --- RETI CONOSCIUTE (uguali a quelle del bridge) ---
const char* knownNetworks[] = {
    "TISCALI-5311",
    "TISCALI-07EE7E",
    "HUAWEI P30 lite"
};
const int knownCount = 3;

// --- VARIABILI GLOBALI ---
uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t bridge_mac[6] = {0};
bool paired = false;
bool bridge_peer_added = false;
uint32_t last_print = 0;
uint32_t last_data_received = 0;  // ← NUOVO: timestamp ultimo dato ricevuto
String buffer = "";
uint16_t sequence = 0;
unsigned long pairStartTime = 0;
int currentChannel = 11;  // Default, verrà aggiornato dalla scansione

// ACK tracking
volatile uint16_t lastAckSeq = 0xFFFF;

// ---------------------------------------------------------
//  SCANSIONE CANALE TRAMITE RETI WIFI CONOSCIUTE
// ---------------------------------------------------------
bool scanForChannel() {
    Serial.println("\n🔍 Scansione reti WiFi per trovare il canale...");
    
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("❌ Nessuna rete trovata");
        WiFi.scanDelete();
        return false;
    }
    
    Serial.printf("📡 Trovate %d reti WiFi:\n", n);
    
    int foundChannel = -1;
    String foundNetwork = "";
    int bestRSSI = -127;
    
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int channel = WiFi.channel(i);
        int rssi = WiFi.RSSI(i);
        
        Serial.printf("   %s (Canale: %d, RSSI: %d dBm)\n", ssid.c_str(), channel, rssi);
        
        for (int j = 0; j < knownCount; j++) {
            if (ssid == knownNetworks[j]) {
                if (rssi > bestRSSI) {
                    bestRSSI = rssi;
                    foundChannel = channel;
                    foundNetwork = ssid;
                }
                break;
            }
        }
    }
    
    WiFi.scanDelete();
    
    if (foundChannel > 0) {
        Serial.printf("\n✅ Rete trovata: %s\n", foundNetwork.c_str());
        Serial.printf("   Canale: %d\n", foundChannel);
        Serial.printf("   RSSI: %d dBm\n", bestRSSI);
        currentChannel = foundChannel;
        return true;
    }
    
    Serial.println("\n⚠️ Nessuna rete conosciuta trovata, uso canale default 11");
    return false;
}

// ---------------------------------------------------------
//  RESET PAIRING (RIPARTE DA ZERO)
// ---------------------------------------------------------
void resetPairing() {
    if (paired) {
        Serial.println("\n🔄 Bridge perso! Reset pairing in corso...");
    }
    
    paired = false;
    bridge_peer_added = false;
    memset(bridge_mac, 0, 6);
    lastAckSeq = 0xFFFF;
    sequence = 0;
    
    // Rimuovi il vecchio peer se esiste
    if (bridge_mac[0] != 0) {
        esp_now_del_peer(bridge_mac);
    }
    
    // Ritenta pairing
    Serial.println("📡 Invio PAIR al bridge...");
    esp_now_send(broadcastMac, (uint8_t*)"PAIR", 4);
    pairStartTime = millis();
}

// ---------------------------------------------------------
//  VALIDAZIONE CARATTERI
// ---------------------------------------------------------
bool isValidGcodeChar(char c) {
    return (c >= ' ' && c <= '~') || c == '\n' || c == '\r';
}

// ---------------------------------------------------------
//  GENERATORE G-CODE DI TEST
// ---------------------------------------------------------
String generateTestGcode(int repetitions, float x1 = 10.0, float x2 = 0.0) {
    String out;
    out.reserve(repetitions * 20 + 10);

    for (int i = 0; i < repetitions; i++) {
        out += "G0 X";
        out += String(x1, 3);
        out += "\n";

        out += "G0 X";
        out += String(x2, 3);
        out += "\n";
    }

    out += "M30\n";
    return out;
}

// ---------------------------------------------------------
//  CALLBACK RICEZIONE ESP-NOW
// ---------------------------------------------------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (!info || len <= 0) return;

    // Aggiorna timestamp ultimo dato ricevuto (← NUOVO)
    last_data_received = millis();

    // ACK (3 byte)
    if (len == 3 && data[2] == 0x01) {
        uint16_t seq = data[0] | (data[1] << 8);
        lastAckSeq = seq;
        return;
    }

    // PAIR_OK
    if (len == 7 && memcmp(data, "PAIR_OK", 7) == 0) {
        memcpy(bridge_mac, info->src_addr, 6);

        esp_now_peer_info_t bridgePeer = {};
        memcpy(bridgePeer.peer_addr, bridge_mac, 6);
        bridgePeer.channel = currentChannel;
        bridgePeer.encrypt = false;

        esp_err_t res = esp_now_add_peer(&bridgePeer);

        if (res == ESP_OK || res == ESP_ERR_ESPNOW_EXIST) {
            bridge_peer_added = true;
            paired = true;

            Serial.println("\n✅ BRIDGE TROVATO!");
            Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          bridge_mac[0], bridge_mac[1], bridge_mac[2],
                          bridge_mac[3], bridge_mac[4], bridge_mac[5]);
            Serial.printf("Canale: %d\n", currentChannel);
            Serial.println("Pronto per inviare G-code!\n");
            Serial.print("> ");
        }
        return;
    }

    // Risposte dalla stampante
    for (int i = 0; i < len; i++) {
        char c = (char)data[i];
        if (isValidGcodeChar(c)) buffer += c;
    }

    if (buffer.length() > 0 &&
        (millis() - last_print > 100 || buffer.indexOf('\n') >= 0)) {

        Serial.print(buffer);
        buffer = "";
        last_print = millis();
    }
}

// ---------------------------------------------------------
//  INVIO SINGOLA RIGA G-CODE
// ---------------------------------------------------------
bool sendGcodeLine(const String& cmd) {
    if (!paired || !bridge_peer_added) {
        Serial.println("⏳ Attendi pairing...");
        return false;
    }

    uint16_t len = cmd.length();
    if (len > 246) len = 246;

    uint8_t packet[250];
    packet[0] = sequence & 0xFF;
    packet[1] = (sequence >> 8) & 0xFF;
    packet[2] = len & 0xFF;
    packet[3] = (len >> 8) & 0xFF;
    memcpy(&packet[4], cmd.c_str(), len);

    Serial.printf("📤 Invio [seq=%d]: %s", sequence, cmd.c_str());

    esp_err_t err = esp_now_send(bridge_mac, packet, len + 4);
    if (err != ESP_OK) {
        Serial.printf(" ❌ ERR=%d\n", err);
        return false;
    }

    // Attesa ACK
    unsigned long t0 = millis();
    while (lastAckSeq != sequence) {
        if (millis() - t0 > 3000) {
            Serial.println(" ⚠️ Timeout ACK");
            return false;
        }
        delay(1);
    }

    Serial.println(" ✓");
    sequence++;
    return true;
}

// ---------------------------------------------------------
//  INVIO STREAMING DI G-CODE (MULTI-RIGA)
// ---------------------------------------------------------
bool sendGCodeEspNowStream(const String &gcode) {
    Serial.println("📦 Parsing G-code...");

    std::vector<String> lines;
    lines.reserve(128);

    String cur = "";
    for (int i = 0; i < gcode.length(); i++) {
        char c = gcode[i];
        if (c == '\n' || c == '\r') {
            if (cur.length() > 0) lines.push_back(cur + "\n");
            cur = "";
        } else {
            cur += c;
        }
    }
    if (cur.length() > 0) lines.push_back(cur + "\n");

    Serial.printf("📊 Righe da inviare: %d\n", lines.size());

    int ok = 0;

    for (int i = 0; i < lines.size(); i++) {
        Serial.printf("➡️ [%d/%d] %s", i+1, lines.size(), lines[i].c_str());
        if (sendGcodeLine(lines[i])) ok++;
        else Serial.println("❌ Errore invio riga");
    }

    Serial.printf("🎉 COMPLETATO: %d/%d righe OK\n", ok, lines.size());
    return ok == lines.size();
}

// ---------------------------------------------------------
//  INVIO COMANDI REALTIME
// ---------------------------------------------------------
void sendRealtime(uint8_t cmd) {
    if (!paired || !bridge_peer_added) {
        Serial.println("⏳ Attendi pairing...");
        return;
    }

    uint8_t packet[5];
    packet[0] = sequence & 0xFF;
    packet[1] = (sequence >> 8) & 0xFF;
    packet[2] = 1;
    packet[3] = 0;
    packet[4] = cmd;

    Serial.printf("⚡ Realtime [seq=%d] cmd=0x%02X\n", sequence, cmd);

    esp_err_t err = esp_now_send(bridge_mac, packet, 5);
    if (err == ESP_OK) {
        sequence++;
    } else {
        Serial.printf(" ❌ ERR=%d\n", err);
    }
}

void sendReset()      { sendRealtime(CMD_RESET); }
void sendCycleStart() { sendRealtime(CMD_CYCLE_START); }
void sendFeedHold()   { sendRealtime(CMD_FEED_HOLD); }

// ---------------------------------------------------------
//  SETUP
// ---------------------------------------------------------
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    Serial.println("\n====================================");
    Serial.println("ESP32-S3 Peer ESP NOW");
    Serial.println("====================================\n");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // ========== SCANSIONE CANALE ==========
    if (scanForChannel()) {
        Serial.printf("📡 Imposto canale ESP-NOW: %d\n", currentChannel);
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    } else {
        Serial.println("📡 Uso canale default: 11");
        esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);
        currentChannel = 11;
    }

    // ========== INIZIALIZZAZIONE ESP-NOW ==========
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed!");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    // Peer broadcast per PAIR (usa lo stesso canale)
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, broadcastMac, 6);
    broadcastPeer.channel = currentChannel;
    broadcastPeer.encrypt = false;
    esp_now_add_peer(&broadcastPeer);

    Serial.println("ESP-NOW OK");
    Serial.println("Invio PAIR al bridge...\n");

    esp_now_send(broadcastMac, (uint8_t*)"PAIR", 4);
    pairStartTime = millis();
    last_data_received = millis();  // ← NUOVO: inizializza timestamp
}

// ---------------------------------------------------------
//  LOOP
// ---------------------------------------------------------
void loop() {
    // ========== NUOVO: HEARTBEAT TIMEOUT ==========
    // Se siamo paired ma non riceviamo dati per 3 secondi, reset pairing
    if (paired && (millis() - last_data_received) > HEARTBEAT_TIMEOUT_MS) {
        Serial.println("\n⚠️ Nessun dato dal bridge per 3 secondi!");
        resetPairing();
        delay(500);  // Pausa prima di riprovare
    }
    
    // Ritenta pairing se non siamo paired
    if (!paired && (millis() - pairStartTime) > PAIR_RETRY_INTERVAL_MS) {
        Serial.println("❌ Bridge non trovato, riprovo...");
        esp_now_send(broadcastMac, (uint8_t*)"PAIR", 4);
        pairStartTime = millis();
    }

    // Input seriale
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        // --- TEST AUTOMATICO ---
        if (input.equalsIgnoreCase("Test")) {
            Serial.println("🚀 Avvio test G-code automatico (200 cicli)...");
            String gcode = generateTestGcode(200, 10.0, 0.0);
            sendGCodeEspNowStream(gcode);
            Serial.print("> ");
            return;
        }

        // --- COMANDI REALTIME ---
        if (input.equalsIgnoreCase("reset")) {
            sendReset();
            Serial.println("⚡ Soft Reset inviato");
            Serial.print("> ");
            return;
        }

        if (input.equalsIgnoreCase("start")) {
            sendCycleStart();
            Serial.println("▶️ Cycle Start inviato");
            Serial.print("> ");
            return;
        }

        if (input.equalsIgnoreCase("hold")) {
            sendFeedHold();
            Serial.println("⏸️ Feed Hold inviato");
            Serial.print("> ");
            return;
        }

        // Comando manuale
        if (input.length() > 0) {
            String cmd = input;
            if (!cmd.endsWith("\n")) cmd += "\n";
            sendGcodeLine(cmd);
        }

        Serial.print("> ");
    }

    delay(10);
}