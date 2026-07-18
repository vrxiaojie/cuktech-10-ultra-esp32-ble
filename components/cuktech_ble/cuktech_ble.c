#include "cuktech_ble.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charger_state.h"
#include "cuktech_ble_core.h"
#include "cuktech_command.h"
#include "cuktech_miot_auth.h"
#include "cuktech_protocol.h"
#include "esp_log.h"
#include "esp_random.h"
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
#define BLE_REQUEST_QUEUE_DEPTH 8U
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
#define BLE_PENDING_NOTIFY_DEPTH 8U
#define BLE_AUTH_FAILURE_LOCK_COUNT 5U
#define BLE_AUTH_MIN_RETRY_SECONDS 3U
#define BLE_READ_MAX_PAYLOAD 64U
#define BLE_COMMAND_TIMEOUT_MS 3000U
#define BLE_COMMAND_RESPONSE_TIMEOUT_MS 8000U
#define BLE_SETTINGS_REFRESH_MS 60000U
#define BLE_INIT_PUSH_MAX_COUNT 60U
#define BLE_INIT_PUSH_MAX_MS 6000U
#define BLE_DECRYPT_FAILURE_LIMIT 10U

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
    CONTROL_EVENT_READ_DONE,
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
        struct {
            uint16_t attr_handle;
            uint16_t length;
            uint8_t bytes[BLE_READ_MAX_PAYLOAD];
        } read;
    } data;
} control_event_t;

typedef struct {
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint16_t length;
    bool indication;
    uint8_t data[BLE_NOTIFY_MAX_PAYLOAD];
} notify_event_t;

typedef enum {
    BLE_REQUEST_CONTROL = 0,
    BLE_REQUEST_ENABLE,
} ble_request_type_t;

typedef struct {
    ble_request_type_t type;
    uint32_t request_id;
    union {
        cuktech_control_command_t control;
        bool enabled;
    } data;
} ble_request_t;

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
    MIOT_CHAR_DEVICE_INFO,
};
static const uint8_t BLUETOOTH_UUID_BASE_LE[12] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00,
    0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
};
static const uint16_t READABLE_SETTINGS_PIIDS[] = {
    5U, 6U, 8U, 9U, 10U, 11U, 12U, 13U,
    15U, 16U, 17U, 18U, 19U, 20U, 21U,
};

static QueueHandle_t s_control_queue;
static QueueHandle_t s_notify_queue;
static QueueHandle_t s_request_queue;
static TaskHandle_t s_ble_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static cuktech_ble_status_t s_status = {.state = CUKTECH_BLE_STATE_NOT_STARTED};
static ble_addr_t s_target_address;
static uint8_t s_token[CUKTECH_TOKEN_SIZE];
static cuktech_session_t s_session;
static miot_characteristic_t s_miot_characteristics[MIOT_CHAR_COUNT];
static notify_event_t s_pending_notifications[BLE_PENDING_NOTIFY_DEPTH];
static size_t s_pending_notification_count;
static uint8_t s_own_address_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_control_overflow;
static uint32_t s_next_request_id;
static bool s_started;

static void notify_ble_task(void);

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
    case CUKTECH_BLE_STATE_AUTHENTICATING:
        return "authenticating";
    case CUKTECH_BLE_STATE_AUTHENTICATED:
        return "authenticated";
    case CUKTECH_BLE_STATE_AUTH_FAILED_LOCKED:
        return "auth_failed_locked";
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
    if (!connected) {
        s_status.authenticated = false;
    }
    if (mtu != 0U) {
        s_status.mtu = mtu;
    }
    s_status.retry_delay_seconds = retry_delay_seconds;
    if (last_error != NULL) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "%s", last_error);
    }
    portEXIT_CRITICAL(&s_lock);
}

static void update_authentication_status(bool authenticated,
                                         uint32_t failure_count,
                                         const char *last_error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.authenticated = authenticated;
    s_status.authentication_failures = failure_count;
    if (last_error != NULL) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "%s",
                 last_error);
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

static bool ble_runtime_enabled(void)
{
    portENTER_CRITICAL(&s_lock);
    bool enabled = s_status.enabled;
    portEXIT_CRITICAL(&s_lock);
    return enabled;
}

static uint32_t allocate_request_id(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_next_request_id;
    if (s_next_request_id == 0U) {
        ++s_next_request_id;
    }
    uint32_t request_id = s_next_request_id;
    portEXIT_CRITICAL(&s_lock);
    return request_id;
}

