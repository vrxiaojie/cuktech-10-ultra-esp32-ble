#include "cuktech_ble.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cuktech_ble_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

#define BLE_CONTROL_QUEUE_DEPTH 32U
#define BLE_NOTIFY_QUEUE_DEPTH 12U
#define BLE_NOTIFY_MAX_PAYLOAD 244U
#define BLE_MAX_DISCOVERED_CHARACTERISTICS 24U
#define BLE_SCAN_DURATION_MS 5000
#define BLE_CONNECT_TIMEOUT_MS 10000
#define BLE_GATT_TIMEOUT_MS 10000
#define BLE_DISCONNECT_TIMEOUT_MS 3000
#define BLE_HOST_SYNC_TIMEOUT_MS 15000
#define BLE_RETRY_MAX_SECONDS 300U
#define BLE_APP_TASK_STACK_SIZE 6144U
#define BLE_APP_TASK_PRIORITY 5U

typedef enum {
    CONTROL_EVENT_HOST_SYNC = 0,
    CONTROL_EVENT_HOST_RESET,
    CONTROL_EVENT_ADV_MATCH,
    CONTROL_EVENT_SCAN_COMPLETE,
    CONTROL_EVENT_CONNECT_RESULT,
    CONTROL_EVENT_DISCONNECTED,
    CONTROL_EVENT_MTU_COMPLETE,
    CONTROL_EVENT_SERVICE_ITEM,
    CONTROL_EVENT_SERVICE_DONE,
    CONTROL_EVENT_CHARACTERISTIC_ITEM,
    CONTROL_EVENT_CHARACTERISTIC_DONE,
    CONTROL_EVENT_DESCRIPTOR_ITEM,
    CONTROL_EVENT_DESCRIPTOR_DONE,
    CONTROL_EVENT_WRITE_DONE,
} control_event_type_t;

typedef struct {
    control_event_type_t type;
    int status;
    uint16_t conn_handle;
    union {
        ble_addr_t address;
        struct {
            uint16_t start_handle;
            uint16_t end_handle;
        } service;
        struct {
            uint16_t def_handle;
            uint16_t val_handle;
            uint8_t properties;
            ble_uuid_any_t uuid;
        } characteristic;
        struct {
            uint16_t chr_val_handle;
            uint16_t handle;
            ble_uuid_any_t uuid;
        } descriptor;
        struct {
            uint16_t mtu;
        } mtu;
        struct {
            uint16_t attr_handle;
        } write;
    } data;
} control_event_t;

typedef struct {
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint16_t length;
    bool indication;
    uint8_t data[BLE_NOTIFY_MAX_PAYLOAD];
} notify_event_t;

typedef struct {
    uint16_t def_handle;
    uint16_t val_handle;
    uint8_t properties;
    ble_uuid_any_t uuid;
} discovered_characteristic_t;

typedef enum {
    MIOT_CHAR_FIRMWARE = 0,
    MIOT_CHAR_AUTH_CONTROL,
    MIOT_CHAR_AUTH_DATA,
    MIOT_CHAR_COMMAND_SEND,
    MIOT_CHAR_COMMAND_RECEIVE,
    MIOT_CHAR_DEVICE_INFO,
    MIOT_CHAR_COUNT,
} miot_characteristic_id_t;

typedef struct {
    uint16_t value_handle;
    uint16_t cccd_handle;
} miot_characteristic_t;

static const char *TAG = "cuktech_ble";
static const ble_uuid16_t MIOT_SERVICE_UUID = BLE_UUID16_INIT(0xfe95);
static const uint32_t MIOT_CHARACTERISTIC_IDS[MIOT_CHAR_COUNT] = {
    [MIOT_CHAR_FIRMWARE] = 0x00000004U,
    [MIOT_CHAR_AUTH_CONTROL] = 0x00000010U,
    [MIOT_CHAR_AUTH_DATA] = 0x00000019U,
    [MIOT_CHAR_COMMAND_SEND] = 0x0000001aU,
    [MIOT_CHAR_COMMAND_RECEIVE] = 0x0000001bU,
    [MIOT_CHAR_DEVICE_INFO] = 0x0000001cU,
};
static const miot_characteristic_id_t SUBSCRIBE_TARGETS[] = {
    MIOT_CHAR_AUTH_CONTROL,
    MIOT_CHAR_AUTH_DATA,
    MIOT_CHAR_COMMAND_SEND,
    MIOT_CHAR_COMMAND_RECEIVE,
};
static const uint8_t BLUETOOTH_UUID_BASE_LE[12] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00,
    0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
};

