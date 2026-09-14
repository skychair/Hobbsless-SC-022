/**
 * Hobbsless Main App
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */


#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "defs.h"
#include "util.h"
/* EE */
#include "ee.h"
/* BLE CC */
#include "ble_cc.h"
/* Wi-Fi */
#include "wifi.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_spp_server.h"
#include "driver/uart.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
/* SNTP */
#include "ntp.h"
/* LED */
#include "led.h"
/* Accelerometer */
#include "lis3dh.h"
#include "fft.h"
#include "driver/rtc_io.h"
#include "http_upload.h"
#include "ota.h"

#define RTC_PIN_BITMASK     (1ULL << LIS3DH_INT_PIN)

/*                  */
/*                  */
/* SECTION: GLOBALS */
/*                  */
/*                  */

uint8_t                   ssid[33];
uint8_t                   psk[65];
RTC_DATA_ATTR uint16_t    pub_freq; //hours (deep sleep this long then wake and connect wifi/send if new data)
RTC_DATA_ATTR uint16_t    ble_listen_duration; //seconds to listen for BLE connection after first boot
RTC_DATA_ATTR uint32_t    next_pub; //epoch (sec) of next publish, used to set the nighttime wake

/* Engine monitoring state - preserved across deep sleep during active monitoring */
RTC_DATA_ATTR uint16_t    engine_mon_interval;  // minutes between monitoring checks
RTC_DATA_ATTR uint16_t    engine_detect_samples; // FFT samples taken to decide if the engine is running
RTC_DATA_ATTR uint8_t     engine_monitoring;    // 1 = actively monitoring a running engine
RTC_DATA_ATTR uint32_t    engine_start_time;    // epoch when engine was first detected
RTC_DATA_ATTR float       engine_sum_freq;      // running sum of dominant freq across all FFT calls
RTC_DATA_ATTR float       engine_peak_mag;      // peak magnitude seen across all FFT calls
RTC_DATA_ATTR float       engine_sum_mag;       // running sum of magnitude across all FFT calls
RTC_DATA_ATTR uint32_t    engine_fft_calls;     // total FFT calls made this session
uint8_t                   mac_uuid[6];
char                      mac_uuid_str[13];
char                      serial_str[7];        // last 6 hex chars of the MAC

static char               L_TAG[] = "MAIN";

static uint8_t            own_addr_type;
QueueHandle_t             spp_common_uart_queue = NULL;
static bool               conn_handle_subs[CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1];
static uint16_t           ble_spp_svc_gatt_read_val_handle;
static bool               client_connected = false;

char                      out[128];

int8_t                   wifi_is_connected;

/* Semaphore to signal BLE stop */
static SemaphoreHandle_t  ble_stop_semaphore = NULL;

/* Timer handle for restarting timer on disconnect */
static TimerHandle_t      ble_stop_timer = NULL;

/* BLE advertising indicator blink task handle */
static TaskHandle_t       ble_blink_task_handle = NULL;

/* Which LED the advertising indicator blinks, and how often.
 * Defaults to BLU every 10s; unconfigured boot switches it to RED every 4s. */
static uint8_t            indicator_led = BLU;
static uint32_t           indicator_period_ms = 5000;


/*            */
/*            */
/* Prototypes */
/*            */
/*            */

static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg);
int gatt_svr_register(void);
void ble_store_config_init(void);
static void go_to_sleep();
static void before_sleep();
static void stop_ble_indicator_blink(void);

/*              */
/*              */
/* SECTION: BLE */
/*              */
/*              */

/**
 * Logs information about a connection to the console.
 */