static esp_err_t enqueue_request(const ble_request_t *request,
                                 uint32_t *request_id)
{
    if (s_request_queue == NULL || request == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_request_queue, request, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    portENTER_CRITICAL(&s_lock);
    ++s_status.commands_accepted;
    if (request->type == BLE_REQUEST_ENABLE) {
        s_status.enabled = request->data.enabled;
    }
    portEXIT_CRITICAL(&s_lock);
    if (request_id != NULL) {
        *request_id = request->request_id;
    }
    notify_ble_task();
    return ESP_OK;
}

esp_err_t cuktech_ble_submit_command(const cuktech_control_command_t *command,
                                     uint32_t *request_id)
{
    if (!cuktech_control_command_valid(command)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    bool ready = s_status.enabled && s_status.authenticated;
    portEXIT_CRITICAL(&s_lock);
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ble_request_t request = {
        .type = BLE_REQUEST_CONTROL,
        .request_id = allocate_request_id(),
        .data.control = *command,
    };
    return enqueue_request(&request, request_id);
}

esp_err_t cuktech_ble_set_enabled(bool enabled, uint32_t *request_id)
{
    ble_request_t request = {
        .type = BLE_REQUEST_ENABLE,
        .request_id = allocate_request_id(),
        .data.enabled = enabled,
    };
    return enqueue_request(&request, request_id);
}

static void record_request_result(uint32_t request_id, bool success,
                                  const char *error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.last_request_id = request_id;
    if (success) {
        ++s_status.commands_completed;
    } else {
        ++s_status.commands_failed;
        if (error != NULL) {
            snprintf(s_status.last_error, sizeof(s_status.last_error),
                     "command:%s", error);
        }
    }
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

static int read_callback(uint16_t conn_handle,
                         const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attribute, void *argument)
{
    control_event_t event = {
        .type = CONTROL_EVENT_READ_DONE,
        .status = error->status,
        .conn_handle = conn_handle,
    };
    event.data.read.attr_handle =
        attribute != NULL ? attribute->handle : (uint16_t)(uintptr_t)argument;
    if (error->status == 0 && attribute != NULL && attribute->om != NULL) {
        uint16_t length = OS_MBUF_PKTLEN(attribute->om);
        if (length > sizeof(event.data.read.bytes) ||
            os_mbuf_copydata(attribute->om, 0, length,
                             event.data.read.bytes) != 0) {
            event.status = BLE_HS_EMSGSIZE;
        } else {
            event.data.read.length = length;
        }
    }
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

static void record_notification_received(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_status.notifications_received;
    portEXIT_CRITICAL(&s_lock);
}

static bool pending_notification_take(uint16_t attr_handle,
                                      notify_event_t *notification)
{
    for (size_t index = 0U; index < s_pending_notification_count; ++index) {
        if (s_pending_notifications[index].attr_handle == attr_handle) {
            *notification = s_pending_notifications[index];
            if (index + 1U < s_pending_notification_count) {
                memmove(&s_pending_notifications[index],
                        &s_pending_notifications[index + 1U],
                        (s_pending_notification_count - index - 1U) *
                            sizeof(s_pending_notifications[0]));
            }
            --s_pending_notification_count;
            return true;
        }
    }
    return false;
}

static void pending_notification_store(const notify_event_t *notification)
{
    if (s_pending_notification_count < BLE_PENDING_NOTIFY_DEPTH) {
        s_pending_notifications[s_pending_notification_count++] = *notification;
    } else {
        record_notification_drop();
    }
}

static void discard_notifications_for_handle(uint16_t attr_handle)
{
    for (size_t index = 0U; index < s_pending_notification_count;) {
        if (s_pending_notifications[index].attr_handle == attr_handle) {
            if (index + 1U < s_pending_notification_count) {
                memmove(&s_pending_notifications[index],
                        &s_pending_notifications[index + 1U],
                        (s_pending_notification_count - index - 1U) *
                            sizeof(s_pending_notifications[0]));
            }
            --s_pending_notification_count;
        } else {
            ++index;
        }
    }
}

static cuktech_command_io_status_t receive_notification_for_handle(
    uint16_t expected_handle, uint8_t *data, size_t data_capacity,
    size_t *data_len, uint32_t timeout_ms)
{
    if (expected_handle == 0U || data == NULL || data_len == NULL) {
        return CUKTECH_COMMAND_IO_ERROR;
    }
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        if (!ble_runtime_enabled()) {
            return CUKTECH_COMMAND_IO_DISCONNECTED;
        }
        notify_event_t notification;
        if (pending_notification_take(expected_handle, &notification)) {
            if (notification.length > data_capacity) {
                return CUKTECH_COMMAND_IO_ERROR;
            }
            memcpy(data, notification.data, notification.length);
            *data_len = notification.length;
            return CUKTECH_COMMAND_IO_OK;
        }
        while (xQueueReceive(s_notify_queue, &notification, 0) == pdTRUE) {
            record_notification_received();
            if (notification.conn_handle != s_conn_handle) {
                continue;
            }
            if (notification.attr_handle == expected_handle) {
                if (notification.length > data_capacity) {
                    return CUKTECH_COMMAND_IO_ERROR;
                }
                memcpy(data, notification.data, notification.length);
                *data_len = notification.length;
                return CUKTECH_COMMAND_IO_OK;
            }
            pending_notification_store(&notification);
        }
        control_event_t control;
        while (xQueueReceive(s_control_queue, &control, 0) == pdTRUE) {
            if (control.type == CONTROL_EVENT_DISCONNECTED) {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                charger_state_set_connection(false, false);
                update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                              "peer_disconnected");
                return CUKTECH_COMMAND_IO_DISCONNECTED;
            }
            if (control.type == CONTROL_EVENT_HOST_RESET) {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                charger_state_set_connection(false, false);
                set_last_error_code("host_reset", control.status);
                update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                              NULL);
                return CUKTECH_COMMAND_IO_DISCONNECTED;
            }
        }
        if (s_control_overflow) {
            s_control_overflow = false;
            return CUKTECH_COMMAND_IO_ERROR;
        }
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) {
            return CUKTECH_COMMAND_IO_TIMEOUT;
        }
        ulTaskNotifyTake(pdTRUE, timeout - elapsed);
    }
}

static bool wait_control_event_internal(control_event_t *event,
                                        uint32_t timeout_ms,
                                        bool cancel_when_disabled)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        if (cancel_when_disabled && !ble_runtime_enabled()) {
            return false;
        }
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

static bool wait_control_event(control_event_t *event, uint32_t timeout_ms)
{
    return wait_control_event_internal(event, timeout_ms, true);
}

static bool wait_control_event_unconditional(control_event_t *event,
                                             uint32_t timeout_ms)
{
    return wait_control_event_internal(event, timeout_ms, false);
}

static void discard_queued_events(void)
{
    control_event_t control;
    while (s_control_queue != NULL &&
           xQueueReceive(s_control_queue, &control, 0) == pdTRUE) {
    }
    s_pending_notification_count = 0U;
    drain_notifications();
    s_control_overflow = false;
    ulTaskNotifyTake(pdTRUE, 0);
}

static uint16_t auth_channel_handle(cuktech_miot_auth_channel_t channel)
{
    return channel == CUKTECH_MIOT_AUTH_CHANNEL_CONTROL
               ? s_miot_characteristics[MIOT_CHAR_AUTH_CONTROL].value_handle
               : s_miot_characteristics[MIOT_CHAR_AUTH_DATA].value_handle;
}