static QueueHandle_t s_control_queue;
static QueueHandle_t s_notify_queue;
static TaskHandle_t s_ble_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static cuktech_ble_status_t s_status = {.state = CUKTECH_BLE_STATE_NOT_STARTED};
static ble_addr_t s_target_address;
static uint8_t s_own_address_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_control_overflow;
static bool s_started;

static bool uuid_matches_miot_id(const ble_uuid_t *uuid, uint32_t id)
{
    if (uuid == NULL) {
        return false;
    }
    if (uuid->type == BLE_UUID_TYPE_16) {
        return id <= UINT16_MAX && BLE_UUID16(uuid)->value == (uint16_t)id;
    }
    if (uuid->type == BLE_UUID_TYPE_32) {
        return BLE_UUID32(uuid)->value == id;
    }
    if (uuid->type != BLE_UUID_TYPE_128) {
        return false;
    }
    const ble_uuid128_t *uuid128 = BLE_UUID128(uuid);
    return memcmp(uuid128->value, BLUETOOTH_UUID_BASE_LE,
                  sizeof(BLUETOOTH_UUID_BASE_LE)) == 0 &&
           uuid128->value[12] == (uint8_t)(id & 0xffU) &&
           uuid128->value[13] == (uint8_t)((id >> 8U) & 0xffU) &&
           uuid128->value[14] == (uint8_t)((id >> 16U) & 0xffU) &&
           uuid128->value[15] == (uint8_t)((id >> 24U) & 0xffU);
}

static bool uuid_is_cccd(const ble_uuid_t *uuid)
{
    return uuid != NULL && uuid->type == BLE_UUID_TYPE_16 &&
           BLE_UUID16(uuid)->value == BLE_GATT_DSC_CLT_CFG_UUID16;
}

static const char *peer_address_type_name(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC:
        return "public";
    case BLE_ADDR_RANDOM:
        return "random";
    case BLE_ADDR_PUBLIC_ID:
        return "public_identity";
    case BLE_ADDR_RANDOM_ID:
        return "random_identity";
    default:
        return "unknown";
    }
}

const char *cuktech_ble_state_name(cuktech_ble_state_t state)
{
    switch (state) {
    case CUKTECH_BLE_STATE_NOT_STARTED:
        return "not_started";
    case CUKTECH_BLE_STATE_DISABLED:
        return "disabled";
    case CUKTECH_BLE_STATE_WAITING_CONFIG:
        return "waiting_config";
    case CUKTECH_BLE_STATE_HOST_SYNC:
        return "host_sync";
    case CUKTECH_BLE_STATE_SCANNING:
        return "scanning";
    case CUKTECH_BLE_STATE_CONNECTING:
        return "connecting";
    case CUKTECH_BLE_STATE_EXCHANGING_MTU:
        return "exchanging_mtu";
    case CUKTECH_BLE_STATE_DISCOVERING_SERVICE:
        return "discovering_service";
    case CUKTECH_BLE_STATE_DISCOVERING_CHARACTERISTICS:
        return "discovering_characteristics";
    case CUKTECH_BLE_STATE_DISCOVERING_DESCRIPTORS:
        return "discovering_descriptors";
    case CUKTECH_BLE_STATE_SUBSCRIBING:
        return "subscribing";
    case CUKTECH_BLE_STATE_READY:
        return "ready";
    case CUKTECH_BLE_STATE_DISCONNECTING:
        return "disconnecting";
    case CUKTECH_BLE_STATE_BACKOFF:
        return "backoff";
    case CUKTECH_BLE_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static void update_status(cuktech_ble_state_t state, bool connected,
                          bool gatt_ready, uint16_t mtu,
                          uint32_t retry_delay_seconds,
                          const char *last_error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.connected = connected;
    s_status.gatt_ready = gatt_ready;
    if (mtu != 0U) {
        s_status.mtu = mtu;
    }
    s_status.retry_delay_seconds = retry_delay_seconds;
    if (last_error != NULL) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "%s", last_error);
    }
    portEXIT_CRITICAL(&s_lock);
}

