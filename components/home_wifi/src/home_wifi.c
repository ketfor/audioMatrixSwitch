#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "lwip/apps/netbiosns.h"
#include "mdns.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs_preferences.h"
#include "home_json.h"
#include "home_wifi.h"

ESP_EVENT_DEFINE_BASE(HOME_WIFI_EVENT);
#define WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define WIFI_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

#define MDNS_INSTANCE "esp home web server"

#define TAG "home_wifi"
#define NVSGROUP "wifi"
#define MUTEX_TAKE_TICK_PERIOD 1000 / portTICK_PERIOD_MS

static wifiConfig_t wifiConfig;
static int s_retry_num = 0;
static esp_ip4_addr_t iPv4;
static SemaphoreHandle_t xMutex;
static nvs_handle_t pHandle = 0;
static esp_netif_t *netif = NULL;

//wifiState_t wifiState;

BaseType_t getMAC(uint8_t *mac){
    esp_err_t ret = esp_wifi_get_mac(0, mac); // для WIFI Station

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to retrieve the MAC address of the WiFi interface: %s", esp_err_to_name(ret));
        return pdFALSE;
    }
    return pdTRUE;
}

BaseType_t getIPv4Str(char * iPv4Str){
    snprintf(iPv4Str, 16, IPSTR, IP2STR(&iPv4));
    return pdTRUE;
}

static void initializeMdns(const char *hostname)
{
    mdns_service_remove("_http", "_tcp");
    mdns_init();
    mdns_hostname_set(hostname);
    mdns_instance_name_set(MDNS_INSTANCE);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "esp32"},
        {"path", "/"}
    };

    ESP_ERROR_CHECK(mdns_service_add(MDNS_INSTANCE, "_http", "_tcp", 80, serviceTxtData,
                                    sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

static void initializeNetbiosns(const char *hostname)
{
    netbiosns_init();
    netbiosns_set_name(hostname);
}

static void eventPost(int32_t eventId)
{
    esp_err_t err = esp_event_post(HOME_WIFI_EVENT, eventId, NULL, 0, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event to \"%s\" #%ld: %d (%s)", HOME_WIFI_EVENT, eventId, err, esp_err_to_name(err));
    };
}

static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        eventPost(HOME_WIFI_EVENT_START);
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            ESP_LOGI(TAG, "failed to connect to SSID: %s, password: %s", wifiConfig.ssid, wifiConfig.password);
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        iPv4 = event->ip_info.ip;
        ESP_LOGI(TAG, "connected to AP SSID: %s, password: %s", WIFI_SSID, WIFI_PASS);
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&iPv4));
        s_retry_num = 0;
        eventPost(HOME_WIFI_EVENT_START);
    }
}

static BaseType_t initializeWifi(void)
{
    char modeStr[8];
    if (wifiConfig.mode == WIFI_MODE_AP)
        strlcpy(modeStr, "soft-ap", sizeof(modeStr));
    else if (wifiConfig.mode == WIFI_MODE_STA)
        strlcpy(modeStr, "station", sizeof(modeStr));
    else
        strlcpy(modeStr, "unknow", sizeof(modeStr));

    ESP_LOGI(TAG, "Initializing wifi %s", modeStr);

    initializeMdns(wifiConfig.hostname);
    initializeNetbiosns(wifiConfig.hostname);

    if (netif != NULL) {
        ESP_LOGI(TAG, "Wifi %s deinit begin...", modeStr);
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_deinit());
        esp_netif_destroy_default_wifi(netif);
        netif = NULL;
        ESP_LOGI(TAG, "Wifi %s deinit complite", modeStr);
    }

    if (wifiConfig.mode == WIFI_MODE_AP)
        netif = esp_netif_create_default_wifi_ap();
    else if (wifiConfig.mode == WIFI_MODE_STA)
        netif = esp_netif_create_default_wifi_sta();
    else {
        ESP_LOGE(TAG, "Wifi init fail: unknow mode: %s", modeStr);
        return pdFALSE;
    }
    esp_netif_set_hostname(netif, wifiConfig.hostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (wifiConfig.mode == WIFI_MODE_AP) {
        wifi_config_t wifi_config = {
            .ap = {
                .ssid_len = strlen(wifiConfig.ssid),
                .channel = 11,
                .max_connection = 5,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
                .authmode = WIFI_AUTH_WPA3_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else // CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
                .authmode = WIFI_AUTH_WPA2_PSK,
#endif
                .pmf_cfg = {
                        .required = true,
                },
            },
        };
        strlcpy((char *)wifi_config.ap.ssid, wifiConfig.ssid, sizeof(wifi_config.ap.ssid));
        strlcpy((char *)wifi_config.ap.password, wifiConfig.password, sizeof(wifi_config.ap.password));

        if (strlen(wifiConfig.password) < 8) {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

        ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
        esp_netif_ip_info_t ipInfo;
        ipInfo.gw.addr = wifiConfig.ipaddr.addr;
        ipInfo.ip.addr = wifiConfig.ipaddr.addr;
        ipInfo.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ipInfo));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    }
    else if (wifiConfig.mode == WIFI_MODE_STA) {
        wifi_config_t wifi_config = {
            .sta = {
                .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
                .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
                .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
            },
        };
        strlcpy((char *)wifi_config.sta.ssid, wifiConfig.ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, wifiConfig.password, sizeof(wifi_config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
    else {
        ESP_LOGE(TAG, "Wifi init fail: unknow mode: %s", modeStr);
        return pdFALSE;
    }

    s_retry_num = 0;
    if (esp_wifi_start() == ESP_OK) {
        ESP_LOGI(TAG, "Wifi %s initialized complite. ssid: %s, password:%s", 
            modeStr, wifiConfig.ssid, wifiConfig.password);
        return pdTRUE;
    }
    else {
        ESP_LOGE(TAG, "Wifi %s initialize failed. ssid: %s, password:%s", 
            modeStr, wifiConfig.ssid, wifiConfig.password);
        return pdFALSE;
    }
}

wifiConfig_t * getWifiConfig()
{
    return &wifiConfig;
}

const char * getJsonWifiConfig()
{
    // payload
    char ip4[16];
    snprintf(ip4, sizeof(ip4), IPSTR, IP2STR(&(&wifiConfig)->ipaddr));
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(root, "mode", wifiConfig.mode);
    cJSON_AddStringToObject(root, "ip", ip4);
    cJSON_AddStringToObject(root, "hostname", wifiConfig.hostname); 
    cJSON_AddStringToObject(root, "ssid", wifiConfig.ssid); 
    cJSON_AddStringToObject(root, "password", wifiConfig.password); 

    char *jsonConfig = cJSON_Print(root);
    cJSON_Delete(root);
    return jsonConfig;
}

static BaseType_t wifiConfigure()
{    
    ESP_LOGI(TAG, "Setting wifi config...");
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGW(TAG, "Failed setting wifi config");
        return pdFALSE;
    }
    nvsOpen(NVSGROUP, NVS_READONLY, &pHandle);
    getUInt8Pref(pHandle, NVSKEY_WIFI_MODE, &(wifiConfig.mode));
    getUInt32Pref(pHandle, NVSKEY_WIFI_IPADDR, &(wifiConfig.ipaddr.addr));
    getStrPref(pHandle, NVSKEY_WIFI_HOSTNAME, wifiConfig.hostname, sizeof(wifiConfig.hostname));
    getStrPref(pHandle, NVSKEY_WIFI_SSID, wifiConfig.ssid, sizeof(wifiConfig.ssid));
    getStrPref(pHandle, NVSKEY_WIFI_PASSWORD, wifiConfig.password, sizeof(wifiConfig.password));
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);

    ESP_LOGI(TAG, "Wifi config complite");

    return pdTRUE;
} 