static cuktech_miot_auth_io_status_t auth_transport_write(
    void *context, cuktech_miot_auth_channel_t channel, const uint8_t *data,
    size_t data_len)
{
    (void)context;
    uint16_t handle = auth_channel_handle(channel);
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return CUKTECH_MIOT_AUTH_IO_DISCONNECTED;
    }
    if (handle == 0U || data == NULL || data_len == 0U ||
        data_len > UINT16_MAX) {
        return CUKTECH_MIOT_AUTH_IO_ERROR;
    }
    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, handle, data,
                                         (uint16_t)data_len);
    if (rc == BLE_HS_ENOTCONN) {
        return CUKTECH_MIOT_AUTH_IO_DISCONNECTED;
    }
    return rc == 0 ? CUKTECH_MIOT_AUTH_IO_OK
                   : CUKTECH_MIOT_AUTH_IO_ERROR;
}

static cuktech_miot_auth_io_status_t auth_transport_receive(
    void *context, cuktech_miot_auth_channel_t channel, uint8_t *data,
    size_t data_capacity, size_t *data_len, uint32_t timeout_ms)
{
    (void)context;
    if (data == NULL || data_len == NULL) {
        return CUKTECH_MIOT_AUTH_IO_ERROR;
    }
    uint16_t expected_handle = auth_channel_handle(channel);
    if (expected_handle == 0U) {
        return CUKTECH_MIOT_AUTH_IO_ERROR;
    }
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        if (!ble_runtime_enabled()) {
            return CUKTECH_MIOT_AUTH_IO_DISCONNECTED;
        }
        notify_event_t notification;
        if (pending_notification_take(expected_handle, &notification)) {
            if (notification.length > data_capacity) {
                return CUKTECH_MIOT_AUTH_IO_ERROR;
            }
            memcpy(data, notification.data, notification.length);
            *data_len = notification.length;
            return CUKTECH_MIOT_AUTH_IO_OK;
        }
        while (xQueueReceive(s_notify_queue, &notification, 0) == pdTRUE) {
            record_notification_received();
            if (notification.conn_handle != s_conn_handle) {
                continue;
            }
            if (notification.attr_handle == expected_handle) {
                if (notification.length > data_capacity) {
                    return CUKTECH_MIOT_AUTH_IO_ERROR;
                }
                memcpy(data, notification.data, notification.length);
                *data_len = notification.length;
                return CUKTECH_MIOT_AUTH_IO_OK;
            }
            pending_notification_store(&notification);
        }
        control_event_t control;
        while (xQueueReceive(s_control_queue, &control, 0) == pdTRUE) {
            if (control.type == CONTROL_EVENT_DISCONNECTED) {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                charger_state_set_connection(false, false);
                update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                              "peer_disconnected");
                return CUKTECH_MIOT_AUTH_IO_DISCONNECTED;
            }
            if (control.type == CONTROL_EVENT_HOST_RESET) {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                charger_state_set_connection(false, false);
                set_last_error_code("host_reset", control.status);
                update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                              NULL);
                return CUKTECH_MIOT_AUTH_IO_DISCONNECTED;
            }
        }
        if (s_control_overflow) {
            s_control_overflow = false;
            return CUKTECH_MIOT_AUTH_IO_ERROR;
        }
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) {
            return CUKTECH_MIOT_AUTH_IO_TIMEOUT;
        }
        ulTaskNotifyTake(pdTRUE, timeout - elapsed);
    }
}

static void auth_transport_discard(void *context,
                                   cuktech_miot_auth_channel_t channel)
{
    (void)context;
    uint16_t target_handle = auth_channel_handle(channel);
    for (size_t index = 0U; index < s_pending_notification_count;) {
        if (s_pending_notifications[index].attr_handle == target_handle) {
            if (index + 1U < s_pending_notification_count) {
                memmove(&s_pending_notifications[index],
                        &s_pending_notifications[index + 1U],
                        (s_pending_notification_count - index - 1U) *
                            sizeof(s_pending_notifications[0]));
            }
            --s_pending_notification_count;
        } else {
            ++index;
        }
    }
    notify_event_t notification;
    while (xQueueReceive(s_notify_queue, &notification, 0) == pdTRUE) {
        record_notification_received();
        if (notification.attr_handle != target_handle) {
            pending_notification_store(&notification);
        }
    }
}

static void auth_transport_delay(void *context, uint32_t delay_ms)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static bool auth_transport_random(void *context, uint8_t *data, size_t data_len)
{
    (void)context;
    if (data == NULL || data_len == 0U) {
        return false;
    }
    esp_fill_random(data, data_len);
    return true;
}

static bool authentication_status_counts_failure(
    cuktech_miot_auth_status_t status)
{
    return status != CUKTECH_MIOT_AUTH_DISCONNECTED &&
           status != CUKTECH_MIOT_AUTH_TRANSPORT_ERROR;
}

static cuktech_miot_auth_status_t authenticate_link(uint32_t failure_count)
{
    const cuktech_miot_auth_transport_t transport = {
        .context = NULL,
        .write = auth_transport_write,
        .receive = auth_transport_receive,
        .discard = auth_transport_discard,
        .delay_ms = auth_transport_delay,
        .fill_random = auth_transport_random,
    };
    update_status(CUKTECH_BLE_STATE_AUTHENTICATING, true, true, 0U, 0U, "");
    update_authentication_status(false, failure_count, NULL);
    ESP_LOGI(TAG, "starting MiOT login authentication");
    cuktech_miot_auth_status_t status =
        cuktech_miot_authenticate(s_token, &transport, &s_session);
    if (status == CUKTECH_MIOT_AUTH_OK) {
        update_status(CUKTECH_BLE_STATE_AUTHENTICATED, true, true, 0U, 0U, "");
        update_authentication_status(true, 0U, "");
        charger_state_set_connection(true, true);
        ESP_LOGI(TAG, "MiOT login authentication succeeded");
    } else {
        char error[CUKTECH_BLE_LAST_ERROR_MAX_LEN + 1U];
        snprintf(error, sizeof(error), "auth:%s",
                 cuktech_miot_auth_status_name(status));
        uint32_t reported_failures =
            failure_count +
            (authentication_status_counts_failure(status) ? 1U : 0U);
        update_authentication_status(false, reported_failures, error);
        ESP_LOGW(TAG, "MiOT login authentication failed stage=%s",
                 cuktech_miot_auth_status_name(status));
    }
    return status;
}

static uint16_t command_channel_handle(cuktech_command_channel_t channel)
{
    return channel == CUKTECH_COMMAND_CHANNEL_SEND
               ? s_miot_characteristics[MIOT_CHAR_COMMAND_SEND].value_handle
               : s_miot_characteristics[MIOT_CHAR_COMMAND_RECEIVE].value_handle;
}