static void set_last_error_code(const char *stage, int code)
{
    char message[CUKTECH_BLE_LAST_ERROR_MAX_LEN + 1U];
    snprintf(message, sizeof(message), "%s:%d", stage, code);
    portENTER_CRITICAL(&s_lock);
    snprintf(s_status.last_error, sizeof(s_status.last_error), "%s", message);
    portEXIT_CRITICAL(&s_lock);
}

void cuktech_ble_get_status(cuktech_ble_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

static void notify_ble_task(void)
{
    TaskHandle_t task = s_ble_task;
    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

static bool enqueue_control(const control_event_t *event)
{
    if (s_control_queue == NULL ||
        xQueueSend(s_control_queue, event, 0) != pdTRUE) {
        s_control_overflow = true;
        notify_ble_task();
        return false;
    }
    notify_ble_task();
    return true;
}

static void record_notification_drop(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_status.notifications_dropped;
    portEXIT_CRITICAL(&s_lock);
}

static int gap_event_callback(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    control_event_t queued = {0};
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if ((event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
             event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) &&
            memcmp(event->disc.addr.val, s_target_address.val,
                   sizeof(s_target_address.val)) == 0) {
            queued.type = CONTROL_EVENT_ADV_MATCH;
            queued.data.address = event->disc.addr;
            enqueue_control(&queued);
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        queued.type = CONTROL_EVENT_SCAN_COMPLETE;
        queued.status = event->disc_complete.reason;
        enqueue_control(&queued);
        break;
    case BLE_GAP_EVENT_CONNECT:
        queued.type = CONTROL_EVENT_CONNECT_RESULT;
        queued.status = event->connect.status;
        queued.conn_handle = event->connect.conn_handle;
        enqueue_control(&queued);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        queued.type = CONTROL_EVENT_DISCONNECTED;
        queued.status = event->disconnect.reason;
        queued.conn_handle = event->disconnect.conn.conn_handle;
        enqueue_control(&queued);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        notify_event_t notification = {
            .conn_handle = event->notify_rx.conn_handle,
            .attr_handle = event->notify_rx.attr_handle,
            .indication = event->notify_rx.indication,
        };
        uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (length > sizeof(notification.data) ||
            os_mbuf_copydata(event->notify_rx.om, 0, length,
                             notification.data) != 0) {
            record_notification_drop();
            break;
        }
        notification.length = length;
        if (s_notify_queue == NULL ||
            xQueueSend(s_notify_queue, &notification, 0) != pdTRUE) {
            record_notification_drop();
        } else {
            notify_ble_task();
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

static void host_reset_callback(int reason)
{
    control_event_t event = {
        .type = CONTROL_EVENT_HOST_RESET,
        .status = reason,
    };
    enqueue_control(&event);
}

static void host_sync_callback(void)
{
    control_event_t event = {.type = CONTROL_EVENT_HOST_SYNC};
    enqueue_control(&event);
}

static int mtu_callback(uint16_t conn_handle, const struct ble_gatt_error *error,
                        uint16_t mtu, void *argument)
{
    (void)argument;
    control_event_t event = {
        .type = CONTROL_EVENT_MTU_COMPLETE,
        .status = error->status,
        .conn_handle = conn_handle,
    };
    event.data.mtu.mtu = mtu;
    enqueue_control(&event);
    return 0;
}

static int service_callback(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service, void *argument)
{
    (void)argument;
    control_event_t event = {.conn_handle = conn_handle};
    if (error->status == 0 && service != NULL) {
        event.type = CONTROL_EVENT_SERVICE_ITEM;
        event.data.service.start_handle = service->start_handle;
        event.data.service.end_handle = service->end_handle;
    } else {
        event.type = CONTROL_EVENT_SERVICE_DONE;
        event.status = error->status == BLE_HS_EDONE ? 0 : error->status;
    }
    enqueue_control(&event);
    return 0;
}

static int characteristic_callback(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_chr *characteristic,
                                   void *argument)
{
    (void)argument;
    control_event_t event = {.conn_handle = conn_handle};
    if (error->status == 0 && characteristic != NULL) {
        event.type = CONTROL_EVENT_CHARACTERISTIC_ITEM;
        event.data.characteristic.def_handle = characteristic->def_handle;
        event.data.characteristic.val_handle = characteristic->val_handle;
        event.data.characteristic.properties = characteristic->properties;
        ble_uuid_copy(&event.data.characteristic.uuid, &characteristic->uuid.u);
    } else {
        event.type = CONTROL_EVENT_CHARACTERISTIC_DONE;
        event.status = error->status == BLE_HS_EDONE ? 0 : error->status;
    }
    enqueue_control(&event);
    return 0;
}

static int descriptor_callback(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               uint16_t chr_val_handle,
                               const struct ble_gatt_dsc *descriptor,
                               void *argument)
{
    (void)argument;
    control_event_t event = {.conn_handle = conn_handle};
    if (error->status == 0 && descriptor != NULL) {
        event.type = CONTROL_EVENT_DESCRIPTOR_ITEM;
        event.data.descriptor.chr_val_handle = chr_val_handle;
        event.data.descriptor.handle = descriptor->handle;
        ble_uuid_copy(&event.data.descriptor.uuid, &descriptor->uuid.u);
    } else {
        event.type = CONTROL_EVENT_DESCRIPTOR_DONE;
        event.status = error->status == BLE_HS_EDONE ? 0 : error->status;
        event.data.descriptor.chr_val_handle = chr_val_handle;
    }
    enqueue_control(&event);
    return 0;
}

static int write_callback(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attribute, void *argument)
{
    control_event_t event = {
        .type = CONTROL_EVENT_WRITE_DONE,
        .status = error->status,
        .conn_handle = conn_handle,
    };
    event.data.write.attr_handle =
        attribute != NULL ? attribute->handle : (uint16_t)(uintptr_t)argument;
    enqueue_control(&event);
    return 0;
}

static void drain_notifications(void)
{
    notify_event_t event;
    uint32_t received = 0U;
    while (s_notify_queue != NULL &&
           xQueueReceive(s_notify_queue, &event, 0) == pdTRUE) {
        (void)event;
        ++received;
    }
    if (received > 0U) {
        portENTER_CRITICAL(&s_lock);
        s_status.notifications_received += received;
        portEXIT_CRITICAL(&s_lock);
    }
}

static bool wait_control_event(control_event_t *event, uint32_t timeout_ms)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        drain_notifications();
        if (s_control_overflow) {
            s_control_overflow = false;
            set_last_error_code("control_queue_overflow", 0);
            return false;
        }
        if (xQueueReceive(s_control_queue, event, 0) == pdTRUE) {
            return true;
        }
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) {
            return false;
        }
        ulTaskNotifyTake(pdTRUE, timeout - elapsed);
    }
}

static void discard_queued_events(void)
{
    control_event_t control;
    while (s_control_queue != NULL &&
           xQueueReceive(s_control_queue, &control, 0) == pdTRUE) {
    }
    drain_notifications();
    s_control_overflow = false;
    ulTaskNotifyTake(pdTRUE, 0);
}

static bool handle_common_failure_event(const control_event_t *event,
                                        const char *stage)
{
    if (event->type == CONTROL_EVENT_DISCONNECTED) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                      "peer_disconnected");
        ESP_LOGW(TAG, "%s interrupted by disconnect reason=%d", stage,
                 event->status);
        return true;
    }
    if (event->type == CONTROL_EVENT_HOST_RESET) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        set_last_error_code("host_reset", event->status);
        update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U, NULL);
        ESP_LOGE(TAG, "NimBLE host reset during %s reason=%d", stage,
                 event->status);
        return true;
    }
    return false;
}

