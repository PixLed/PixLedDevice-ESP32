#include "mqtt_config.h"
#include "main.h"

static esp_mqtt_client_handle_t client;
static bool client_initialized = false;

void save_mqtt_uri_to_nvs(const char* uri) {
  // Init NVS connection
  nvs_handle nvs_config_handle;
  ESP_ERROR_CHECK(nvs_open("conf", NVS_READWRITE, &nvs_config_handle));

  // Save adress
  ESP_ERROR_CHECK(nvs_set_str(nvs_config_handle, "mqtt_uri", uri));
  ESP_ERROR_CHECK(nvs_commit(nvs_config_handle));

  // Close NVS handler
  nvs_close(nvs_config_handle);
}

bool load_mqtt_uri_from_nvs(char** uri) {
  // Init nvs connection
  nvs_handle nvs_config_handle;
  ESP_ERROR_CHECK(nvs_open("conf", NVS_READONLY, &nvs_config_handle));

  // Load adress
  size_t uri_length;
  esp_err_t err = nvs_get_str(nvs_config_handle, "mqtt_uri", NULL, &uri_length);
  bool found = false;
  if (err != ESP_ERR_NOT_FOUND) {
    found = true;
    *uri = (char*) malloc(uri_length);
    ESP_ERROR_CHECK(nvs_get_str(nvs_config_handle, "mqtt_uri", *uri, &uri_length));
  }

  // Close NVS handler
  nvs_close(nvs_config_handle);

  return found;
}

esp_err_t test_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  esp_mqtt_client_handle_t client = event->client;
  int msg_id;
  struct mqtt_context* context = (mqtt_context*)(event->user_context);

  switch (event->event_id) {
      case MQTT_EVENT_CONNECTED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
          context->connected=1;
          break;
      case MQTT_EVENT_BEFORE_CONNECT:
          break;

      case MQTT_EVENT_DISCONNECTED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
          if (context->connected == -1){
            ESP_LOGI(MQTT_TAG,"MQTT connection test failed.\n");
            context->connected=0;
          }
          break;

      case MQTT_EVENT_SUBSCRIBED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED, topic=%.*s, msg_id=%d",event->topic_len, event->topic, event->msg_id);
          break;

      case MQTT_EVENT_UNSUBSCRIBED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
          break;

      case MQTT_EVENT_PUBLISHED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
          break;

      case MQTT_EVENT_ERROR:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
          break;

      case MQTT_EVENT_DATA:
          break;

  }
  return ESP_OK;
}