static void
ble_spp_server_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    const uint8_t *addr;
    addr = desc->our_ota_addr.val;
    ESP_LOGI(L_TAG, "handle=%d our_ota_addr_type=%d our_ota_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->conn_handle, desc->our_ota_addr.type,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    //print_addr(desc->our_ota_addr.val);

    addr = desc->our_id_addr.val;
    ESP_LOGI(L_TAG, " our_id_addr_type=%d our_id_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->our_id_addr.type,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    //print_addr(desc->our_id_addr.val);

    addr = desc->peer_ota_addr.val;
    ESP_LOGI(L_TAG, " peer_ota_addr_type=%d peer_ota_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->peer_ota_addr.type,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    //print_addr(desc->peer_ota_addr.val);


    addr = desc->peer_id_addr.val;
    ESP_LOGI(L_TAG, " peer_id_addr_type=%d peer_id_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->peer_id_addr.type,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    //print_addr(desc->peer_id_addr.val);

    ESP_LOGI(L_TAG, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
ble_spp_server_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name;
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(BLE_SVC_SPP_UUID16)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(L_TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_spp_server_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(L_TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * ble_spp_server uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  ble_spp_server.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
ble_spp_server_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ESP_LOGI(L_TAG, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            ble_spp_server_print_conn_desc(&desc);

            /* Mark that a client is connected */
            client_connected = true;
            ESP_LOGI(L_TAG, "Client connected - BLE will remain active");

            /* Stop the advertising indicator blink */
            stop_ble_indicator_blink();
        }
        if (event->connect.status != 0 || CONFIG_BT_NIMBLE_MAX_CONNECTIONS > 1) {
            /* Connection failed or if multiple connection allowed; resume advertising. */
            ble_spp_server_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(L_TAG, "disconnect; reason=%d ", event->disconnect.reason);
        ble_spp_server_print_conn_desc(&event->disconnect.conn);

        conn_handle_subs[event->disconnect.conn.conn_handle] = false;

        /* Mark that client is disconnected */
        client_connected = false;

        /* Reboot to load new settings from NVS */
        ESP_LOGI(L_TAG, "Client disconnected - rebooting to apply new settings");
        reboot(2);

        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(L_TAG, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        ble_spp_server_print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(L_TAG, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        ble_spp_server_advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(L_TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(L_TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        conn_handle_subs[event->subscribe.conn_handle] = true;
        return 0;

    default:
        return 0;
    }
}

static void
ble_spp_server_on_reset(int reason)
{
    ESP_LOGE(L_TAG, "Resetting state; reason=%d", reason);
}

static void
ble_spp_server_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(L_TAG, "error determining address type; rc=%d", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    ESP_LOGI(L_TAG, "Device Address: %02x:%02x:%02x:%02x:%02x:%02x", 
                  addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);

    /* Name the device after serial_str so the advertised name matches the UUID
     * printed at boot and sent to the server. Do not derive it from addr_val:
     * the BLE address is the base MAC + 2, so its low bytes differ from the
     * WiFi STA MAC the UUID comes from. */
    char device_name[32];
    snprintf(device_name, sizeof(device_name), "Hobbsless-%s", serial_str);
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGE(L_TAG, "error setting device name; rc=%d", rc);
    } else {
        ESP_LOGI(L_TAG, "Device name set to: %s", device_name);
    }

    /* Begin advertising. */
    ble_spp_server_advertise();
}

void ble_spp_server_host_task(void *param)
{
    ESP_LOGI(L_TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

/* Callback function for custom service */
static int  ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(L_TAG, "Callback for read");
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ctxt->om) {
            uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
            char *data_str = malloc(data_len + 1);
            uint8_t ack = 0;  // Default to failure

            bool is_get_cmd = false;

            if (data_str) {
                os_mbuf_copydata(ctxt->om, 0, data_len, data_str);
                data_str[data_len] = '\0';  // Null terminate the string
                ESP_LOGI(L_TAG, "Data received in write event,conn_handle = %x,attr_handle = %x, data: %s", conn_handle, attr_handle, data_str);

                /* Check if this is a get command (starts with 'g|') */
                if (data_len >= 2 && data_str[0] == 'g' && data_str[1] == '|') {
                    is_get_cmd = true;
                }

                /* Parse and execute BLE command */
                ack = parse_from_ble(data_str, conn_handle, ble_spp_svc_gatt_read_val_handle);

                free(data_str);
            } else {
                ESP_LOGI(L_TAG, "Data received in write event,conn_handle = %x,attr_handle = %x (failed to allocate memory)", conn_handle, attr_handle);
            }

            /* Send ACK back to client (only for non-get commands) */
            /* Get commands already sent their response data */
            if (!is_get_cmd) {
                struct os_mbuf *txom = ble_hs_mbuf_from_flat(&ack, 1);
                if (txom) {
                    int rc = ble_gatts_notify_custom(conn_handle, ble_spp_svc_gatt_read_val_handle, txom);
                    if (rc != 0) {
                        ESP_LOGI(L_TAG, "Failed to send ACK; rc=%d", rc);
                    }
                }
            }
        } else {
            ESP_LOGI(L_TAG, "Data received in write event,conn_handle = %x,attr_handle = %x (no data)", conn_handle, attr_handle);
        }
        break;

    default:
        ESP_LOGI(L_TAG, "Default Callback");
        break;
    }
    return 0;
}

/* Define new custom service */
static const struct ble_gatt_svc_def new_ble_svc_gatt_defs[] = {
    {
        /*** Service: SPP */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[])
        { {
                /* Support SPP service */
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_CHR_UUID16),
                .access_cb = ble_svc_gatt_handler,
                .val_handle = &ble_spp_svc_gatt_read_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            }, {
                0, /* No more characteristics */
            }
        },
    },
    {
        0, /* No more services. */
    },
};

static void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(L_TAG, "registered service %s with handle=%d",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(L_TAG, "registering characteristic %s with "
                    "def_handle=%d val_handle=%d",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(L_TAG, "registering descriptor %s with handle=%d",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int gatt_svr_init(void)
{
    int rc = 0;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(new_ble_svc_gatt_defs);

    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(new_ble_svc_gatt_defs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}


void ble_server_uart_task(void *pvParameters)
{
    ESP_LOGI(L_TAG, "BLE server UART_task started");
    uart_event_t event;
    int rc = 0;
    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(spp_common_uart_queue, (void * )&event, (TickType_t)portMAX_DELAY))            {
            switch (event.type) {
            //Event of UART receiving data
            case UART_DATA:
                if (event.size) {
                    uint8_t *ntf;
                    ntf = (uint8_t *)malloc(sizeof(uint8_t) * event.size);
                    memset(ntf, 0x00, event.size);
                    uart_read_bytes(UART_NUM_0, ntf, event.size, portMAX_DELAY);

                    for (int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
                        /* Check if client has subscribed to notifications */
                        if (conn_handle_subs[i]) {
                            struct os_mbuf *txom;
                            txom = ble_hs_mbuf_from_flat(ntf, event.size);
                            rc = ble_gatts_notify_custom(i, ble_spp_svc_gatt_read_val_handle,
                                                         txom);
                            if (rc == 0) {
                                ESP_LOGI(L_TAG, "Notification sent successfully");
                            } else {
                                ESP_LOGI(L_TAG, "Error in sending notification rc = %d", rc);
                            }
                        }
                    }

		    free(ntf);
                }
                break;
            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}
static void ble_spp_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_RTS,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    //Install UART driver, and get the queue.
    uart_driver_install(UART_NUM_0, 4096, 8192, 10, &spp_common_uart_queue, 0);
    //Set UART parameters
    uart_param_config(UART_NUM_0, &uart_config);
    //Set UART pins
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(ble_server_uart_task, "uTask", 4096, (void *)UART_NUM_0, 8, NULL);
}

/**
 * Put device in deep sleep
 */
static void go_to_sleep() {
    before_sleep();

    /**
     *
     * to determine how long to sleep, use now (get_epoch_time) - pub_time.
     * pub_time is reset after each publish.
     * if wake from pin interrupt (motion) we don't want to restart the 
     * multi-hour sleep as occasional motion will prevent publish from ever happening.
     *
     */

    uint32_t now = get_epoch_time();
    uint32_t sleep_for;
    if (next_pub == 0 || next_pub <= now) {
        // next_pub not set or already past - fall back to pub_freq hours
        sleep_for = (uint32_t)pub_freq * 60 * 60;
        ESP_LOGW(L_TAG, "next_pub invalid (next_pub=%lu now=%lu), sleeping for pub_freq=%u hours",
                 (unsigned long)next_pub, (unsigned long)now, pub_freq);
    } else {
        sleep_for = next_pub - now;
    }

    ESP_LOGI(L_TAG, "Going to deep sleep for %lu seconds or until motion wakes me up.", (unsigned long)sleep_for);

    // Configure timer wakeup
    //esp_sleep_enable_timer_wakeup(30000000); // 30 seconds, debug
    esp_sleep_enable_timer_wakeup(sleep_for * 1000000ULL); // seconds to microseconds

    // Force deep sleep without subsystem checks
    esp_deep_sleep_start();
}

/**
 * Task to handle BLE shutdown
 * Waits for signal and then stops BLE properly
 */
static void ble_shutdown_task(void *pvParameters)
{
    ESP_LOGI(L_TAG, "BLE shutdown task started");

    /* Wait for semaphore signal to stop BLE */
    if (xSemaphoreTake(ble_stop_semaphore, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(L_TAG, "%d seconds elapsed - stopping BLE", ble_listen_duration);

        /* Stop advertising first */
        int rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGE(L_TAG, "Failed to stop advertising; rc=%d", rc);
        }

        /* Stop the NimBLE port */
        rc = nimble_port_stop();
        if (rc != 0) {
            ESP_LOGE(L_TAG, "Failed to stop nimble port; rc=%d", rc);
        } else {
            ESP_LOGI(L_TAG, "BLE stopped successfully");
        }

        /* Stop the advertising indicator blink */
        stop_ble_indicator_blink();

        /* deep sleep here */
        go_to_sleep();
    }

    /* Task can loop or delete itself */
    vTaskDelete(NULL);
}

/**
 * Timer callback to signal BLE stop
 */
static void ble_auto_stop_timer_callback(TimerHandle_t xTimer)
{
    /* Signal shutdown if no client is connected */
    if (!client_connected && ble_stop_semaphore != NULL) {
        xSemaphoreGive(ble_stop_semaphore);
    }
    /* If client is connected, do nothing - timer will be restarted on disconnect */
}


/* Blink indicator_led briefly every indicator_period_ms to show BLE is
 * advertising and connectable */
static void ble_indicator_blink_task(void *pvParameters)
{
    for (;;) {
        led_set(indicator_led, OFF);
        vTaskDelay(pdMS_TO_TICKS(80));
        led_set(indicator_led, ON);
        vTaskDelay(pdMS_TO_TICKS(indicator_period_ms - 80));
    }
}

/* Stop the advertising indicator blink and leave its LED off */
static void stop_ble_indicator_blink(void)
{
    if (ble_blink_task_handle != NULL) {
        vTaskDelete(ble_blink_task_handle);
        ble_blink_task_handle = NULL;
        led_set(indicator_led, ON);  // active low - ensure LED is off
    }
}

/* Start BLE advertising with an auto-stop timer of duration_secs seconds.
 * duration_secs == 0 means advertise forever - no auto-stop, no deep sleep.
 * Caller must have ee_open()'d before calling. BLE shutdown task calls go_to_sleep(). */
static void start_ble_listen(uint16_t duration_secs)
{
    int rc;
    esp_err_t ret;

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "Failed to init nimble %d", ret);
        return;
    }

    for (int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
        conn_handle_subs[i] = false;
    }

    ble_spp_uart_init();

    ble_hs_cfg.reset_cb = ble_spp_server_on_reset;
    ble_hs_cfg.sync_cb = ble_spp_server_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    rc = gatt_svr_init();
    assert(rc == 0);

    ble_store_config_init();
    nimble_port_freertos_init(ble_spp_server_host_task);

    ble_stop_semaphore = xSemaphoreCreateBinary();
    if (ble_stop_semaphore == NULL) {
        ESP_LOGE(L_TAG, "Failed to create BLE stop semaphore");
        return;
    }

    BaseType_t task_created = xTaskCreate(
        ble_shutdown_task, "BLE_Shutdown", 4096, NULL, 5, NULL
    );
    if (task_created != pdPASS) {
        ESP_LOGE(L_TAG, "Failed to create BLE shutdown task");
        return;
    }

    if (duration_secs == 0) {
        /* Unconfigured - stay advertising until the user configures us */
        ESP_LOGI(L_TAG, "BLE listen started - no auto-stop, staying awake");
    } else {
        ble_stop_timer = xTimerCreate(
            "BLE_AutoStop",
            pdMS_TO_TICKS((uint32_t)duration_secs * 1000),
            pdFALSE,
            NULL,
            ble_auto_stop_timer_callback
        );
        if (ble_stop_timer != NULL) {
            if (xTimerStart(ble_stop_timer, 0) == pdPASS) {
                ESP_LOGI(L_TAG, "BLE listen started - auto-stop in %d seconds", duration_secs);
            } else {
                ESP_LOGE(L_TAG, "Failed to start BLE auto-stop timer");
            }
        } else {
            ESP_LOGE(L_TAG, "Failed to create BLE auto-stop timer");
        }
    }

    xTaskCreate(ble_indicator_blink_task, "BLE_Blink", 1024, NULL, 3, &ble_blink_task_handle);
}

static void do_upload(const char *serial_str);      /* forward declaration */
static void woke_by_engine_monitor(void);           /* forward declaration */

/* Deep sleep for one monitoring interval - timer wakeup only.
 * EXT1 is intentionally omitted: the engine is running so the accelerometer
 * would fire immediately and prevent sleep. */
static void go_to_sleep_monitoring() {
    ESP_LOGI(L_TAG, "Engine still running - sleeping %d min before next check",
             engine_mon_interval);
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_sleep_enable_timer_wakeup((uint64_t)engine_mon_interval * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void fresh_boot() {
    engine_monitoring = 0;  /* clear any stale monitoring state from unexpected reset */

    /* Initialize and blink LEDs */
    led_init();
    led_boot_sequence();

    /* Initialize accelerometer */
    if (lis3dh_init() == ESP_OK) {
        lis3dh_config_interrupt();
        int16_t x, y, z;
        if (lis3dh_read_accel(&x, &y, &z) == ESP_OK) {
            float gx = (x >> 4) / 1000.0f;
            float gy = (y >> 4) / 1000.0f;
            float gz = (z >> 4) / 1000.0f;
            ESP_LOGI(L_TAG, "Accel X:%.2fg Y:%.2fg Z:%.2fg", gx, gy, gz);
        }
    }

    ESP_LOGI(L_TAG, "\n\nHobbsless SC-022\nApp Version: %s\nUUID: %s\n", fw_version(), serial_str);


    /* Initialize NVS - it is used to store PHY calibration data */
    ee_open();
    // check saved value of ser_debug, enable if saved on
    uint8_t ser_debug = ON;
    //uint8_t ser_debug = ee_get_ser_debug();
    if (ser_debug == ON ) {
      esp_log_level_set("*", ESP_LOG_DEBUG);
      ESP_LOGI(L_TAG, "ser_debug enabled from ee at boot");
    }  else {
      esp_log_level_set("*", ESP_LOG_INFO);
      ESP_LOGW(L_TAG, "ser_debug disabled from ee at boot");
    }

    /* Set defaults in EE */
    //ee_make_virgin();
    ee_init_if_needed();


    /* Set globals from ee */
    pub_freq = ee_get_pub_freq();
    ble_listen_duration = ee_get_ble_listen_duration();
    engine_mon_interval = ee_get_engine_mon_interval();
    engine_detect_samples = ee_get_engine_detect_samples();

    /* check for saved wifi creds */
    uint8_t creds_exist = ee_check_for_saved_wifi_creds();

    /* Unconfigured device: blink RED every 4s and advertise forever. Do NOT
     * sleep - the user has to configure us over BLE before we're of any use.
     * Disconnect reboots, which re-runs this check with the new settings. */
    if (creds_exist == NO) {
        ESP_LOGW(L_TAG, "No WiFi credentials saved - entering unconfigured mode "
                        "(RED blink every 4s, BLE advertising, no deep sleep)");
        indicator_led = RED;
        indicator_period_ms = 4000;
        start_ble_listen(0);
        return;
    }

    uint8_t upload_queued = ee_get_upload_queued();
    uint8_t ota_queued = ee_get_ota_queued();
    uint8_t need_wifi = (is_time_valid() == NO || upload_queued == ON || ota_queued == ON);

    if (need_wifi) {
      wifi_start(ssid, psk);
      wifi_connect();

      if (wifi_is_connected) {
        if (is_time_valid() == NO) {
          sync_sntp_time();
        }
        if (is_time_valid() == NO) {
          ESP_LOGI(L_TAG, "time is not valid, cannot continue, reboot/retry");
          reboot(5);
        }

        if (upload_queued == ON) {
          do_upload(serial_str);
          ee_set_upload_queued(OFF);
        }

        /* Clear the request before acting on it so a repeatedly failing
         * update cannot pin the device in a connect-and-retry loop. */
        if (ota_queued == ON) {
          ee_set_ota_queued(OFF);
        }
        ota_check_and_update();
      } else {
        ESP_LOGI(L_TAG, "Skipping NTP/upload, no wifi connection");
      }

      wifi_stop();
    }

    /* Start the publish schedule from this boot, whether or not we went near
     * WiFi. RTC_DATA_ATTR is reloaded from the image on every boot except a
     * deep sleep wake, so next_pub is 0 here even when the RTC clock survived
     * the reset (esp_restart, OTA reboot, BLE-disconnect reboot) - in that
     * case need_wifi is false and nothing else would set it. Done after any
     * upload so a pubFreq pushed back by the server takes effect now. */
    if (is_time_valid()) {
      uint32_t now = get_epoch_time();
      next_pub = now + ((uint32_t)pub_freq * 60 * 60);
      ESP_LOGI(L_TAG, "now: %lu next_pub: %lu diff: %lu",
               (unsigned long)now, (unsigned long)next_pub,
               (unsigned long)(next_pub - now));
    }

    start_ble_listen(ble_listen_duration);
}


/**
 * Maintenance tasks before sleep
 */
void before_sleep() {
    ESP_LOGI(L_TAG, "before_sleep():: configure EXT1 wakeup");

    // Ensure UART finishes transmitting before sleep
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);

    // Shutdown WiFi stack before deep sleep
    if (wifi_is_connected != -1) {
      wifi_shutdown();
    }

    // Enable EXT1 wakeup
    esp_sleep_enable_ext1_wakeup(RTC_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
}

/* Inner upload logic - assumes WiFi is already connected and NVS is open.
 * Reads pending runs, POSTs them, handles server commands. */
static void do_upload(const char *serial_str) {
    engine_run_t runs[EE_MAX_ENGINE_RUNS];
    uint8_t count = 0;
    ee_get_engine_runs(runs, &count);

    if (count == 0) {
        ESP_LOGI(L_TAG, "do_upload: no runs to upload");
        return;
    }

    ESP_LOGI(L_TAG, "do_upload: uploading %d run(s)", count);
    upload_response_t resp = { .pub_freq_hours = -1 };
    esp_err_t err = http_upload_runtime(serial_str, fw_version(), runs, count, &resp);

    if (err == ESP_OK) {
        ESP_LOGI(L_TAG, "do_upload: success, clearing NVS runs");
        ee_clear_engine_runs();

        if (resp.pub_freq_hours > 0) {
            ESP_LOGI(L_TAG, "Server command: set pub_freq to %d hours", resp.pub_freq_hours);
            ee_set_pub_freq((uint16_t)resp.pub_freq_hours);
            pub_freq = (uint16_t)resp.pub_freq_hours;
        }
    } else {
        ESP_LOGE(L_TAG, "do_upload: upload failed, runs kept in NVS");
    }
}

/* Shared upload helper - connects WiFi, calls do_upload(), disconnects.
 * Assumes NVS is already open. Does not go to sleep. */
static void try_upload_runs(const char *serial_str) {
    uint8_t creds_exist = ee_check_for_saved_wifi_creds();
    if (creds_exist == NO) {
        ESP_LOGW(L_TAG, "try_upload_runs: no WiFi creds, skipping");
        return;
    }

    wifi_start(ssid, psk);
    wifi_connect();

    if (!wifi_is_connected) {
        ESP_LOGE(L_TAG, "try_upload_runs: WiFi failed, skipping upload");
        wifi_stop();
        return;
    }

    if (is_time_valid() == NO) {
        sync_sntp_time();
    }

    do_upload(serial_str);

    /* Data is away; safe to reboot into a new image if one is waiting */
    ota_check_and_update();

    wifi_stop();
}


void woke_by_motion() {
    ESP_LOGI(L_TAG, "woke_by_motion()::");
    log_current_time();

    /* Init LEDs (not initialized on wakeup paths) */
    led_init();

    /* Always clear the LIS3DH interrupt first - prevents immediate re-wakeup */
    if (lis3dh_init() != ESP_OK) {
        ESP_LOGE(L_TAG, "woke_by_motion: lis3dh_init failed");
        go_to_sleep();
        return;
    }
    lis3dh_clear_interrupt();
    lis3dh_config_interrupt();

    /* If time is not valid, open a short BLE config window so the user can
     * set WiFi credentials, then the next fresh boot will sync NTP. */
    if (is_time_valid() == NO) {
        ESP_LOGI(L_TAG, "woke_by_motion: time not valid, blinking NO TIME then starting 30s BLE config window");
        led_morse_no_time();
        ee_open();
        ee_init_if_needed();
        start_ble_listen(30);
        return;  // BLE shutdown task will call go_to_sleep()
    }

    fft_init();

    /* Accumulators for vibration profile across the full session */
    float    vib_sum_freq  = 0.0f;
    float    vib_peak_mag  = 0.0f;
    float    vib_sum_mag   = 0.0f;
    uint32_t vib_fft_calls = 0;

    /* Phase 1: Initial detection - engine_detect_samples samples, 1s apart.
     * Guard against a zeroed RTC value so detection can never be skipped. */
    uint16_t detect_samples = (engine_detect_samples >= MIN_ENGINE_DETECT_SAMPLES &&
                               engine_detect_samples <= MAX_ENGINE_DETECT_SAMPLES)
                              ? engine_detect_samples : EE_DEFAULT_ENGINE_DETECT_SAMPLES;

    int above_threshold = 0;
    for (int i = 0; i < detect_samples; i++) {
        ESP_LOGI(L_TAG, "Detection sample %d/%d", i + 1, detect_samples);
        led_blink(GRN, 80);   /* one green blink per FFT sample */
        if (fft_collect_samples() == ESP_OK) {
            fft_result_t result;
            fft_compute(&result);
            ESP_LOGI(L_TAG, "Vibration: %.1fHz (mag=%.4f, energy=%.4f)",
                     result.peak_freq_hz, result.peak_magnitude, result.total_energy);
            if (result.peak_freq_hz > ENGINE_FREQ_THRESHOLD_HZ) {
                above_threshold++;
                vib_sum_freq += result.peak_freq_hz;
                if (result.peak_magnitude > vib_peak_mag) vib_peak_mag = result.peak_magnitude;
                vib_sum_mag  += result.peak_magnitude;
                vib_fft_calls++;
            }
        }
        if (i < detect_samples - 1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* Strict majority of samples must be above the threshold */
    int detect_min = detect_samples / 2;

    ESP_LOGI(L_TAG, "Detection: %d/%d samples above %.0fHz threshold (need >%d)",
             above_threshold, detect_samples, ENGINE_FREQ_THRESHOLD_HZ, detect_min);

    if (above_threshold <= detect_min) {
        ESP_LOGI(L_TAG, "woke_by_motion: transient shock detected, going back to sleep");
        go_to_sleep();
        return;
    }

    /* Phase 2: Engine detected - save state to RTC and deep sleep for monitoring interval.
     * woke_by_engine_monitor() handles each subsequent check. */
    engine_monitoring  = 1;
    engine_start_time  = get_epoch_time();
    engine_sum_freq    = vib_sum_freq;
    engine_peak_mag    = vib_peak_mag;
    engine_sum_mag     = vib_sum_mag;
    engine_fft_calls   = vib_fft_calls;

    ESP_LOGI(L_TAG, "Engine detected at %lu - sleeping before first monitor check",
             (unsigned long)engine_start_time);
    go_to_sleep_monitoring();
}

static void woke_by_engine_monitor(void) {
    ESP_LOGI(L_TAG, "woke_by_engine_monitor():: checking engine, start=%lu",
             (unsigned long)engine_start_time);
    log_current_time();
    led_init();

    if (lis3dh_init() != ESP_OK) {
        ESP_LOGE(L_TAG, "woke_by_engine_monitor: lis3dh_init failed - stopping monitor");
        goto engine_stopped;
    }

    fft_init();

    int running_count = 0;
    for (int i = 0; i < ENGINE_MONITOR_SAMPLES; i++) {
        ESP_LOGI(L_TAG, "Monitor sample %d/%d", i + 1, ENGINE_MONITOR_SAMPLES);
        led_blink(GRN, 80);   /* one green blink per FFT sample */
        if (fft_collect_samples() == ESP_OK) {
            fft_result_t result;
            fft_compute(&result);
            ESP_LOGI(L_TAG, "Vibration: %.1fHz (mag=%.4f, energy=%.4f)",
                     result.peak_freq_hz, result.peak_magnitude, result.total_energy);
            engine_sum_freq += result.peak_freq_hz;
            if (result.peak_magnitude > engine_peak_mag) engine_peak_mag = result.peak_magnitude;
            engine_sum_mag  += result.peak_magnitude;
            engine_fft_calls++;
            if (result.peak_freq_hz > ENGINE_FREQ_THRESHOLD_HZ) {
                running_count++;
            }
        }
        if (i < ENGINE_MONITOR_SAMPLES - 1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(L_TAG, "Monitor: %d/%d samples above threshold", running_count, ENGINE_MONITOR_SAMPLES);

    if (running_count >= ENGINE_MONITOR_MIN) {
        go_to_sleep_monitoring();
        return;
    }

engine_stopped: ;
    engine_monitoring = 0;

    uint32_t end_time = get_epoch_time();
    float avg_freq = (engine_fft_calls > 0) ? (engine_sum_freq / engine_fft_calls) : 0.0f;
    float avg_mag  = (engine_fft_calls > 0) ? (engine_sum_mag  / engine_fft_calls) : 0.0f;
    ESP_LOGI(L_TAG, "Engine stopped, end=%lu avg_freq=%.1fHz peak_mag=%.3fg",
             (unsigned long)end_time, avg_freq, engine_peak_mag);

    ee_open();
    ee_save_engine_run(engine_start_time, end_time, avg_freq, engine_peak_mag, avg_mag,
                       engine_fft_calls * 256);

    if (ee_get_post_flight_upload() == ON) {
        try_upload_runs(serial_str);
    }

    go_to_sleep();
}


void woke_by_timer() {
    ESP_LOGI(L_TAG, "woke_by_timer()::");
    log_current_time();

    ee_open();
    ee_init_if_needed();

    try_upload_runs(serial_str);

    if (is_time_valid()) {
        uint32_t now = get_epoch_time();
        next_pub = now + (pub_freq * 60 * 60);
        ESP_LOGI(L_TAG, "now: %lu next_pub: %lu diff: %lu",
                 (unsigned long)now, (unsigned long)next_pub,
                 (unsigned long)(next_pub - now));
    } else {
        ESP_LOGW(L_TAG, "Time not valid after wakeup, rebooting");
        reboot(5);
    }

    go_to_sleep();
}

/**
 * Main
 */
void
app_main(void)
{

    wifi_is_connected = -1; // tri-state, -1 == never init'd

    /* Identify ourselves on every boot. MAC is not preserved across deep sleep,
     * so this has to be re-read on each wake path. */
    get_mac_uuid(mac_uuid, mac_uuid_str);
    snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X",
             mac_uuid[3], mac_uuid[4], mac_uuid[5]);
    ESP_LOGI(L_TAG, "UUID: %s", serial_str);

    /* Confirm this image if it is a freshly installed OTA on probation. We do
     * it here, on reaching app_main, so a build that crashes during early boot
     * is rolled back by the bootloader instead of looping. */
    ota_mark_valid();

    // Check wake cause and branch, either fresh boot or periodic/reactive wake
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT1:
            ESP_LOGI(L_TAG, "Wakeup caused by external signal using RTC_CNTL");
            woke_by_motion();
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            if (engine_monitoring) {
                ESP_LOGI(L_TAG, "Wakeup caused by timer (engine monitoring)");
                woke_by_engine_monitor();
            } else {
                ESP_LOGI(L_TAG, "Wakeup caused by timer");
                woke_by_timer();
            }
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(L_TAG, "Wakeup caused by external signal using RTC_IO");
            //nop
            go_to_sleep();
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            ESP_LOGI(L_TAG, "Wakeup caused by touchpad");
            //nop
            go_to_sleep();
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            ESP_LOGI(L_TAG, "Wakeup caused by ULP program");
            //nop
            go_to_sleep();
            break;
        default:
            ESP_LOGI(L_TAG, "Wakeup was not caused by deep sleep (likely a fresh Power-on or Reset)");
            fresh_boot();
            break;
    }
}
