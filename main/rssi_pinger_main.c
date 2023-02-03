/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "rssi_pinger.h"

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow_example";

static QueueHandle_t s_example_espnow_queue;

static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t device_mac[ESP_NOW_ETH_ALEN] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB };


typedef struct {
	unsigned frame_ctrl:16;
	unsigned duration_id:16;
	uint8_t addr1[6]; /* receiver address */
	uint8_t addr2[6]; /* sender address */
	uint8_t addr3[6]; /* filtering address */
	unsigned sequence_ctrl:16;
    uint8_t category_code;
    uint8_t organization_identifier[3];
    unsigned random_values:32;
} wifi_ieee80211_mac_hdr_t;


typedef struct {
	wifi_ieee80211_mac_hdr_t hdr;
	uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;


typedef struct {
    uint8_t element_id;
    uint8_t len;
    uint8_t organization_identifier[3];
    uint8_t type;
    uint8_t version;
    uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
}esp_now_hdr_t;


/**
 * 
 * 
A decompositions of ESP-NOW Communication, wifi_promiscuous_pkt_t.payload is as follows:
bytes: 		01   03   05   07   09   11   13   15   17   19   21   23   25   27   29   31   33   35   37   39   41   43   45   47
content: 	d000 0000 ffff ffff ffff 7cdf a101 f314 ffff ffff ffff a037 7f18 fe34 ac2b 9dd8 dd0b 18fe 3404 0101 2345 6789 ab00 0000

MAC HEADER
[1-2]: frame_ctrl = 0d00
[3-4]: duration_id = 0000
[5-10]: mac-destination = ffff ffff ffff
[11-16]: mac-source = 7cdf a101 f314
[17-22]: mac-broadcast = ffff ffff ffff
[23-24]: sequence ctrl
[25]: Category code: 7f (127)
[26-28]: Organization Identifier: 18 fe34
[29-32]: Random Values

VENDOR SPECIFIC CONTENT
[33]: Element ID: dd (221)
[34]: Length: 0b
[35-37]: Organization Identifier: 18 fe34
[38]: Type: 04 (means esp-now)
[39]: ESP-NOW Version: 01
[40+]: body.

 * 
 * 
*/
void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {

    // All espnow traffic uses action frames which are a subtype of the mgmnt frames so filter out everything else.

    static const uint8_t ACTION_SUBTYPE = 0xd0;
    static const uint8_t ESPRESSIF_OUI[] = {0x18, 0xfe, 0x34};
    static const uint8_t ESP_NOW_TYPE = 0x04;

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const esp_now_hdr_t *esp_now_hdr = (esp_now_hdr_t *)ipkt->payload;

    if (type != WIFI_PKT_MGMT)
        return;

    // // Only continue processing if this is an action frame containing the Espressif OUI.
    if ((ACTION_SUBTYPE == (ipkt->hdr.frame_ctrl & 0xFF)) &&
        (memcmp(ipkt->hdr.organization_identifier, ESPRESSIF_OUI, 3) == 0) &&
        (esp_now_hdr->type == ESP_NOW_TYPE)) {
        
        int rssi = ppkt->rx_ctrl.rssi;
        printf("%02x:%02x:%02x:%02x:%02x:%02x %i\n", 
               ipkt->hdr.addr2[0], ipkt->hdr.addr2[1], ipkt->hdr.addr2[2], ipkt->hdr.addr2[3], ipkt->hdr.addr2[4], ipkt->hdr.addr2[5],
               rssi);
     }
}



/* WiFi should start before using ESPNOW */
static void example_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );

    // set promiscuous for tracking packets.
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&promiscuous_rx_cb);

    ESP_ERROR_CHECK( esp_wifi_start());

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif


}


static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    return;
}


static void example_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    return;
}


static void example_espnow_task(void *pvParameter)
{
    ESP_LOGI(TAG, "starting sending");

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 10/portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(1){
        vTaskDelayUntil( &xLastWakeTime, xFrequency );
    
        if (esp_now_send(broadcast_mac, device_mac, ESP_NOW_ETH_ALEN) != ESP_OK){
            ESP_LOGE(TAG, "Send error");
        }
        else {
            // ESP_LOGI(TAG, "sent data");
        }
    }
}


static esp_err_t example_espnow_init(void)
{
    
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );
#if CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE
    ESP_ERROR_CHECK( esp_now_set_wake_window(65535) );
#endif

    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    xTaskCreate(example_espnow_task, "example_espnow_task", 2048, NULL, 4, NULL);

    return ESP_OK;
}


void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    example_wifi_init();
    example_espnow_init();
}