static bool wait_for_host_sync(void)
{
    update_status(CUKTECH_BLE_STATE_HOST_SYNC, false, false, 0U, 0U, "");
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_HOST_SYNC_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_HOST_SYNC) {
            int rc = ble_hs_util_ensure_addr(0);
            if (rc == 0) {
                rc = ble_hs_id_infer_auto(0, &s_own_address_type);
            }
            if (rc == 0) {
                ESP_LOGI(TAG, "NimBLE host synchronized; own_addr_type=%u",
                         s_own_address_type);
                return true;
            }
            set_last_error_code("own_address", rc);
            return false;
        }
        if (event.type == CONTROL_EVENT_HOST_RESET) {
            ESP_LOGW(TAG, "NimBLE host reset before sync reason=%d", event.status);
        }
    }
    update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                  "host_sync_timeout");
    return false;
}

static bool connect_to_address(const ble_addr_t *address)
{
    update_status(CUKTECH_BLE_STATE_CONNECTING, false, false, 0U, 0U, "");
    ESP_LOGI(TAG, "connecting to configured charger using %s address",
             peer_address_type_name(address->type));
    int rc = ble_gap_connect(s_own_address_type, address, BLE_CONNECT_TIMEOUT_MS,
                             NULL, gap_event_callback, NULL);
    if (rc != 0) {
        set_last_error_code("connect_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_CONNECT_TIMEOUT_MS + 1000U);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_CONNECT_RESULT) {
            if (event.status != 0) {
                set_last_error_code("connect", event.status);
                ESP_LOGW(TAG, "BLE connection failed status=%d", event.status);
                return false;
            }
            s_conn_handle = event.conn_handle;
            update_status(CUKTECH_BLE_STATE_CONNECTING, true, false, 0U, 0U, "");
            ESP_LOGI(TAG, "BLE connection established handle=%u",
                     (unsigned)s_conn_handle);
            return true;
        }
        if (handle_common_failure_event(&event, "connect")) {
            return false;
        }
    }
    int cancel_rc = ble_gap_conn_cancel();
    if (cancel_rc != 0 && cancel_rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "connect cancel failed rc=%d", cancel_rc);
    }
    update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                  "connect_timeout");
    return false;
}

