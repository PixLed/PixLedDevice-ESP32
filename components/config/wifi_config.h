#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASS
#define MAXIMUM_RETRY  10
#define MAIN_WIFI_EVENT_HANDLER main_wifi_event_handler
#define TEST_WIFI_EVENT_HANDLER test_wifi_event_handler

#define WIFI_STATUS_WAITING -1
#define WIFI_STATUS_DISCONNECTED 0
#define WIFI_STATUS_CONNECTED 1

#include <string.h>
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event_loop.h"

#define WIFI_TAG "WIFI"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

struct WifiContext {
  volatile TaskHandle_t blinkLedTaskHandler;
  volatile int connected;
};

esp_err_t main_wifi_event_handler(void *blinkLedTaskHandler, system_event_t *event);
esp_err_t test_wifi_event_handler(void *blinkLedTaskHandler, system_event_t *event);

WifiContext* wifi_init_sta(const char* ssid, const char* password, system_event_cb_t wifi_event_handler);
void clean_wifi();
void save_wifi_info_to_nvs(const char* ssid, const char* password);
bool load_wifi_config_from_nvs(char** ssid, char** password);
