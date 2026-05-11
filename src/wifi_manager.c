#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "wifi_manager.h"
#include "WiFiSelector.h"
#include "nvs_storage.h"
#include "config.h"
#include "my_logs.h"

static const char *TAG = "WIFI_MANAGER";
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA avviato, connessione in corso...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnesso, tentativo di riconnessione...");
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        (void)event;
        ESP_LOGI(TAG, "IP ottenuto: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    // Imposta modalità AP+STA ma NON avvia ancora la connessione
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi inizializzato (in modalità AP+STA)");
}

// Versione originale (mantenuta per compatibilità con AP predefinito)
void wifi_start_ap(void) {
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONNECT,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = WIFI_AP_CHANNEL,
        },
    };
    
    if (strlen(WIFI_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "AP avviato: SSID=%s, password=%s", 
             WIFI_AP_SSID, WIFI_AP_PASSWORD);
}

// NUOVA: Avvia AP con configurazione da NVS
void wifi_start_ap_with_config(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(cfg->ap_ssid),
            .max_connection = WIFI_AP_MAX_CONNECT,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = cfg->ap_channel,
        },
    };
    
    strncpy((char*)ap_config.ap.ssid, cfg->ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
    
    strncpy((char*)ap_config.ap.password, cfg->ap_password, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0';
    
    if (strlen(cfg->ap_password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "AP avviato: SSID=%s, canale=%d", cfg->ap_ssid, cfg->ap_channel);
}

void wifi_start_sta(const char *ssid, const char *password) {
    wifi_config_t sta_config = {0};
    
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    sta_config.sta.ssid[sizeof(sta_config.sta.ssid) - 1] = '\0';
    
    strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.password[sizeof(sta_config.sta.password) - 1] = '\0';
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    ESP_LOGI(TAG, "Connessione a SSID: %s in corso...", ssid);
}

bool wifi_is_connected(void) {
    return (xEventGroupGetBits(wifi_event_group) & CONNECTED_BIT) != 0;
}

const char* wifi_get_ip(void) {
    static char ip_str[16];
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        return ip_str;
    }
    return "0.0.0.0";
}

// NUOVA: Imposta IP statico da configurazione NVS
void set_static_ip_from_config(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    // Se non usa IP statico, esci
    if (!cfg->use_static_ip) {
        ESP_LOGI(TAG, "IP dinamico (DHCP)");
        return;
    }
    
    // Verifica che l'IP sia valido
    if (strlen(cfg->static_ip) == 0 || strcmp(cfg->static_ip, "0.0.0.0") == 0) {
        ESP_LOGW(TAG, "IP statico non valido: '%s', uso DHCP", cfg->static_ip);
        return;
    }
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Impossibile ottenere netif");
        return;
    }
    
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton(cfg->static_ip);
    ip_info.netmask.addr = esp_ip4addr_aton(cfg->static_netmask);
    ip_info.gw.addr = esp_ip4addr_aton(cfg->static_gateway);
    
    // Verifica che l'IP sia valido (non 0 e non broadcast)
    if (ip_info.ip.addr == 0 || ip_info.ip.addr == 0xFFFFFFFF) {
        ESP_LOGW(TAG, "IP statico non valido: %s", cfg->static_ip);
        return;
    }
    
    esp_netif_dhcpc_stop(netif);
    esp_netif_set_ip_info(netif, &ip_info);
    
    ESP_LOGI(TAG, "✅ IP statico impostato: %s (gw:%s)", cfg->static_ip, cfg->static_gateway);
}

// Versione originale (mantenuta per compatibilità)
void wifi_auto_connect(void) {
    ESP_LOGI(TAG, "=== Avvio selezione automatica WiFi ===");
    
    // Disconnetti da eventuali connessioni esistenti
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Seleziona la migliore rete disponibile
    if (selectBestWiFi()) {
        const char* ssid = getSelectedSSID();
        const char* password = getSelectedPassword();
        
        if (ssid && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Connessione a: %s", ssid);
            wifi_start_sta(ssid, password);
        }
    }
    // Se nessuna rete trovata, rimane solo AP (già attivo)
}

// NUOVA: Auto connect usando configurazione NVS
void wifi_auto_connect_from_nvs(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    if (cfg->network_count == 0) {
        ESP_LOGW(TAG, "Nessuna rete configurata in NVS");
        return;
    }
    
    ESP_LOGI(TAG, "=== Avvio selezione automatica WiFi da NVS ===");
    
    // Disconnetti da eventuali connessioni esistenti
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Seleziona la migliore rete disponibile
    if (selectBestWiFi()) {
        const char* ssid = getSelectedSSID();
        const char* password = getSelectedPassword();
        
        if (ssid && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Connessione a: %s", ssid);
            wifi_start_sta(ssid, password);
        }
    }
}