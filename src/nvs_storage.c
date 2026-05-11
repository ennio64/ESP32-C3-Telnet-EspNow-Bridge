#include <string.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "nvs_storage.h"
#include "MyWiFiData.h"
#include "config.h"

static const char *TAG = "NVS_STORAGE";
static bridge_config_t g_config;
static nvs_handle_t nvs_handle_storage = 0;

// Inizializza la configurazione di default
static void init_default_config(bridge_config_t *config) {
    memset(config, 0, sizeof(bridge_config_t));
    
    // Carica le reti da MyWiFiData.h
    for (int i = 0; i < (int)DEFAULT_NETWORKS_COUNT && i < MAX_KNOWN_NETWORKS; i++) {
        strncpy(config->networks[i].ssid, default_networks[i].ssid, MAX_SSID_LEN - 1);
        config->networks[i].ssid[MAX_SSID_LEN - 1] = '\0';
        strncpy(config->networks[i].password, default_networks[i].password, MAX_PASSWORD_LEN - 1);
        config->networks[i].password[MAX_PASSWORD_LEN - 1] = '\0';
        config->network_count++;
    }
    
    // Imposta i valori di default
    config->ap_channel = 6;
    strcpy(config->ap_ssid, "ESP32-C3-Serial-Bridge");
    strcpy(config->ap_password, "12345678");
    strcpy(config->static_ip, "192.168.1.123");
    strcpy(config->static_gateway, "192.168.1.1");
    strcpy(config->static_netmask, "255.255.255.0");
    config->use_static_ip = true;
    config->debug_level = ENABLE_DEBUG_LOGS;
    config->configured = true;
    
    ESP_LOGI(TAG, "Default config initialized with %d networks, debug_level=%d", 
             config->network_count, config->debug_level);
}

void nvs_storage_init(void) {
    bridge_config_t default_config;
    init_default_config(&default_config);
    
    esp_err_t err = nvs_open("bridge_cfg", NVS_READWRITE, &nvs_handle_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore apertura NVS: %d", err);
        return;
    }
    
    // Carica configurazione o usa default
    if (!nvs_storage_load_config(&g_config)) {
        ESP_LOGI(TAG, "Nessuna config salvata, uso default con %d reti", default_config.network_count);
        memcpy(&g_config, &default_config, sizeof(bridge_config_t));
        nvs_storage_save_config();
    } else {
        // Verifica che l'IP statico sia valido, altrimenti correggi
        if (strlen(g_config.static_ip) == 0 || strcmp(g_config.static_ip, "0.0.0.0") == 0) {
            ESP_LOGW(TAG, "IP statico non valido, correggo a 192.168.1.123");
            strcpy(g_config.static_ip, "192.168.1.123");
            strcpy(g_config.static_gateway, "192.168.1.1");
            strcpy(g_config.static_netmask, "255.255.255.0");
            g_config.use_static_ip = true;
            nvs_storage_save_config();
        }
        // Se debug_level non è valido, correggi
        if (g_config.debug_level > 2) {
            ESP_LOGW(TAG, "debug_level non valido (%d), resetto a 0", g_config.debug_level);
            g_config.debug_level = 0;
            nvs_storage_save_config();
        }
        ESP_LOGI(TAG, "Configurazione caricata: %d reti, IP=%s, debug_level=%d", 
                 g_config.network_count, g_config.static_ip, g_config.debug_level);
    }
}

