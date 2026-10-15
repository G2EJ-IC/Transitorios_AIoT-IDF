/*
 * File: components/ESPNOW_AIoT/src/ESPNOW_AIoT.cpp
 * Description: ESP-NOW library for ESP-IDF.
 *              1. C++ class ESPNOW_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of ESPNOW_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "ESPNOW_AIoT.h"

#include <cstring>
#include <new>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "ESPNOW_AIoT";

static const uint8_t kBroadcastMac[ESPNOW_AIOT_MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class ESPNOW_AIoT {
private:
    uint8_t           _channel;
    uint16_t          _rx_queue_len;
    QueueHandle_t     _rx_queue;             // received packets
    SemaphoreHandle_t _tx_done;              // given by the send callback
    SemaphoreHandle_t _tx_lock;              // one send at a time
    volatile bool     _tx_ok;
    volatile uint32_t _dropped;
    bool              _started;
    bool              _wifi_started_here;    // WiFi started by this object

    // Single instance: the ESP-NOW callbacks are global
    static ESPNOW_AIoT *s_instance;

    // Receive callback (WiFi task, core 0): copy and return immediately
    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
        ESPNOW_AIoT *self = s_instance;
        if (self == nullptr || info == nullptr || data == nullptr || len <= 0) {
            return;
        }
        ESPNOW_AIoT_packet_t pkt;
        memcpy(pkt.mac, info->src_addr, ESPNOW_AIOT_MAC_LEN);
        pkt.rssi = (info->rx_ctrl != nullptr) ? (int8_t)info->rx_ctrl->rssi : 0;
        pkt.len  = (uint16_t)((len > ESPNOW_AIOT_MAX_PAYLOAD) ? ESPNOW_AIOT_MAX_PAYLOAD : len);
        memcpy(pkt.data, data, pkt.len);
        if (xQueueSend(self->_rx_queue, &pkt, 0) != pdTRUE) {
            self->_dropped = self->_dropped + 1;   // queue full: packet lost
        }
    }

    // Send callback (WiFi task): delivery report of the last packet
    static void onSend(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
        (void)tx_info;
        ESPNOW_AIoT *self = s_instance;
        if (self == nullptr) {
            return;
        }
        self->_tx_ok = (status == ESP_NOW_SEND_SUCCESS);
        xSemaphoreGive(self->_tx_done);
    }

    // Starts WiFi in station mode when nobody started it before (sensor node)
    esp_err_t startWifiIfNeeded() {
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) == ESP_OK) {
            // WiFi already initialized by the application: keep its channel
            uint8_t primary = 0;
            wifi_second_chan_t second;
            if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0) {
                _channel = primary;
            }
            return ESP_OK;
        }

        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_start();
        if (err == ESP_OK) err = esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi start for ESP-NOW failed: %s", esp_err_to_name(err));
            return err;
        }
        _wifi_started_here = true;
        ESP_LOGI(TAG, "WiFi started (station, channel %u) for ESP-NOW", (unsigned)_channel);
        return ESP_OK;
    }

public:
    // Constructor: creates the RTOS objects only
    ESPNOW_AIoT(uint8_t channel, uint16_t rx_queue_len)
        : _channel(channel), _rx_queue_len(rx_queue_len),
          _rx_queue(xQueueCreate(rx_queue_len, sizeof(ESPNOW_AIoT_packet_t))),
          _tx_done(xSemaphoreCreateBinary()), _tx_lock(xSemaphoreCreateMutex()),
          _tx_ok(false), _dropped(0), _started(false), _wifi_started_here(false) {
        if (s_instance == nullptr) {
            s_instance = this;               // claim the single ESP-NOW slot
        }
    }

    // Destructor: stops only what this object started
    ~ESPNOW_AIoT() {
        if (_started) {
            esp_now_unregister_recv_cb();
            esp_now_unregister_send_cb();
            esp_now_deinit();
        }
        if (_wifi_started_here) {
            esp_wifi_stop();
            esp_wifi_deinit();
        }
        if (s_instance == this) {
            s_instance = nullptr;
        }
        if (_rx_queue != nullptr) vQueueDelete(_rx_queue);
        if (_tx_done != nullptr)  vSemaphoreDelete(_tx_done);
        if (_tx_lock != nullptr)  vSemaphoreDelete(_tx_lock);
    }

    bool isValid() const {
        return _rx_queue != nullptr && _tx_done != nullptr && _tx_lock != nullptr;
    }

    static bool exists() { return s_instance != nullptr; }

    // Radio initialization
    esp_err_t begin() {
        if (_started) {
            return ESP_OK;
        }
        esp_err_t err = startWifiIfNeeded();
        if (err != ESP_OK) {
            return err;
        }

        err = esp_now_init();
        if (err == ESP_OK) err = esp_now_register_recv_cb(&ESPNOW_AIoT::onReceive);
        if (err == ESP_OK) err = esp_now_register_send_cb(&ESPNOW_AIoT::onSend);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
            esp_now_deinit();
            return err;
        }
        _started = true;

        // Broadcast peer is always available
        (void)addPeer(kBroadcastMac);

        uint8_t mac[ESPNOW_AIOT_MAC_LEN];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        ESP_LOGI(TAG, "ESP-NOW started -> MAC " MACSTR ", channel %u, RX queue %u",
                 MAC2STR(mac), (unsigned)_channel, (unsigned)_rx_queue_len);
        return ESP_OK;
    }

    esp_err_t addPeer(const uint8_t *mac) {
        if (!_started || mac == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        if (esp_now_is_peer_exist(mac)) {
            return ESP_OK;
        }
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, ESPNOW_AIOT_MAC_LEN);
        peer.channel = 0;                    // 0 = current channel
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = false;
        return esp_now_add_peer(&peer);
    }

    esp_err_t removePeer(const uint8_t *mac) {
        if (!_started || mac == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        return esp_now_del_peer(mac);
    }

    // Sends and waits for the delivery report
    esp_err_t send(const uint8_t *mac, const void *data, size_t len) {
        if (!_started) {
            return ESP_ERR_INVALID_STATE;
        }
        if (data == nullptr || len == 0 || len > ESPNOW_AIOT_MAX_PAYLOAD) {
            return ESP_ERR_INVALID_ARG;
        }
        const uint8_t *dest = (mac != nullptr) ? mac : kBroadcastMac;

        xSemaphoreTake(_tx_lock, portMAX_DELAY);
        xSemaphoreTake(_tx_done, 0);         // discard a stale report
        esp_err_t err = esp_now_send(dest, static_cast<const uint8_t *>(data), len);
        if (err == ESP_OK) {
            if (xSemaphoreTake(_tx_done, pdMS_TO_TICKS(ESPNOW_AIOT_SEND_TIMEOUT_MS)) != pdTRUE) {
                err = ESP_ERR_TIMEOUT;
            } else if (!_tx_ok) {
                err = ESP_FAIL;              // no ACK from the receiver
            }
        }
        xSemaphoreGive(_tx_lock);
        return err;
    }

    esp_err_t receive(ESPNOW_AIoT_packet_t *packet, uint32_t timeout_ms) {
        if (packet == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }
        const TickType_t wait = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
        return (xQueueReceive(_rx_queue, packet, wait) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    uint8_t channel() const { return _channel; }
    uint32_t dropped() const { return _dropped; }
};

ESPNOW_AIoT *ESPNOW_AIoT::s_instance = nullptr;

// Handle <-> object conversion
static inline ESPNOW_AIoT *toObj(ESPNOW_AIoT_handle_t handle) {
    return reinterpret_cast<ESPNOW_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

ESPNOW_AIoT_handle_t ESPNOW_AIoT_create(uint8_t channel, uint16_t rx_queue_len) {
    if (channel < 1 || channel > 13 || rx_queue_len == 0) {
        ESP_LOGE(TAG, "Invalid arguments (channel %u, queue %u)", (unsigned)channel, (unsigned)rx_queue_len);
        return nullptr;
    }
    if (ESPNOW_AIoT::exists()) {
        ESP_LOGE(TAG, "Only one ESP-NOW object can exist");
        return nullptr;
    }
    ESPNOW_AIoT *obj = new (std::nothrow) ESPNOW_AIoT(channel, rx_queue_len);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for ESP-NOW object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<ESPNOW_AIoT_handle_t>(obj);
}

esp_err_t ESPNOW_AIoT_init(ESPNOW_AIoT_handle_t handle) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void ESPNOW_AIoT_destroy(ESPNOW_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

esp_err_t ESPNOW_AIoT_add_peer(ESPNOW_AIoT_handle_t handle, const uint8_t mac[ESPNOW_AIOT_MAC_LEN]) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->addPeer(mac) : ESP_ERR_INVALID_ARG;
}

esp_err_t ESPNOW_AIoT_remove_peer(ESPNOW_AIoT_handle_t handle, const uint8_t mac[ESPNOW_AIOT_MAC_LEN]) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->removePeer(mac) : ESP_ERR_INVALID_ARG;
}

esp_err_t ESPNOW_AIoT_send(ESPNOW_AIoT_handle_t handle, const uint8_t *mac, const void *data, size_t len) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->send(mac, data, len) : ESP_ERR_INVALID_ARG;
}

esp_err_t ESPNOW_AIoT_receive(ESPNOW_AIoT_handle_t handle, ESPNOW_AIoT_packet_t *packet, uint32_t timeout_ms) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->receive(packet, timeout_ms) : ESP_ERR_INVALID_ARG;
}

esp_err_t ESPNOW_AIoT_get_own_mac(uint8_t mac[ESPNOW_AIOT_MAC_LEN]) {
    return (mac != nullptr) ? esp_read_mac(mac, ESP_MAC_WIFI_STA) : ESP_ERR_INVALID_ARG;
}

uint8_t ESPNOW_AIoT_get_channel(ESPNOW_AIoT_handle_t handle) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->channel() : 0;
}

uint32_t ESPNOW_AIoT_get_dropped(ESPNOW_AIoT_handle_t handle) {
    ESPNOW_AIoT *obj = toObj(handle);
    return obj ? obj->dropped() : 0;
}

} // extern "C"
