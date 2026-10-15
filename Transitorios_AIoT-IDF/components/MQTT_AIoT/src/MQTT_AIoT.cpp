/*
 * File: components/MQTT_AIoT/src/MQTT_AIoT.cpp
 * Description: MQTT client library for ESP-IDF (esp-mqtt).
 *              1. C++ class MQTT_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of MQTT_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "MQTT_AIoT.h"

#include <cstdio>
#include <cstring>
#include <new>
#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "MQTT_AIoT";

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class MQTT_AIoT {
private:
    struct Subscription {
        char topic[MQTT_AIOT_TOPIC_LEN];
        int  qos;
        bool used;
    };

    char                     _uri[128];
    char                     _client_id[64];
    char                     _username[64];
    char                     _password[64];
    MQTT_AIoT_message_cb_t   _cb;
    void                    *_cb_ctx;
    esp_mqtt_client_handle_t _client;        // nullptr until begin()
    volatile bool            _connected;
    Subscription             _subs[MQTT_AIOT_MAX_TOPICS];
    SemaphoreHandle_t        _lock;          // protects _subs

    // Reassembly of messages that arrive in several fragments
    char                     _rx_topic[MQTT_AIOT_TOPIC_LEN];
    uint8_t                 *_rx_buf;
    size_t                   _rx_len;

    // Repeats every remembered subscription (after each connection)
    void resubscribeAll() {
        xSemaphoreTake(_lock, portMAX_DELAY);
        for (int i = 0; i < MQTT_AIOT_MAX_TOPICS; i++) {
            if (_subs[i].used) {
                esp_mqtt_client_subscribe_single(_client, _subs[i].topic, _subs[i].qos);
            }
        }
        xSemaphoreGive(_lock);
    }

    // Collects the fragments of one message and calls the user callback
    void onData(const esp_mqtt_event_handle_t ev) {
        if (ev->current_data_offset == 0) {
            int tlen = (ev->topic_len < (MQTT_AIOT_TOPIC_LEN - 1)) ? ev->topic_len : (MQTT_AIOT_TOPIC_LEN - 1);
            if (tlen < 0) tlen = 0;
            memcpy(_rx_topic, ev->topic, (size_t)tlen);
            _rx_topic[tlen] = '\0';
            _rx_len = 0;
        }
        if (_rx_buf != nullptr && ev->data_len > 0) {
            size_t room  = MQTT_AIOT_MAX_MESSAGE - _rx_len;
            size_t chunk = ((size_t)ev->data_len < room) ? (size_t)ev->data_len : room;
            memcpy(_rx_buf + _rx_len, ev->data, chunk);
            _rx_len += chunk;
        }
        const bool complete = (ev->current_data_offset + ev->data_len) >= ev->total_data_len;
        if (complete && _cb != nullptr) {
            _cb(_rx_topic, _rx_buf, _rx_len, _cb_ctx);
        }
    }

    // esp-mqtt event handler (MQTT task)
    static void onEvent(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
        (void)base;
        MQTT_AIoT *self = static_cast<MQTT_AIoT *>(arg);
        esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(event_data);
        switch ((esp_mqtt_event_id_t)event_id) {
            case MQTT_EVENT_CONNECTED:
                self->_connected = true;
                ESP_LOGI(TAG, "Connected to %s", self->_uri);
                self->resubscribeAll();
                break;
            case MQTT_EVENT_DISCONNECTED:
                if (self->_connected) {
                    ESP_LOGW(TAG, "Disconnected, retrying automatically");
                }
                self->_connected = false;
                break;
            case MQTT_EVENT_DATA:
                self->onData(ev);
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGW(TAG, "MQTT error event");
                break;
            default:
                break;
        }
    }

public:
    // Constructor: copies the configuration only
    MQTT_AIoT(const char *uri, const char *client_id)
        : _cb(nullptr), _cb_ctx(nullptr), _client(nullptr), _connected(false),
          _lock(xSemaphoreCreateMutex()), _rx_buf(nullptr), _rx_len(0) {
        snprintf(_uri, sizeof(_uri), "%s", uri);
        snprintf(_client_id, sizeof(_client_id), "%s", client_id ? client_id : "");
        _username[0] = '\0';
        _password[0] = '\0';
        _rx_topic[0] = '\0';
        memset(_subs, 0, sizeof(_subs));
        _rx_buf = static_cast<uint8_t *>(heap_caps_malloc(MQTT_AIOT_MAX_MESSAGE, MALLOC_CAP_8BIT));
    }

    // Destructor: stops only what this object started
    ~MQTT_AIoT() {
        if (_client != nullptr) {
            esp_mqtt_client_stop(_client);
            esp_mqtt_client_destroy(_client);
        }
        if (_rx_buf != nullptr) heap_caps_free(_rx_buf);
        if (_lock != nullptr) vSemaphoreDelete(_lock);
    }

    bool isValid() const { return _lock != nullptr && _rx_buf != nullptr; }

    esp_err_t setCredentials(const char *user, const char *pass) {
        if (_client != nullptr) return ESP_ERR_INVALID_STATE;
        snprintf(_username, sizeof(_username), "%s", user ? user : "");
        snprintf(_password, sizeof(_password), "%s", pass ? pass : "");
        return ESP_OK;
    }

    void setCallback(MQTT_AIoT_message_cb_t cb, void *ctx) {
        _cb     = cb;
        _cb_ctx = ctx;
    }

    // Starts the client (connects in the background)
    esp_err_t begin() {
        if (_client != nullptr) {
            return ESP_OK;
        }
        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = _uri;
        if (_client_id[0] != '\0') cfg.credentials.client_id = _client_id;
        if (_username[0] != '\0')  cfg.credentials.username  = _username;
        if (_password[0] != '\0')  cfg.credentials.authentication.password = _password;
        cfg.session.keepalive = 30;
        cfg.network.reconnect_timeout_ms = 5000;

        _client = esp_mqtt_client_init(&cfg);
        if (_client == nullptr) {
            ESP_LOGE(TAG, "esp_mqtt_client_init failed (check the URI: %s)", _uri);
            return ESP_FAIL;
        }
        esp_err_t err = esp_mqtt_client_register_event(_client, MQTT_EVENT_ANY, &MQTT_AIoT::onEvent, this);
        if (err == ESP_OK) {
            err = esp_mqtt_client_start(_client);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MQTT start error: %s", esp_err_to_name(err));
            esp_mqtt_client_destroy(_client);
            _client = nullptr;
            return err;
        }
        ESP_LOGI(TAG, "MQTT client started -> %s", _uri);
        return ESP_OK;
    }

    bool connected() const { return _connected; }

    int publish(const char *topic, const void *data, size_t len, int qos, bool retain) {
        if (_client == nullptr || !_connected || topic == nullptr || qos < 0 || qos > 2) {
            return -1;
        }
        return esp_mqtt_client_publish(_client, topic, static_cast<const char *>(data),
                                       (int)len, qos, retain ? 1 : 0);
    }

    esp_err_t subscribe(const char *topic, int qos) {
        if (topic == nullptr || strlen(topic) >= MQTT_AIOT_TOPIC_LEN || qos < 0 || qos > 2) {
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        int slot = -1;
        for (int i = 0; i < MQTT_AIOT_MAX_TOPICS; i++) {
            if (_subs[i].used && strcmp(_subs[i].topic, topic) == 0) { slot = i; break; }
            if (!_subs[i].used && slot < 0) slot = i;
        }
        esp_err_t err = ESP_OK;
        if (slot < 0) {
            err = ESP_ERR_NO_MEM;            // list full
        } else {
            snprintf(_subs[slot].topic, MQTT_AIOT_TOPIC_LEN, "%s", topic);
            _subs[slot].qos  = qos;
            _subs[slot].used = true;
        }
        xSemaphoreGive(_lock);
        if (err == ESP_OK && _client != nullptr && _connected) {
            if (esp_mqtt_client_subscribe_single(_client, topic, qos) < 0) {
                err = ESP_FAIL;
            }
        }
        return err;
    }

    esp_err_t unsubscribe(const char *topic) {
        if (topic == nullptr) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        for (int i = 0; i < MQTT_AIOT_MAX_TOPICS; i++) {
            if (_subs[i].used && strcmp(_subs[i].topic, topic) == 0) {
                _subs[i].used = false;
            }
        }
        xSemaphoreGive(_lock);
        if (_client != nullptr && _connected) {
            return (esp_mqtt_client_unsubscribe(_client, topic) >= 0) ? ESP_OK : ESP_FAIL;
        }
        return ESP_OK;
    }
};

// Handle <-> object conversion
static inline MQTT_AIoT *toObj(MQTT_AIoT_handle_t handle) {
    return reinterpret_cast<MQTT_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

MQTT_AIoT_handle_t MQTT_AIoT_create(const char *broker_uri, const char *client_id) {
    if (broker_uri == nullptr || strlen(broker_uri) < 8 || strlen(broker_uri) >= 128) {
        ESP_LOGE(TAG, "Invalid broker URI");
        return nullptr;
    }
    MQTT_AIoT *obj = new (std::nothrow) MQTT_AIoT(broker_uri, client_id);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for MQTT object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<MQTT_AIoT_handle_t>(obj);
}

esp_err_t MQTT_AIoT_set_credentials(MQTT_AIoT_handle_t handle, const char *username, const char *password) {
    MQTT_AIoT *obj = toObj(handle);
    return obj ? obj->setCredentials(username, password) : ESP_ERR_INVALID_ARG;
}

esp_err_t MQTT_AIoT_set_callback(MQTT_AIoT_handle_t handle, MQTT_AIoT_message_cb_t cb, void *user_ctx) {
    MQTT_AIoT *obj = toObj(handle);
    if (obj == nullptr) return ESP_ERR_INVALID_ARG;
    obj->setCallback(cb, user_ctx);
    return ESP_OK;
}

esp_err_t MQTT_AIoT_init(MQTT_AIoT_handle_t handle) {
    MQTT_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void MQTT_AIoT_destroy(MQTT_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

bool MQTT_AIoT_is_connected(MQTT_AIoT_handle_t handle) {
    MQTT_AIoT *obj = toObj(handle);
    return obj ? obj->connected() : false;
}

int MQTT_AIoT_publish(MQTT_AIoT_handle_t handle, const char *topic,
                      const void *data, size_t len, int qos, bool retain) {
    MQTT_AIoT *obj = toObj(handle);
    return obj ? obj->publish(topic, data, len, qos, retain) : -1;
}

int MQTT_AIoT_publish_str(MQTT_AIoT_handle_t handle, const char *topic,
                          const char *text, int qos, bool retain) {
    MQTT_AIoT *obj = toObj(handle);
    if (obj == nullptr || text == nullptr) return -1;
    return obj->publish(topic, text, strlen(text), qos, retain);
}

esp_err_t MQTT_AIoT_subscribe(MQTT_AIoT_handle_t handle, const char *topic, int qos) {
    MQTT_AIoT *obj = toObj(handle);
    return obj ? obj->subscribe(topic, qos) : ESP_ERR_INVALID_ARG;
}

esp_err_t MQTT_AIoT_unsubscribe(MQTT_AIoT_handle_t handle, const char *topic) {
    MQTT_AIoT *obj = toObj(handle);
    return obj ? obj->unsubscribe(topic) : ESP_ERR_INVALID_ARG;
}

} // extern "C"