static cuktech_command_io_status_t command_transport_write(
    void *context, cuktech_command_channel_t channel, const uint8_t *data,
    size_t data_len)
{
    (void)context;
    uint16_t handle = command_channel_handle(channel);
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return CUKTECH_COMMAND_IO_DISCONNECTED;
    }
    if (handle == 0U || data == NULL || data_len == 0U ||
        data_len > UINT16_MAX) {
        return CUKTECH_COMMAND_IO_ERROR;
    }
    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, handle, data,
                                         (uint16_t)data_len);
    if (rc == BLE_HS_ENOTCONN) {
        return CUKTECH_COMMAND_IO_DISCONNECTED;
    }
    return rc == 0 ? CUKTECH_COMMAND_IO_OK : CUKTECH_COMMAND_IO_ERROR;
}

static cuktech_command_io_status_t command_transport_receive(
    void *context, cuktech_command_channel_t channel, uint8_t *data,
    size_t data_capacity, size_t *data_len, uint32_t timeout_ms)
{
    (void)context;
    return receive_notification_for_handle(command_channel_handle(channel),
                                           data, data_capacity, data_len,
                                           timeout_ms);
}

static const cuktech_command_transport_t COMMAND_TRANSPORT = {
    .context = NULL,
    .write = command_transport_write,
    .receive = command_transport_receive,
};

static bool process_miot_plaintext(const uint8_t *plaintext,
                                   size_t plaintext_len)
{
    if (plaintext == NULL || plaintext_len < 9U || plaintext[1] != 0x20U ||
        plaintext[4] != 0x04U || plaintext[6] != 2U ||
        plaintext[8] != 0U) {
        return false;
    }
    uint8_t piid = plaintext[7];
    if (piid < 1U || piid > 4U) {
        return false;
    }
    charger_state_snapshot_t snapshot;
    charger_state_get_snapshot(&snapshot);
    cuktech_pdo_kind_t pdo_kind;
    cuktech_type_c_switches_t switches;
    charger_state_core_protocol_inputs(&snapshot, piid, &pdo_kind, &switches);
    cuktech_port_state_t port;
    if (cuktech_decode_port(piid, plaintext, plaintext_len, pdo_kind,
                            piid <= 2U ? &switches : NULL,
                            &port) != CUKTECH_PROTOCOL_OK) {
        return false;
    }
    charger_state_update_port(piid, &port);
    ESP_LOGD(TAG,
             "port piid=%u voltage=%.1f current=%.1f power=%.1f active=%s protocol=%s",
             piid, (double)port.voltage, (double)port.current,
             (double)port.power, port.active ? "yes" : "no",
             cuktech_charge_protocol_name(port.protocol));
    return true;
}

static cuktech_command_status_t receive_and_process_plaintext(
    uint8_t *plaintext, size_t plaintext_capacity, size_t *plaintext_len,
    uint32_t timeout_ms, uint32_t *decrypt_failures)
{
    cuktech_command_status_t status = cuktech_command_receive(
        &s_session, &COMMAND_TRANSPORT, plaintext, plaintext_capacity,
        plaintext_len, timeout_ms);
    if (status == CUKTECH_COMMAND_CRYPTO_ERROR) {
        ++*decrypt_failures;
        ESP_LOGW(TAG, "command decrypt failed consecutively=%" PRIu32,
                 *decrypt_failures);
    } else if (status == CUKTECH_COMMAND_OK) {
        *decrypt_failures = 0U;
    }
    return status;
}

static bool send_get_setting(uint16_t piid, uint32_t *decrypt_failures,
                             bool *session_stale)
{
    uint8_t plaintext[32];
    size_t plaintext_len = 0U;
    uint8_t sequence = s_session.miot_sequence++;
    if (cuktech_miot_build_get(sequence, 2U, piid, plaintext,
                               sizeof(plaintext), &plaintext_len) !=
        CUKTECH_PROTOCOL_OK) {
        return false;
    }
    cuktech_command_status_t status = cuktech_command_send(
        &s_session, &COMMAND_TRANSPORT, plaintext, plaintext_len);
    if (status == CUKTECH_COMMAND_COUNTER_EXHAUSTED ||
        status == CUKTECH_COMMAND_DISCONNECTED) {
        *session_stale = true;
        return false;
    }
    if (status != CUKTECH_COMMAND_OK) {
        ESP_LOGW(TAG, "GET PIID %u send failed stage=%s", piid,
                 cuktech_command_status_name(status));
        return false;
    }

    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_COMMAND_RESPONSE_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining =
            (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                       portTICK_PERIOD_MS);
        if (remaining > BLE_COMMAND_TIMEOUT_MS) {
            remaining = BLE_COMMAND_TIMEOUT_MS;
        }
        plaintext_len = 0U;
        status = receive_and_process_plaintext(
            plaintext, sizeof(plaintext), &plaintext_len, remaining,
            decrypt_failures);
        if (*decrypt_failures >= BLE_DECRYPT_FAILURE_LIMIT ||
            status == CUKTECH_COMMAND_DISCONNECTED) {
            *session_stale = true;
            return false;
        }
        if (status == CUKTECH_COMMAND_TIMEOUT ||
            status == CUKTECH_COMMAND_CRYPTO_ERROR ||
            status == CUKTECH_COMMAND_PROTOCOL_ERROR) {
            continue;
        }
        if (status != CUKTECH_COMMAND_OK) {
            ESP_LOGW(TAG, "GET PIID %u receive failed stage=%s", piid,
                     cuktech_command_status_name(status));
            return false;
        }
        uint32_t value = 0U;
        if (cuktech_command_parse_get_result(plaintext, plaintext_len, 2U,
                                             piid, &value)) {
            charger_state_update_setting(piid, value);
            ESP_LOGD(TAG, "setting PIID %u updated", piid);
            return true;
        }
        (void)process_miot_plaintext(plaintext, plaintext_len);
    }
    ESP_LOGW(TAG, "GET PIID %u response timed out", piid);
    return false;
}