esp_err_t main_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  esp_mqtt_client_handle_t client = event->client;
  int msg_id;
  struct mqtt_context* context = (mqtt_context*)(event->user_context);
  // your_context_t *context = event->context;
  switch (event->event_id) {
      case MQTT_EVENT_CONNECTED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
          context->connected=1;
          // Publish device_id to /connected topic
          esp_mqtt_client_publish(client, connection_topic, device_id, 0, 1, 0);

          // (Re)initialize subscribtions
          context->subscribed_to_switch_topic=false;
          context->subscribed_to_color_topic=false;

          // Firstly, connect to switch topic, and wait for the first switch initialization message.
          // -> go to MQTT_EVENT_DATA
          ESP_LOGI(MQTT_TAG, "Subscribing to switch topic...");
          esp_mqtt_client_subscribe(client, switch_topic, 1);
          break;
      case MQTT_EVENT_BEFORE_CONNECT:
          break;

      case MQTT_EVENT_DISCONNECTED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
          context->connected=0;
          break;

      case MQTT_EVENT_SUBSCRIBED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED, topic=%.*s, msg_id=%d",event->topic_len, event->topic, event->msg_id);
          break;

      case MQTT_EVENT_UNSUBSCRIBED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
          break;

      case MQTT_EVENT_PUBLISHED:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
          break;

      case MQTT_EVENT_ERROR:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
          break;

      case MQTT_EVENT_DATA:
          ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DATA");
          printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
          printf("DATA=%.*s\r\n", event->data_len, event->data);
          size_t topic_len = event->topic_len;
          char topic_str[topic_len + 1];
          for (int i = 0; i < topic_len + 1; i++) {
            topic_str[i] = event->topic[i];
          }
          topic_str[topic_len] = '\0';

          if (strcmp(topic_str, check_topic) == 0) {
            esp_mqtt_client_publish(client, connection_topic, device_id, 0, 1, 0);
          }


          else if (strcmp(topic_str, switch_topic) == 0) {
            handle_switch(event->data_len, event->data);
            ESP_LOGI(MQTT_TAG, "subscribed_to_switch_topic : %s", context->subscribed_to_switch_topic ? "true" : "false");
            if(!context->subscribed_to_switch_topic) {
              // subscribed_to_switch_topic was false, so this is the initialization message.
              context->subscribed_to_switch_topic=true;

              // Then, we connect to color_topic
              ESP_LOGI(MQTT_TAG, "Subscribing to color topic...");
              esp_mqtt_client_subscribe(client, color_topic, 1);
            }
          }

          else if (strcmp(topic_str, color_topic) == 0) {
            handle_color_changed(event->data_len, event->data);
            if(!context->subscribed_to_color_topic){
              // subscribed_to_color_topic was false, so this is the initialization message.
              context->subscribed_to_color_topic=true;
              ESP_LOGI(MQTT_TAG, "Initialization complete!");
              esp_mqtt_client_subscribe(client, check_topic, 1);
            }
          }

          break;

  }
  return ESP_OK;
}

void mqtt_app_start(const char* broker_uri, mqtt_event_callback_t mqtt_event_handler)
{
  // Topics initialization
  ESP_LOGI(MQTT_TAG, "Init topics");
  int8_t id;
  load_id_from_nvs(&id);
  sprintf(device_id, "%i", id);
  sprintf(client_id, "light_%i", id);
  sprintf(color_topic, "/devices/%i/state/color", id);
  sprintf(switch_topic, "/buildings/%i/state/switch", id);

  // Blink on board led
  uint32_t delay_ms = 300;
  TaskHandle_t blinkLedTaskHandler;
  xTaskCreate(&blink_task, "blink_connect_mqtt", configMINIMAL_STACK_SIZE, (void*)&delay_ms, 5, &blinkLedTaskHandler);
  vTaskDelay(2000 / portTICK_PERIOD_MS);

  ESP_LOGI(MQTT_TAG, "Connecting to broker...");

  // Context initialization
  struct mqtt_context context { };
  context.connected = -1;
  context.subscribed_to_switch_topic = false;
  context.subscribed_to_color_topic = false;

  // Mqtt client initialization
  esp_mqtt_client_config_t mqtt_cfg = { };
  mqtt_cfg.uri = broker_uri;
  mqtt_cfg.event_handle = mqtt_event_handler;
  mqtt_cfg.user_context = (void*)&context;
  mqtt_cfg.client_id = client_id;
  mqtt_cfg.keepalive = 5;
  // If this light disconnect, devices subscribed to this topic (e.g. Spring
  // server and Android apps) will be warned that this module has disconnected.
  mqtt_cfg.lwt_topic = disconnection_topic;
  mqtt_cfg.lwt_msg = device_id;
  mqtt_cfg.lwt_qos = 1;
  mqtt_cfg.lwt_retain = 0;

  client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_start(client);
  client_initialized = true;

  ESP_LOGI(MQTT_TAG, "Try to connect to %s", broker_uri);
  while (context.connected == -1) {
    printf(".");
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  vTaskDelete(blinkLedTaskHandler);
  gpio_set_level((gpio_num_t) BLINK_GPIO, 0);
}

void clean_mqtt() {
  if (client_initialized) {
    ESP_ERROR_CHECK( esp_mqtt_client_destroy(client) );
    client_initialized = false;
  }
}