BaseType_t saveWifiConfig(wifiConfig_t *pWifiConfig)
{
    ESP_LOGI(TAG, "Saving wifi config...");

    if (pWifiConfig->ipaddr.addr == 0) {
        int res = sscanf(pWifiConfig->ip, STRIP, STR2IPPOINT((pWifiConfig)->ipaddr));
        if (res != 4) {
            ESP_LOGE(TAG, "Failed saving wifi config: ip '%s' not parsed", pWifiConfig->ip);
            return pdFALSE;
        }
    }
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGE(TAG, "Failed saving wifi config: mutex not taked");
        return pdFALSE;
    }
    nvsOpen(NVSGROUP, NVS_READWRITE, &pHandle);
    setUInt8Pref(pHandle, NVSKEY_WIFI_MODE, pWifiConfig->mode);
    setUInt32Pref(pHandle, NVSKEY_WIFI_IPADDR, pWifiConfig->ipaddr.addr);
    setStrPref(pHandle, NVSKEY_WIFI_HOSTNAME, pWifiConfig->hostname);
    setStrPref(pHandle, NVSKEY_WIFI_SSID, pWifiConfig->ssid);
    setStrPref(pHandle, NVSKEY_WIFI_PASSWORD, pWifiConfig->password);
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);
    ESP_LOGI(TAG, "Wifi config saved");

    wifiConfigure();
    return initializeWifi();
}

BaseType_t setWifiDefaultPreferences() 
{
    ESP_LOGI(TAG, "Setting default preference of wifi");
    wifiConfig_t *pWifiConfig = malloc(sizeof(wifiConfig_t));
 
    pWifiConfig->mode = WIFI_MODE_AP;
    pWifiConfig->ipaddr.addr = ESP_IP4TOADDR(192, 168, 81, 1);
    strlcpy(pWifiConfig->hostname, "Audiomatrix", sizeof(pWifiConfig->hostname));    
    strlcpy(pWifiConfig->ssid, "audiomatrix", sizeof(pWifiConfig->ssid));
    strlcpy(pWifiConfig->password, "12345678", sizeof(pWifiConfig->password));
    saveWifiConfig(pWifiConfig);
    free(pWifiConfig);
    return pdTRUE; 
}

void wifiStationInit(void)
{
    static StaticSemaphore_t xSemaphoreBuffer;
    xMutex = xSemaphoreCreateMutexStatic(&xSemaphoreBuffer);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, NULL));

    if(nvsOpen(NVSGROUP, NVS_READONLY, &pHandle) != pdTRUE ) {
        ESP_LOGW(TAG, "Namespace '%s' notfound", NVSGROUP);
        setWifiDefaultPreferences();
    }
    else {
        nvs_close(pHandle);
        wifiConfigure();
        initializeWifi();
    }

    ESP_LOGI(TAG, "wifi_init finished.");
}