bool nvs_storage_save_config(void) {
    if (nvs_handle_storage == 0) return false;
    
    esp_err_t err = nvs_set_blob(nvs_handle_storage, "config", &g_config, sizeof(bridge_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore salvataggio: %d", err);
        return false;
    }
    
    err = nvs_commit(nvs_handle_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore commit: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Configurazione salvata");
    return true;
}

bool nvs_storage_load_config(bridge_config_t *config) {
    if (nvs_handle_storage == 0) return false;
    
    size_t size = sizeof(bridge_config_t);
    esp_err_t err = nvs_get_blob(nvs_handle_storage, "config", config, &size);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "📖 CONFIGURAZIONE CARICATA DA NVS:");
        ESP_LOGI(TAG, "   Reti configurate: %d", config->network_count);
        for (int i = 0; i < config->network_count; i++) {
            ESP_LOGI(TAG, "   [%d] SSID: '%s'", i, config->networks[i].ssid);
            ESP_LOGI(TAG, "   [%d] PWD:  '%s'", i, config->networks[i].password);
            ESP_LOGI(TAG, "   [%d] len:  %d", i, strlen(config->networks[i].password));
        }
        ESP_LOGI(TAG, "   AP SSID: '%s'", config->ap_ssid);
        ESP_LOGI(TAG, "   AP PWD:  '%s'", config->ap_password);
        ESP_LOGI(TAG, "   Static IP: %s (use: %s)", config->static_ip, 
                 config->use_static_ip ? "YES" : "NO");
        ESP_LOGI(TAG, "   Debug Level: %d", config->debug_level);
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGI(TAG, "Nessuna configurazione salvata in NVS (err=%d)", err);
    }
    
    return (err == ESP_OK);
}

bool nvs_storage_add_network(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "📝 AGGIUNTA NUOVA RETE:");
    ESP_LOGI(TAG, "   SSID: '%s'", ssid);
    ESP_LOGI(TAG, "   PWD:  '%s'", password);
    ESP_LOGI(TAG, "   PWD len: %d", strlen(password));
    ESP_LOGI(TAG, "========================================");
    
    if (g_config.network_count >= MAX_KNOWN_NETWORKS) {
        ESP_LOGW(TAG, "Limite reti raggiunto (%d)", MAX_KNOWN_NETWORKS);
        return false;
    }
    
    // Controlla se esiste già
    for (int i = 0; i < g_config.network_count; i++) {
        if (strcmp(g_config.networks[i].ssid, ssid) == 0) {
            // Aggiorna password
            strncpy(g_config.networks[i].password, password, MAX_PASSWORD_LEN - 1);
            g_config.networks[i].password[MAX_PASSWORD_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Password aggiornata per rete: %s", ssid);
            return nvs_storage_save_config();
        }
    }
    
    // Aggiungi nuova
    strncpy(g_config.networks[g_config.network_count].ssid, ssid, MAX_SSID_LEN - 1);
    g_config.networks[g_config.network_count].ssid[MAX_SSID_LEN - 1] = '\0';
    
    strncpy(g_config.networks[g_config.network_count].password, password, MAX_PASSWORD_LEN - 1);
    g_config.networks[g_config.network_count].password[MAX_PASSWORD_LEN - 1] = '\0';
    
    g_config.network_count++;
    g_config.configured = true;
    
    ESP_LOGI(TAG, "✅ Rete salvata in NVS: %s", ssid);
    return nvs_storage_save_config();
}

bool nvs_storage_remove_network(const char *ssid) {
    for (int i = 0; i < g_config.network_count; i++) {
        if (strcmp(g_config.networks[i].ssid, ssid) == 0) {
            // Shift rimanenti
            for (int j = i; j < g_config.network_count - 1; j++) {
                memcpy(&g_config.networks[j], &g_config.networks[j + 1], sizeof(network_t));
            }
            g_config.network_count--;
            
            ESP_LOGI(TAG, "Rete rimossa: %s", ssid);
            return nvs_storage_save_config();
        }
    }
    
    ESP_LOGW(TAG, "Rete non trovata: %s", ssid);
    return false;
}

const bridge_config_t* nvs_storage_get_config(void) {
    return &g_config;
}

bridge_config_t* nvs_storage_get_config_mutable(void) {
    return &g_config;
}

void nvs_storage_reset_default(void) {
    ESP_LOGI(TAG, "Reset configurazione a default");
    bridge_config_t default_config;
    init_default_config(&default_config);
    memcpy(&g_config, &default_config, sizeof(bridge_config_t));
    nvs_storage_save_config();
}