static bool scan_then_connect(void)
{
    struct ble_gap_disc_params parameters = {
        .filter_duplicates = 1,
        .passive = 1,
        .itvl = 0,
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
    };
    update_status(CUKTECH_BLE_STATE_SCANNING, false, false, 0U, 0U, "");
    ESP_LOGI(TAG, "scanning for configured charger for %d ms",
             BLE_SCAN_DURATION_MS);
    int rc = ble_gap_disc(s_own_address_type, BLE_SCAN_DURATION_MS, &parameters,
                          gap_event_callback, NULL);
    if (rc != 0) {
        set_last_error_code("scan_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_SCAN_DURATION_MS + 1000U);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_ADV_MATCH) {
            int cancel_rc = ble_gap_disc_cancel();
            if (cancel_rc != 0 && cancel_rc != BLE_HS_EALREADY) {
                set_last_error_code("scan_cancel", cancel_rc);
                return false;
            }
            ESP_LOGI(TAG, "configured charger advertisement found; address_type=%s",
                     peer_address_type_name(event.data.address.type));
            return connect_to_address(&event.data.address);
        }
        if (event.type == CONTROL_EVENT_SCAN_COMPLETE) {
            break;
        }
        if (event.type == CONTROL_EVENT_HOST_RESET) {
            handle_common_failure_event(&event, "scan");
            return false;
        }
    }
    int cancel_rc = ble_gap_disc_cancel();
    if (cancel_rc != 0 && cancel_rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "scan cleanup failed rc=%d", cancel_rc);
    }
    ESP_LOGI(TAG, "charger not advertising; trying bounded direct connections");
    ble_addr_t direct = s_target_address;
    direct.type = BLE_ADDR_PUBLIC;
    discard_queued_events();
    if (connect_to_address(&direct)) {
        return true;
    }
    discard_queued_events();
    direct.type = BLE_ADDR_RANDOM;
    return connect_to_address(&direct);
}

static bool exchange_mtu(void)
{
    update_status(CUKTECH_BLE_STATE_EXCHANGING_MTU, true, false, 0U, 0U, "");
    int rc = ble_gattc_exchange_mtu(s_conn_handle, mtu_callback, NULL);
    if (rc != 0) {
        set_last_error_code("mtu_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_GATT_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_MTU_COMPLETE) {
            if (event.status != 0) {
                set_last_error_code("mtu", event.status);
                return false;
            }
            update_status(CUKTECH_BLE_STATE_EXCHANGING_MTU, true, false,
                          event.data.mtu.mtu, 0U, "");
            ESP_LOGI(TAG, "ATT MTU exchanged: %u", event.data.mtu.mtu);
            return true;
        }
        if (handle_common_failure_event(&event, "mtu_exchange")) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U, "mtu_timeout");
    return false;
}

