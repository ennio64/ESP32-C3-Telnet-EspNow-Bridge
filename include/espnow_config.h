#ifndef ESPNOW_CONFIG_H
#define ESPNOW_CONFIG_H

// Configurazione ESP-NOW
#define ESPNOW_CHANNEL          1
#define ESPNOW_MAX_PAYLOAD      250
#define ESPNOW_WAIT_TIME_MS     200
#define ESPNOW_MAX_RETRY        3

// Comandi speciali
#define CMD_PAIR                "PAIR"
#define CMD_PAIR_OK             "PAIR_OK"
#define CMD_PING                "PING"
#define CMD_PONG                "PONG"

// Heartbeat attivo (bridge invia ping)
#define ESPNOW_PING_IDLE_MS         (5 * 1000)   // 5 sec inattività prima del primo ping
#define ESPNOW_PING_INTERVAL_MS     (2.5 * 1000)    // 2.5 sec tra un ping e il successivo
#define ESPNOW_PING_MAX_RETRIES     3             // max tentativi senza risposta

#endif