static void clear_port_target(cuktech_port_target_t target)
{
    cuktech_port_state_t cleared = {
        .protocol = CUKTECH_CHARGE_IDLE,
    };
    size_t first = target == CUKTECH_PORT_TARGET_ALL ? 0U : (size_t)target;
    size_t end = target == CUKTECH_PORT_TARGET_ALL
                     ? CHARGER_STATE_PORT_COUNT
                     : first + 1U;
    for (size_t index = first; index < end; ++index) {
        charger_state_update_port((uint8_t)(index + 1U), &cleared);
    }
}

static void clear_newly_disabled_ports(uint32_t previous_mask,
                                       uint32_t new_mask)
{
    uint32_t disabled = previous_mask & ~new_mask & 0x0fU;
    for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
        if ((disabled & (1UL << index)) != 0U) {
            clear_port_target((cuktech_port_target_t)index);
        }
    }
}

static bool send_set_setting(uint16_t piid, uint32_t value,
                             bool *session_stale)
{
    uint8_t plaintext[32];
    size_t plaintext_len = 0U;
    uint8_t sequence = s_session.miot_sequence++;
    if (cuktech_miot_build_set(sequence, 2U, piid, value, plaintext,
                               sizeof(plaintext), &plaintext_len) !=
        CUKTECH_PROTOCOL_OK) {
        return false;
    }
    charger_state_snapshot_t before;
    charger_state_get_snapshot(&before);
    cuktech_command_status_t status = cuktech_command_send(
        &s_session, &COMMAND_TRANSPORT, plaintext, plaintext_len);
    if (status == CUKTECH_COMMAND_COUNTER_EXHAUSTED ||
        status == CUKTECH_COMMAND_DISCONNECTED) {
        *session_stale = true;
        return false;
    }
    if (status != CUKTECH_COMMAND_OK) {
        ESP_LOGW(TAG, "SET PIID %u failed stage=%s", piid,
                 cuktech_command_status_name(status));
        return false;
    }
    if (!charger_state_update_setting(piid, value)) {
        return false;
    }
    if (piid == 16U) {
        uint32_t previous_mask =
            before.setting_valid[16U] ? before.settings[16U] : 0x0fU;
        clear_newly_disabled_ports(previous_mask, value);
    }
    ESP_LOGI(TAG, "SET PIID %u accepted value=%" PRIu32, piid, value);
    return true;
}

static bool execute_control_request(const ble_request_t *request,
                                    uint32_t *decrypt_failures,
                                    bool *session_stale)
{
    const cuktech_control_command_t *command = &request->data.control;
    if (command->type == CUKTECH_CONTROL_COMMAND_SET) {
        return send_set_setting(command->data.set.piid,
                                command->data.set.value, session_stale);
    }

    if (!send_get_setting(16U, decrypt_failures, session_stale)) {
        return false;
    }
    charger_state_snapshot_t snapshot;
    charger_state_get_snapshot(&snapshot);
    if (!snapshot.setting_valid[16U]) {
        return false;
    }
    uint32_t new_mask = 0U;
    if (!cuktech_control_apply_port_mask(
            snapshot.settings[16U], command->data.port.target,
            command->data.port.enabled, &new_mask)) {
        return false;
    }
    if (new_mask != snapshot.settings[16U] &&
        !send_set_setting(16U, new_mask, session_stale)) {
        return false;
    }
    if (!command->data.port.enabled) {
        clear_port_target(command->data.port.target);
    }
    ESP_LOGI(TAG, "port control target=%s action=%s mask=0x%02" PRIx32,
             cuktech_port_target_name(command->data.port.target),
             command->data.port.enabled ? "on" : "off", new_mask);
    return true;
}

static bool process_authenticated_requests(uint32_t *decrypt_failures,
                                           bool *session_stale)
{
    ble_request_t request;
    while (xQueueReceive(s_request_queue, &request, 0) == pdTRUE) {
        if (request.type == BLE_REQUEST_ENABLE) {
            portENTER_CRITICAL(&s_lock);
            s_status.enabled = request.data.enabled;
            portEXIT_CRITICAL(&s_lock);
            record_request_result(request.request_id, true, NULL);
            if (!request.data.enabled) {
                clear_port_target(CUKTECH_PORT_TARGET_ALL);
                ESP_LOGI(TAG, "BLE runtime disable request accepted id=%" PRIu32,
                         request.request_id);
                return false;
            }
            continue;
        }
        if (!ble_runtime_enabled()) {
            record_request_result(request.request_id, false, "disabled");
            continue;
        }
        bool success = execute_control_request(&request, decrypt_failures,
                                               session_stale);
        record_request_result(
            request.request_id, success,
            success ? NULL : (*session_stale ? "session_stale" : "failed"));
        if (*session_stale) {
            return false;
        }
    }
    return ble_runtime_enabled();
}

static bool process_pre_session_requests(void)
{
    ble_request_t request;
    while (xQueueReceive(s_request_queue, &request, 0) == pdTRUE) {
        if (request.type == BLE_REQUEST_ENABLE) {
            portENTER_CRITICAL(&s_lock);
            s_status.enabled = request.data.enabled;
            portEXIT_CRITICAL(&s_lock);
            record_request_result(request.request_id, true, NULL);
        } else {
            record_request_result(request.request_id, false,
                                  "not_authenticated");
        }
    }
    return ble_runtime_enabled();
}