static bool discover_service(uint16_t *start_handle, uint16_t *end_handle)
{
    *start_handle = 0U;
    *end_handle = 0U;
    update_status(CUKTECH_BLE_STATE_DISCOVERING_SERVICE, true, false, 0U, 0U, "");
    int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &MIOT_SERVICE_UUID.u,
                                        service_callback, NULL);
    if (rc != 0) {
        set_last_error_code("service_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_GATT_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_SERVICE_ITEM && *start_handle == 0U) {
            *start_handle = event.data.service.start_handle;
            *end_handle = event.data.service.end_handle;
        } else if (event.type == CONTROL_EVENT_SERVICE_DONE) {
            if (event.status != 0 || *start_handle == 0U) {
                set_last_error_code("service_discovery",
                                    event.status != 0 ? event.status : BLE_HS_ENOENT);
                return false;
            }
            ESP_LOGI(TAG, "MiOT FE95 service discovered");
            return true;
        } else if (handle_common_failure_event(&event, "service_discovery")) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U,
                  "service_timeout");
    return false;
}

static int characteristic_compare(const void *left, const void *right)
{
    const discovered_characteristic_t *a = left;
    const discovered_characteristic_t *b = right;
    return (a->def_handle > b->def_handle) - (a->def_handle < b->def_handle);
}

static bool discover_characteristics(
    uint16_t service_start, uint16_t service_end,
    discovered_characteristic_t characteristics[BLE_MAX_DISCOVERED_CHARACTERISTICS],
    size_t *characteristic_count,
    miot_characteristic_t miot_characteristics[MIOT_CHAR_COUNT])
{
    *characteristic_count = 0U;
    memset(miot_characteristics, 0,
           sizeof(miot_characteristic_t) * MIOT_CHAR_COUNT);
    update_status(CUKTECH_BLE_STATE_DISCOVERING_CHARACTERISTICS, true, false,
                  0U, 0U, "");
    int rc = ble_gattc_disc_all_chrs(s_conn_handle, service_start, service_end,
                                     characteristic_callback, NULL);
    if (rc != 0) {
        set_last_error_code("characteristics_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_GATT_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_CHARACTERISTIC_ITEM) {
            if (*characteristic_count >= BLE_MAX_DISCOVERED_CHARACTERISTICS) {
                update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U,
                              "too_many_characteristics");
                return false;
            }
            discovered_characteristic_t *stored =
                &characteristics[(*characteristic_count)++];
            stored->def_handle = event.data.characteristic.def_handle;
            stored->val_handle = event.data.characteristic.val_handle;
            stored->properties = event.data.characteristic.properties;
            ble_uuid_copy(&stored->uuid, &event.data.characteristic.uuid.u);
            for (size_t index = 0U; index < MIOT_CHAR_COUNT; ++index) {
                if (uuid_matches_miot_id(&stored->uuid.u,
                                         MIOT_CHARACTERISTIC_IDS[index])) {
                    miot_characteristics[index].value_handle = stored->val_handle;
                }
            }
        } else if (event.type == CONTROL_EVENT_CHARACTERISTIC_DONE) {
            if (event.status != 0) {
                set_last_error_code("characteristics", event.status);
                return false;
            }
            for (size_t index = 0U; index < MIOT_CHAR_COUNT; ++index) {
                if (miot_characteristics[index].value_handle == 0U) {
                    set_last_error_code("missing_characteristic", (int)index);
                    return false;
                }
            }
            qsort(characteristics, *characteristic_count,
                  sizeof(characteristics[0]), characteristic_compare);
            ESP_LOGI(TAG, "all six required MiOT characteristics discovered");
            return true;
        } else if (handle_common_failure_event(&event,
                                               "characteristic_discovery")) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U,
                  "characteristics_timeout");
    return false;
}

static uint16_t descriptor_end_handle(
    const discovered_characteristic_t *characteristics,
    size_t characteristic_count, uint16_t value_handle, uint16_t service_end)
{
    for (size_t index = 0U; index < characteristic_count; ++index) {
        if (characteristics[index].val_handle == value_handle) {
            if (index + 1U < characteristic_count) {
                return (uint16_t)(characteristics[index + 1U].def_handle - 1U);
            }
            return service_end;
        }
    }
    return 0U;
}

