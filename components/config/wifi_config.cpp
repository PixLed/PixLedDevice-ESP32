#include "wifi_config.h"
#include "mqtt_config.h"
#include "esp_err.h"
#include "main.h"

static int s_retry_num = 0;
static bool loop_init = false;
static WifiContext wifiContext = { };

void save_wifi_info_to_nvs(const char* ssid, const char* password) {
  nvs_handle nvs_config_handle;
  ESP_ERROR_CHECK(nvs_open("conf", NVS_READWRITE, &nvs_config_handle));

  ESP_ERROR_CHECK(nvs_set_str(nvs_config_handle, "wifi_ssid", ssid));
  ESP_ERROR_CHECK(nvs_set_str(nvs_config_handle, "wifi_pw", password));
  ESP_ERROR_CHECK(nvs_commit(nvs_config_handle));
  nvs_close(nvs_config_handle);
}

bool load_wifi_config_from_nvs(char** ssid, char** password) {
  // Init nvs connection
  nvs_handle nvs_config_handle;
  ESP_ERROR_CHECK(nvs_open("conf", NVS_READONLY, &nvs_config_handle));

  // SSID
  size_t ssid_length;
  esp_err_t err = nvs_get_str(nvs_config_handle, "wifi_ssid", NULL, &ssid_length);

  bool ssid_found = false;
  if (err != ESP_ERR_NOT_FOUND) {
    ssid_found = true;
    *ssid = (char*) malloc(ssid_length);
    ESP_ERROR_CHECK(nvs_get_str(nvs_config_handle, "wifi_ssid", *ssid, &ssid_length));
  }
  else {
    *ssid = (char*) malloc(2);
    sprintf(*ssid, " ");
  }

  // PASSWORD
  size_t pw_length;
  err = nvs_get_str(nvs_config_handle, "wifi_pw", NULL, &pw_length);

  bool pw_found = false;
  if (err != ESP_ERR_NOT_FOUND) {
    pw_found = true;
    *password = (char*) malloc(ssid_length);
    ESP_ERROR_CHECK(nvs_get_str(nvs_config_handle, "wifi_pw", *password, &pw_length));
  }
  else {
    *password = (char*) malloc(2);
    sprintf(*password, " ");
  }
  nvs_close(nvs_config_handle);
  return ssid_found && pw_found;
}

esp_err_t main_wifi_event_handler(void *context, system_event_t *event)
{
    WifiContext* wifiContext = (WifiContext*) context;
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        wifiContext->connected=WIFI_STATUS_CONNECTED;
        ESP_LOGI(WIFI_TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        vTaskDelete(wifiContext->blinkLedTaskHandler);
        gpio_set_level((gpio_num_t) BLINK_GPIO, 0);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
          if (s_retry_num < MAXIMUM_RETRY) {
              esp_wifi_connect();
              xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
              s_retry_num++;
              ESP_LOGI(WIFI_TAG,"retry to connect to the AP");
          }
          else {
            wifiContext->connected=WIFI_STATUS_DISCONNECTED;
            vTaskDelete(wifiContext->blinkLedTaskHandler);
            gpio_set_level((gpio_num_t) BLINK_GPIO, 0);
            ESP_LOGI(WIFI_TAG,"Fail to connect to the AP. Check your connection parameters.\n");
          }
          break;
        }
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t test_wifi_event_handler(void *context, system_event_t *event)
{
    WifiContext* wifiContext = (WifiContext*) context;
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        wifiContext->connected=WIFI_STATUS_CONNECTED;
        ESP_LOGI(WIFI_TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(WIFI_TAG, "Connection successful!");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
          if (wifiContext->connected == WIFI_STATUS_WAITING){
            ESP_LOGI(WIFI_TAG,"Connection failed.\n");
            wifiContext->connected=WIFI_STATUS_DISCONNECTED;
          }
          vTaskDelete(wifiContext->blinkLedTaskHandler);
          gpio_set_level((gpio_num_t) BLINK_GPIO, 0);
          break;
        }
    default:
        break;
    }
    return ESP_OK;
}

WifiContext* wifi_init_sta(const char* ssid,const char* password, system_event_cb_t wifi_event_handler)
{
    /* BLINK LED */
    uint32_t delay_ms = 100;
    TaskHandle_t blinkLedTaskHandler;
    xTaskCreate(&blink_task, "blink_connect_wifi", configMINIMAL_STACK_SIZE, (void*)&delay_ms, 5, &blinkLedTaskHandler);
    s_wifi_event_group = xEventGroupCreate();

    /* Init WiFi driver */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Start WiFI events handler */
    tcpip_adapter_init();
    wifiContext.blinkLedTaskHandler = blinkLedTaskHandler;
    wifiContext.connected = WIFI_STATUS_WAITING;

    if (!loop_init) {
      ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, &wifiContext));
      loop_init = true;
    }

    /* Connect to WiFi */
    wifi_config_t wifi_config = { };
    strcpy((char*) wifi_config.sta.ssid, ssid);
    strcpy((char*) wifi_config.sta.password, password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(WIFI_TAG, "Wifi STA initialized.");
    ESP_LOGI(WIFI_TAG, "Try to connect with SSID=%s password=%s",
             ssid, password);

    return &wifiContext;
}

void clean_wifi() {
  esp_err_t err = esp_wifi_disconnect();
  if (err == ESP_ERR_WIFI_NOT_INIT){
    return;
  }
  if (err == ESP_ERR_WIFI_NOT_STARTED){
    esp_wifi_deinit();
    return;
  }
  esp_wifi_stop();
  esp_wifi_deinit();
}