static void wait_while_disabled(void)
{
    for (;;) {
        (void)process_pre_session_requests();
        if (ble_runtime_enabled()) {
            return;
        }
        charger_state_set_connection(false, false);
        update_status(CUKTECH_BLE_STATE_DISABLED, false, false, 0U, 0U, "");
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static bool refresh_settings(uint32_t *decrypt_failures)
{
    size_t failures = 0U;
    bool session_stale = false;
    for (size_t index = 0U;
         index < sizeof(READABLE_SETTINGS_PIIDS) /
                     sizeof(READABLE_SETTINGS_PIIDS[0]);
         ++index) {
        if (!ble_runtime_enabled()) {
            return false;
        }
        if (!send_get_setting(READABLE_SETTINGS_PIIDS[index],
                              decrypt_failures, &session_stale)) {
            ++failures;
        }
        if (session_stale) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
    ESP_LOGI(TAG, "settings refresh complete success=%u failed=%u",
             (unsigned)(sizeof(READABLE_SETTINGS_PIIDS) /
                            sizeof(READABLE_SETTINGS_PIIDS[0]) -
                        failures),
             (unsigned)failures);
    return true;
}

static bool drain_initial_pushes(uint32_t *decrypt_failures)
{
    discard_notifications_for_handle(
        s_miot_characteristics[MIOT_CHAR_COMMAND_SEND].value_handle);
    vTaskDelay(pdMS_TO_TICKS(500U));
    TickType_t started = xTaskGetTickCount();
    size_t received = 0U;
    while (received < BLE_INIT_PUSH_MAX_COUNT &&
           (xTaskGetTickCount() - started) * portTICK_PERIOD_MS <
               BLE_INIT_PUSH_MAX_MS) {
        uint8_t plaintext[CUKTECH_COMMAND_PLAINTEXT_MAX];
        size_t plaintext_len = 0U;
        cuktech_command_status_t status = receive_and_process_plaintext(
            plaintext, sizeof(plaintext), &plaintext_len, 800U,
            decrypt_failures);
        if (status == CUKTECH_COMMAND_TIMEOUT) {
            break;
        }
        if (status == CUKTECH_COMMAND_DISCONNECTED ||
            *decrypt_failures >= BLE_DECRYPT_FAILURE_LIMIT) {
            return false;
        }
        if (status == CUKTECH_COMMAND_OK) {
            ++received;
            (void)process_miot_plaintext(plaintext, plaintext_len);
        }
    }
    discard_notifications_for_handle(
        s_miot_characteristics[MIOT_CHAR_COMMAND_SEND].value_handle);
    ESP_LOGI(TAG, "processed %u authentication-time command pushes",
             (unsigned)received);
    return true;
}

static bool run_authenticated_session(void)
{
    uint32_t decrypt_failures = 0U;
    if (!drain_initial_pushes(&decrypt_failures) ||
        !refresh_settings(&decrypt_failures)) {
        if (ble_runtime_enabled()) {
            update_status(CUKTECH_BLE_STATE_ERROR,
                          s_conn_handle != BLE_HS_CONN_HANDLE_NONE, true, 0U,
                          0U,
                          decrypt_failures >= BLE_DECRYPT_FAILURE_LIMIT
                              ? "session_stale_decrypt"
                              : "command_channel_failed");
        }
        return false;
    }

    TickType_t last_refresh = xTaskGetTickCount();
    while (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        bool session_stale = false;
        if (!process_authenticated_requests(&decrypt_failures,
                                            &session_stale)) {
            return false;
        }
        uint8_t plaintext[CUKTECH_COMMAND_PLAINTEXT_MAX];
        size_t plaintext_len = 0U;
        cuktech_command_status_t status = receive_and_process_plaintext(
            plaintext, sizeof(plaintext), &plaintext_len, 1000U,
            &decrypt_failures);
        if (status == CUKTECH_COMMAND_OK) {
            (void)process_miot_plaintext(plaintext, plaintext_len);
        } else if (status == CUKTECH_COMMAND_DISCONNECTED) {
            return false;
        } else if (decrypt_failures >= BLE_DECRYPT_FAILURE_LIMIT) {
            update_status(CUKTECH_BLE_STATE_ERROR, true, true, 0U, 0U,
                          "session_stale_decrypt");
            return false;
        } else if (status != CUKTECH_COMMAND_TIMEOUT &&
                   status != CUKTECH_COMMAND_PROTOCOL_ERROR &&
                   status != CUKTECH_COMMAND_CRYPTO_ERROR) {
            ESP_LOGW(TAG, "command receive failed stage=%s",
                     cuktech_command_status_name(status));
        }
        if ((xTaskGetTickCount() - last_refresh) * portTICK_PERIOD_MS >=
            BLE_SETTINGS_REFRESH_MS) {
            if (!refresh_settings(&decrypt_failures)) {
                if (ble_runtime_enabled()) {
                    update_status(CUKTECH_BLE_STATE_ERROR, true, true, 0U,
                                  0U, "settings_refresh_failed");
                }
                return false;
            }
            last_refresh = xTaskGetTickCount();
        }
    }
    return false;
}

static bool handle_common_failure_event(const control_event_t *event,
                                        const char *stage)
{
    if (event->type == CONTROL_EVENT_DISCONNECTED) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        charger_state_set_connection(false, false);
        update_status(CUKTECH_BLE_STATE_ERROR, false, false, 0U, 0U,
                      "peer_disconnected");
        ESP_LOGW(TAG, "%s interrupted by disconnect reason=%d", stage,
                 event->status);
        return true;
    }
    if (event->type == CONTROL_EVENT_HOST_RESET) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        charger_state_set_connection(false, false);
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
        if (!wait_control_event_unconditional(&event, remaining)) {
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
            charger_state_set_connection(true, false);
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
    if (!ble_runtime_enabled()) {
        return false;
    }
    ESP_LOGI(TAG, "charger not advertising; trying bounded direct connections");
    ble_addr_t direct = s_target_address;
    direct.type = BLE_ADDR_PUBLIC;
    discard_queued_events();
    if (connect_to_address(&direct)) {
        return true;
    }
    if (!ble_runtime_enabled()) {
        return false;
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
    memset(s_miot_characteristics, 0, sizeof(s_miot_characteristics));

    if (!exchange_mtu() || !discover_service(&service_start, &service_end) ||
        !discover_characteristics(service_start, service_end, characteristics,
                                  &characteristic_count,
                                  s_miot_characteristics)) {
        return false;
    }
    update_status(CUKTECH_BLE_STATE_DISCOVERING_DESCRIPTORS, true, false,
                  0U, 0U, "");
    for (size_t index = 0U;
         index < sizeof(SUBSCRIBE_TARGETS) / sizeof(SUBSCRIBE_TARGETS[0]);
         ++index) {
        if (!discover_cccd(characteristics, characteristic_count, service_end,
                           &s_miot_characteristics[SUBSCRIBE_TARGETS[index]])) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_SUBSCRIBING, true, false, 0U, 0U, "");
    for (size_t index = 0U;
         index < sizeof(SUBSCRIBE_TARGETS) / sizeof(SUBSCRIBE_TARGETS[0]);
         ++index) {
        if (!subscribe_characteristic(
                &s_miot_characteristics[SUBSCRIBE_TARGETS[index]])) {
            return false;
        }
    }
    update_status(CUKTECH_BLE_STATE_READY, true, true, 0U, 0U, "");
    ESP_LOGI(TAG, "GATT link ready; five MiOT notification channels enabled");
    return true;
}

static void copy_ascii_field(char *output, size_t output_size,
                             const uint8_t *input, size_t input_len)
{
    if (output == NULL || output_size == 0U) {
        return;
    }
    size_t written = 0U;
    while (written < input_len && written + 1U < output_size &&
           input[written] != 0U) {
        uint8_t byte = input[written];
        output[written] = byte >= 0x20U && byte <= 0x7eU ? (char)byte : '?';
        ++written;
    }
    output[written] = '\0';
}

static bool query_device_info(char *device_model, size_t device_model_size)
{
    uint16_t handle =
        s_miot_characteristics[MIOT_CHAR_DEVICE_INFO].value_handle;
    if (handle == 0U || device_model == NULL || device_model_size == 0U) {
        return false;
    }
    const uint8_t chip_query = 0x03U;
    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, handle, &chip_query,
                                         sizeof(chip_query));
    if (rc != 0) {
        ESP_LOGW(TAG, "device info query failed rc=%d", rc);
        return false;
    }
    uint8_t response[BLE_NOTIFY_MAX_PAYLOAD];
    size_t response_len = 0U;
    if (receive_notification_for_handle(handle, response, sizeof(response),
                                        &response_len,
                                        BLE_COMMAND_TIMEOUT_MS) !=
            CUKTECH_COMMAND_IO_OK ||
        response_len < 3U) {
        ESP_LOGW(TAG, "device info response unavailable");
        return false;
    }
    size_t chip_len = response[1];
    if (chip_len > response_len - 2U) {
        chip_len = response_len - 2U;
    }
    char chip[32];
    copy_ascii_field(chip, sizeof(chip), response + 2U, chip_len);
    if (chip[0] == '\0') {
        return false;
    }
    snprintf(device_model, device_model_size, "njcuk.fitting.ad1204_%s", chip);
    return true;
}

static bool read_firmware_version(char *firmware, size_t firmware_size)
{
    uint16_t handle = s_miot_characteristics[MIOT_CHAR_FIRMWARE].value_handle;
    if (handle == 0U || firmware == NULL || firmware_size == 0U) {
        return false;
    }
    int rc = ble_gattc_read(s_conn_handle, handle, read_callback,
                            (void *)(uintptr_t)handle);
    if (rc != 0) {
        ESP_LOGW(TAG, "firmware read start failed rc=%d", rc);
        return false;
    }
    control_event_t event;
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(BLE_COMMAND_TIMEOUT_MS);
    while (xTaskGetTickCount() - started < timeout) {
        uint32_t remaining =
            (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                       portTICK_PERIOD_MS);
        if (!wait_control_event(&event, remaining)) {
            break;
        }
        if (event.type == CONTROL_EVENT_READ_DONE &&
            event.data.read.attr_handle == handle) {
            if (event.status != 0) {
                ESP_LOGW(TAG, "firmware read failed status=%d", event.status);
                return false;
            }
            copy_ascii_field(firmware, firmware_size, event.data.read.bytes,
                             event.data.read.length);
            return firmware[0] != '\0';
        }
        if (handle_common_failure_event(&event, "firmware_read")) {
            return false;
        }
    }
    ESP_LOGW(TAG, "firmware read timed out");
    return false;
}

static void read_and_store_device_info(void)
{
    char device_model[CHARGER_STATE_DEVICE_MODEL_MAX_LEN + 1U] = {0};
    char firmware[CHARGER_STATE_FIRMWARE_MAX_LEN + 1U] = {0};
    bool model_ok = query_device_info(device_model, sizeof(device_model));
    bool firmware_ok = read_firmware_version(firmware, sizeof(firmware));
    charger_state_set_device_info(model_ok ? device_model : "",
                                  firmware_ok ? firmware : "");
    ESP_LOGI(TAG, "charger info model=%s firmware=%s",
             model_ok ? device_model : "unknown",
             firmware_ok ? firmware : "unknown");
}

static void disable_notifications_best_effort(void)
{
    const uint8_t disable_notifications[2] = {0x00, 0x00};
    for (size_t index = 0U;
         index < sizeof(SUBSCRIBE_TARGETS) / sizeof(SUBSCRIBE_TARGETS[0]);
         ++index) {
        uint16_t cccd_handle =
            s_miot_characteristics[SUBSCRIBE_TARGETS[index]].cccd_handle;
        if (cccd_handle == 0U || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            continue;
        }
        int rc = ble_gattc_write_flat(
            s_conn_handle, cccd_handle, disable_notifications,
            sizeof(disable_notifications), write_callback,
            (void *)(uintptr_t)cccd_handle);
        if (rc != 0) {
            ESP_LOGW(TAG, "CCCD cleanup start failed rc=%d", rc);
            continue;
        }
        control_event_t event;
        TickType_t started = xTaskGetTickCount();
        TickType_t timeout = pdMS_TO_TICKS(1000U);
        while (xTaskGetTickCount() - started < timeout) {
            uint32_t remaining =
                (uint32_t)((timeout - (xTaskGetTickCount() - started)) *
                           portTICK_PERIOD_MS);
            if (!wait_control_event_unconditional(&event, remaining)) {
                break;
            }
            if (event.type == CONTROL_EVENT_WRITE_DONE &&
                event.data.write.attr_handle == cccd_handle) {
                if (event.status != 0) {
                    ESP_LOGW(TAG, "CCCD cleanup failed status=%d",
                             event.status);
                }
                break;
            }
            if (event.type == CONTROL_EVENT_DISCONNECTED) {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                return;
            }
        }
    }
}

static void disconnect_cleanly(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        charger_state_set_connection(false, false);
        cuktech_session_clear(&s_session);
        memset(s_miot_characteristics, 0, sizeof(s_miot_characteristics));
        update_status(CUKTECH_BLE_STATE_DISCONNECTING, false, false, 0U, 0U,
                      NULL);
        return;
    }
    uint16_t handle = s_conn_handle;
    update_status(CUKTECH_BLE_STATE_DISCONNECTING, true, false, 0U, 0U, NULL);
    disable_notifications_best_effort();
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        charger_state_set_connection(false, false);
        cuktech_session_clear(&s_session);
        memset(s_miot_characteristics, 0, sizeof(s_miot_characteristics));
        update_status(CUKTECH_BLE_STATE_DISCONNECTING, false, false, 0U, 0U,
                      NULL);
        discard_queued_events();
        return;
    }
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
            if (!wait_control_event_unconditional(&event, remaining)) {
                break;
            }
            if (event.type == CONTROL_EVENT_DISCONNECTED &&
                event.conn_handle == handle) {
                break;
            }
        }
    }
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    charger_state_set_connection(false, false);
    cuktech_session_clear(&s_session);
    memset(s_miot_characteristics, 0, sizeof(s_miot_characteristics));
    update_status(CUKTECH_BLE_STATE_DISCONNECTING, false, false, 0U, 0U, NULL);
    discard_queued_events();
}