static bool discover_cccd(
    const discovered_characteristic_t *characteristics,
    size_t characteristic_count, uint16_t service_end,
    miot_characteristic_t *target)
{
    uint16_t end_handle = descriptor_end_handle(
        characteristics, characteristic_count, target->value_handle, service_end);
    if (end_handle <= target->value_handle) {
        update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U,
                      "descriptor_range_missing");
        return false;
    }
    int rc = ble_gattc_disc_all_dscs(s_conn_handle, target->value_handle,
                                     end_handle, descriptor_callback, NULL);
    if (rc != 0) {
        set_last_error_code("descriptor_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_GATT_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_DESCRIPTOR_ITEM &&
            event.data.descriptor.chr_val_handle == target->value_handle &&
            uuid_is_cccd(&event.data.descriptor.uuid.u)) {
            target->cccd_handle = event.data.descriptor.handle;
        } else if (event.type == CONTROL_EVENT_DESCRIPTOR_DONE &&
                   event.data.descriptor.chr_val_handle == target->value_handle) {
            if (event.status != 0 || target->cccd_handle == 0U) {
                set_last_error_code("descriptor",
                                    event.status != 0 ? event.status : BLE_HS_ENOENT);
                return false;
            }
            return true;
        } else if (handle_common_failure_event(&event, "descriptor_discovery")) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U,
                  "descriptor_timeout");
    return false;
}

static bool subscribe_characteristic(miot_characteristic_t *target)
{
    const uint8_t enable_notifications[2] = {0x01, 0x00};
    int rc = ble_gattc_write_flat(
        s_conn_handle, target->cccd_handle, enable_notifications,
        sizeof(enable_notifications), write_callback,
        (void *)(uintptr_t)target->cccd_handle);
    if (rc != 0) {
        set_last_error_code("subscribe_start", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_GATT_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining = (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                                        portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_WRITE_DONE &&
            event.data.write.attr_handle == target->cccd_handle) {
            if (event.status != 0) {
                set_last_error_code("subscribe", event.status);
                return false;
            }
            return true;
        }
        if (handle_common_failure_event(&event, "subscribe")) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_ERROR, true, false, 0U, 0U,
                  "subscribe_timeout");
    return false;
}

static bool prepare_gatt_link(void)
{
    uint16_t service_start;
    uint16_t service_end;
    discovered_characteristic_t
        characteristics[BLE_MAX_DISCOVERED_CHARACTERISTICS] = {0};
    size_t characteristic_count = 0U;
    miot_characteristic_t miot_characteristics[MIOT_CHAR_COUNT] = {0};

    if (!exchange_mtu() || !discover_service(&service_start, &service_end) ||
        !discover_characteristics(service_start, service_end, characteristics,
                                  &characteristic_count,
                                  miot_characteristics)) {
        return false;
    }
    update_status(CUKTECH_BLE_STATE_DISCOVERING_DESCRIPTORS, true, false,
                  0U, 0U, "");
    for (size_t index = 0U;
         index < sizeof(SUBSCRIBE_TARGETS) / sizeof(SUBSCRIBE_TARGETS[0]);
         ++index) {
        if (!discover_cccd(characteristics, characteristic_count, service_end,
                           &miot_characteristics[SUBSCRIBE_TARGETS[index]])) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_SUBSCRIBING, true, false, 0U, 0U, "");
    for (size_t index = 0U;
         index < sizeof(SUBSCRIBE_TARGETS) / sizeof(SUBSCRIBE_TARGETS[0]);
         ++index) {
        if (!subscribe_characteristic(
                &miot_characteristics[SUBSCRIBE_TARGETS[index]])) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_READY, true, true, 0U, 0U, "");
    ESP_LOGI(TAG, "GATT link ready; four MiOT notification channels enabled");
    return true;
}

static void disconnect_cleanly(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        update_status(CUKTECH_BLE_STATE_DISCONNECTING, false, false, 0U, 0U,
                      NULL);
        return;
    }
    uint16_t handle = s_conn_handle;
    update_status(CUKTECH_BLE_STATE_DISCONNECTING, true, false, 0U, 0U, NULL);
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        ESP_LOGW(TAG, "disconnect request failed rc=%d", rc);
    }
    if (rc == 0) {
        control_event_t event;
        TickType_t started = xTaskGetTickCount();
        TickType_t timeout = pdMS_TO_TICKS(BLE_DISCONNECT_TIMEOUT_MS);
        while (xTaskGetTickCount() - started < timeout) {
            uint32_t remaining =
                (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                           portTICK_PERIOD_MS);
            if (!wait_control_event(&event, remaining)) {
                break;
            }
            if (event.type == CONTROL_EVENT_DISCONNECTED &&
                event.conn_handle == handle) {
                break;
            }
        }
    }
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    update_status(CUKTECH_BLE_STATE_DISCONNECTING, false, false, 0U, 0U, NULL);
    discard_queued_events();
}

static void wait_until_disconnected(void)
{
    control_event_t event;
    while (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        if (!wait_control_event(&event, 1000U)) {
            continue;
        }
        if (event.type == CONTROL_EVENT_DISCONNECTED) {
            ESP_LOGW(TAG, "BLE link disconnected reason=%d", event.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                          "peer_disconnected");
            return;
        }
        if (event.type == CONTROL_EVENT_HOST_RESET) {
            handle_common_failure_event(&event, "ready");
            return;
        }
    }
}

static void retry_delay(uint32_t seconds)
{
    update_status(CUKTECH_BLE_STATE_BACKOFF, false, false, 0U, seconds, NULL);
    ESP_LOGI(TAG, "BLE retry in %" PRIu32 " seconds", seconds);
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000U));
}

static void ble_application_task(void *argument)
{
    (void)argument;
    if (!wait_for_host_sync()) {
        ESP_LOGE(TAG, "NimBLE host did not synchronize; BLE task stopped");
        vTaskDelete(NULL);
        return;
    }
    uint32_t backoff = 0U;
    for (;;) {
        discard_queued_events();
        if (scan_then_connect() && prepare_gatt_link()) {
            backoff = 0U;
            wait_until_disconnected();
        } else {
            disconnect_cleanly();
        }
        backoff = cuktech_ble_next_backoff(backoff, BLE_RETRY_MAX_SECONDS);
        retry_delay(backoff);
    }
}

static void nimble_host_task(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t cuktech_ble_start(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.enabled = config->ble_enabled;
    if (!config->ble_enabled) {
        s_status.state = CUKTECH_BLE_STATE_DISABLED;
        ESP_LOGI(TAG, "BLE disabled by configuration");
        return ESP_OK;
    }
    uint8_t display_order[6];
    if (!config->token_configured ||
        !cuktech_ble_parse_mac(config->charger_mac, display_order)) {
        s_status.state = CUKTECH_BLE_STATE_WAITING_CONFIG;
        snprintf(s_status.last_error, sizeof(s_status.last_error),
                 "charger_mac_and_token_required");
        ESP_LOGW(TAG, "BLE waiting for a valid charger MAC and Token");
        return ESP_OK;
    }
    s_target_address.type = BLE_ADDR_PUBLIC;
    cuktech_ble_mac_to_nimble(display_order, s_target_address.val);

    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) {
        s_status.state = CUKTECH_BLE_STATE_ERROR;
        snprintf(s_status.last_error, sizeof(s_status.last_error),
                 "nimble_init:%d", error);
        return error;
    }
    s_control_queue = xQueueCreate(BLE_CONTROL_QUEUE_DEPTH,
                                   sizeof(control_event_t));
    s_notify_queue = xQueueCreate(BLE_NOTIFY_QUEUE_DEPTH,
                                  sizeof(notify_event_t));
    if (s_control_queue == NULL || s_notify_queue == NULL) {
        if (s_control_queue != NULL) {
            vQueueDelete(s_control_queue);
            s_control_queue = NULL;
        }
        if (s_notify_queue != NULL) {
            vQueueDelete(s_notify_queue);
            s_notify_queue = NULL;
        }
        nimble_port_deinit();
        s_status.state = CUKTECH_BLE_STATE_ERROR;
        snprintf(s_status.last_error, sizeof(s_status.last_error),
                 "queue_allocation_failed");
        return ESP_ERR_NO_MEM;
    }
    ble_hs_cfg.reset_cb = host_reset_callback;
    ble_hs_cfg.sync_cb = host_sync_callback;
    if (xTaskCreate(ble_application_task, "cuktech_ble", BLE_APP_TASK_STACK_SIZE,
                    NULL, BLE_APP_TASK_PRIORITY, &s_ble_task) != pdPASS) {
        vQueueDelete(s_notify_queue);
        vQueueDelete(s_control_queue);
        s_notify_queue = NULL;
        s_control_queue = NULL;
        nimble_port_deinit();
        s_status.state = CUKTECH_BLE_STATE_ERROR;
        snprintf(s_status.last_error, sizeof(s_status.last_error),
                 "task_allocation_failed");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    update_status(CUKTECH_BLE_STATE_HOST_SYNC, false, false, 0U, 0U, "");
    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "NimBLE Central started for charger MAC %s",
             config->charger_mac);
    return ESP_OK;
}