static void retry_delay(uint32_t seconds)
{
    update_status(CUKTECH_BLE_STATE_BACKOFF, false, false, 0U, seconds, NULL);
    ESP_LOGI(TAG, "BLE retry in %" PRIu32 " seconds", seconds);
    TickType_t started = xTaskGetTickCount();
    TickType_t delay = pdMS_TO_TICKS(seconds * 1000U);
    while (ble_runtime_enabled()) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= delay) {
            break;
        }
        ulTaskNotifyTake(pdTRUE, delay - elapsed);
        (void)process_pre_session_requests();
    }
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
    uint32_t authentication_failures = 0U;
    for (;;) {
        wait_while_disabled();
        bool authentication_failed = false;
        discard_queued_events();
        bool link_ready = scan_then_connect();
        if (!ble_runtime_enabled()) {
            disconnect_cleanly();
            authentication_failures = 0U;
            backoff = 0U;
            continue;
        }
        link_ready = link_ready && prepare_gatt_link();
        if (!ble_runtime_enabled()) {
            disconnect_cleanly();
            authentication_failures = 0U;
            backoff = 0U;
            continue;
        }
        if (link_ready) {
            read_and_store_device_info();
            cuktech_miot_auth_status_t auth_status =
                authenticate_link(authentication_failures);
            if (auth_status == CUKTECH_MIOT_AUTH_OK) {
                authentication_failures = 0U;
                backoff = 0U;
                (void)run_authenticated_session();
                disconnect_cleanly();
            } else {
                authentication_failed = true;
                if (authentication_status_counts_failure(auth_status)) {
                    ++authentication_failures;
                }
                disconnect_cleanly();
                if (authentication_failures >= BLE_AUTH_FAILURE_LOCK_COUNT) {
                    update_status(CUKTECH_BLE_STATE_AUTH_FAILED_LOCKED, false,
                                  false, 0U, 0U, NULL);
                    update_authentication_status(false,
                                                 authentication_failures,
                                                 NULL);
                    ESP_LOGE(TAG,
                             "MiOT authentication locked after %" PRIu32
                             " consecutive failures; reboot or manual retry required",
                             authentication_failures);
                    while (ble_runtime_enabled()) {
                        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000U));
                        (void)process_pre_session_requests();
                    }
                    authentication_failures = 0U;
                    backoff = 0U;
                    continue;
                }
            }
        } else {
            disconnect_cleanly();
        }
        if (!ble_runtime_enabled()) {
            authentication_failures = 0U;
            backoff = 0U;
            continue;
        }
        backoff = cuktech_ble_next_backoff(backoff, BLE_RETRY_MAX_SECONDS);
        if (authentication_failed && backoff < BLE_AUTH_MIN_RETRY_SECONDS) {
            backoff = BLE_AUTH_MIN_RETRY_SECONDS;
        }
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
    uint8_t display_order[6];
    if (!config->token_configured ||
        !cuktech_ble_parse_mac(config->charger_mac, display_order)) {
        s_status.state = config->ble_enabled
                             ? CUKTECH_BLE_STATE_WAITING_CONFIG
                             : CUKTECH_BLE_STATE_DISABLED;
        if (config->ble_enabled) {
            snprintf(s_status.last_error, sizeof(s_status.last_error),
                     "charger_mac_and_token_required");
            ESP_LOGW(TAG, "BLE waiting for a valid charger MAC and Token");
        } else {
            ESP_LOGI(TAG, "BLE disabled; charger credentials not loaded");
        }
        return ESP_OK;
    }
    s_target_address.type = BLE_ADDR_PUBLIC;
    cuktech_ble_mac_to_nimble(display_order, s_target_address.val);
    memcpy(s_token, config->token, sizeof(s_token));

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
    s_request_queue = xQueueCreate(BLE_REQUEST_QUEUE_DEPTH,
                                   sizeof(ble_request_t));
    if (s_control_queue == NULL || s_notify_queue == NULL ||
        s_request_queue == NULL) {
        if (s_control_queue != NULL) {
            vQueueDelete(s_control_queue);
            s_control_queue = NULL;
        }
        if (s_notify_queue != NULL) {
            vQueueDelete(s_notify_queue);
            s_notify_queue = NULL;
        }
        if (s_request_queue != NULL) {
            vQueueDelete(s_request_queue);
            s_request_queue = NULL;
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
        vQueueDelete(s_request_queue);
        s_notify_queue = NULL;
        s_control_queue = NULL;
        s_request_queue = NULL;
        nimble_port_deinit();
        s_status.state = CUKTECH_BLE_STATE_ERROR;
        snprintf(s_status.last_error, sizeof(s_status.last_error),
                 "task_allocation_failed");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    update_status(config->ble_enabled ? CUKTECH_BLE_STATE_HOST_SYNC
                                      : CUKTECH_BLE_STATE_DISABLED,
                  false, false, 0U, 0U, "");
    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "NimBLE Central started for charger MAC %s enabled=%s",
             config->charger_mac, config->ble_enabled ? "yes" : "no");
    return ESP_OK;
}
