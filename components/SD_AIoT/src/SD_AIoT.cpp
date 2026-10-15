/*
 * File: components/SD_AIoT/src/SD_AIoT.cpp
 * Description: microSD library for ESP-IDF (SPI mode + FAT file system).
 *              1. C++ class SD_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of SD_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "SD_AIoT.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "SD_AIoT";

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class SD_AIoT {
private:
    spi_host_device_t _host;
    int               _mosi_pin;
    int               _miso_pin;
    int               _sclk_pin;
    int               _cs_pin;
    int               _freq_khz;
    char              _mount[32];
    sdmmc_card_t     *_card;                 // nullptr until mounted
    SemaphoreHandle_t _lock;

    // Relative names go under the mount point: "log.csv" -> "/sdcard/log.csv"
    bool buildPath(const char *name, char *out, size_t out_len) const {
        if (name == nullptr || name[0] == '\0') {
            return false;
        }
        int n;
        if (strncmp(name, _mount, strlen(_mount)) == 0) {
            n = snprintf(out, out_len, "%s", name);
        } else {
            n = snprintf(out, out_len, "%s/%s", _mount, (name[0] == '/') ? name + 1 : name);
        }
        return (n > 0 && (size_t)n < out_len);
    }

public:
    // Constructor: stores the configuration only
    SD_AIoT(int host, int mosi, int miso, int sclk, int cs, const char *mount_point)
        : _host((spi_host_device_t)host), _mosi_pin(mosi), _miso_pin(miso), _sclk_pin(sclk),
          _cs_pin(cs), _freq_khz(SD_AIOT_DEFAULT_FREQ_KHZ), _card(nullptr),
          _lock(xSemaphoreCreateMutex()) {
        snprintf(_mount, sizeof(_mount), "%s", mount_point);
    }

    // Destructor: unmounts only what this object mounted (bus left as is)
    ~SD_AIoT() {
        if (_card != nullptr) {
            esp_vfs_fat_sdcard_unmount(_mount, _card);
            ESP_LOGI(TAG, "Card unmounted from %s", _mount);
        }
        if (_lock != nullptr) vSemaphoreDelete(_lock);
    }

    bool isValid() const { return _lock != nullptr; }
    bool mounted() const { return _card != nullptr; }
    const char *mountPoint() const { return _mount; }

    esp_err_t setFreq(int khz) {
        if (khz < 400 || khz > 40000) return ESP_ERR_INVALID_ARG;
        if (_card != nullptr) return ESP_ERR_INVALID_STATE;
        _freq_khz = khz;
        return ESP_OK;
    }

    // Bus (shared if already initialized) + FAT mount
    esp_err_t begin() {
        if (_card != nullptr) {
            return ESP_OK;
        }

        spi_bus_config_t bus = {};
        bus.mosi_io_num     = _mosi_pin;
        bus.miso_io_num     = _miso_pin;
        bus.sclk_io_num     = _sclk_pin;
        bus.quadwp_io_num   = -1;
        bus.quadhd_io_num   = -1;
        bus.max_transfer_sz = 4092;
        esp_err_t err = spi_bus_initialize(_host, &bus, SPI_DMA_CH_AUTO);
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "SPI%d already initialized, sharing the bus", (int)_host + 1);
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus error: %s", esp_err_to_name(err));
            return err;
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot         = _host;
        host.max_freq_khz = _freq_khz;

        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.host_id = _host;
        slot.gpio_cs = (gpio_num_t)_cs_pin;

        esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
        mcfg.format_if_mount_failed = false;          // never erase data automatically
        mcfg.max_files              = SD_AIOT_MAX_OPEN_FILES;
        mcfg.allocation_unit_size   = 16 * 1024;

        err = esp_vfs_fat_sdspi_mount(_mount, &host, &slot, &mcfg, &_card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Mount failed (%s): card inserted? FAT32 formatted?", esp_err_to_name(err));
            _card = nullptr;
            return err;
        }
        ESP_LOGI(TAG, "Card mounted on %s (%s, %llu MB)", _mount, _card->cid.name,
                 (unsigned long long)(((uint64_t)_card->csd.capacity) * _card->csd.sector_size / (1024 * 1024)));
        return ESP_OK;
    }

    esp_err_t info(uint64_t *total, uint64_t *free_b) {
        if (_card == nullptr) return ESP_ERR_INVALID_STATE;
        uint64_t t = 0, f = 0;
        esp_err_t err = esp_vfs_fat_info(_mount, &t, &f);
        if (err == ESP_OK) {
            if (total)  *total  = t;
            if (free_b) *free_b = f;
        }
        return err;
    }

    esp_err_t writeFile(const char *name, const void *data, size_t len, bool append) {
        if (_card == nullptr) return ESP_ERR_INVALID_STATE;
        if (data == nullptr && len > 0) return ESP_ERR_INVALID_ARG;
        char path[SD_AIOT_PATH_MAX];
        if (!buildPath(name, path, sizeof(path))) return ESP_ERR_INVALID_ARG;

        xSemaphoreTake(_lock, portMAX_DELAY);
        esp_err_t err = ESP_OK;
        FILE *f = fopen(path, append ? "ab" : "wb");
        if (f == nullptr) {
            err = ESP_FAIL;
        } else {
            if (len > 0 && fwrite(data, 1, len, f) != len) {
                err = ESP_FAIL;
            }
            fclose(f);
        }
        xSemaphoreGive(_lock);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write error on %s", path);
        }
        return err;
    }

    esp_err_t appendLine(const char *name, const char *line) {
        if (line == nullptr) return ESP_ERR_INVALID_ARG;
        if (_card == nullptr) return ESP_ERR_INVALID_STATE;
        char path[SD_AIOT_PATH_MAX];
        if (!buildPath(name, path, sizeof(path))) return ESP_ERR_INVALID_ARG;

        xSemaphoreTake(_lock, portMAX_DELAY);
        esp_err_t err = ESP_OK;
        FILE *f = fopen(path, "a");
        if (f == nullptr || fputs(line, f) < 0 || fputc('\n', f) == EOF) {
            err = ESP_FAIL;
        }
        if (f != nullptr) fclose(f);
        xSemaphoreGive(_lock);
        return err;
    }

    int readFile(const char *name, void *buffer, size_t max_len) {
        if (_card == nullptr || buffer == nullptr || max_len == 0) return -1;
        char path[SD_AIOT_PATH_MAX];
        if (!buildPath(name, path, sizeof(path))) return -1;

        xSemaphoreTake(_lock, portMAX_DELAY);
        int n = -1;
        FILE *f = fopen(path, "rb");
        if (f != nullptr) {
            n = (int)fread(buffer, 1, max_len, f);
            fclose(f);
        }
        xSemaphoreGive(_lock);
        return n;
    }

    bool exists(const char *name) {
        if (_card == nullptr) return false;
        char path[SD_AIOT_PATH_MAX];
        if (!buildPath(name, path, sizeof(path))) return false;
        struct stat st;
        return stat(path, &st) == 0;
    }

    esp_err_t removeFile(const char *name) {
        if (_card == nullptr) return ESP_ERR_INVALID_STATE;
        char path[SD_AIOT_PATH_MAX];
        if (!buildPath(name, path, sizeof(path))) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        int r = unlink(path);
        xSemaphoreGive(_lock);
        return (r == 0) ? ESP_OK : ESP_FAIL;
    }
};

// Handle <-> object conversion
static inline SD_AIoT *toObj(SD_AIoT_handle_t handle) {
    return reinterpret_cast<SD_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

SD_AIoT_handle_t SD_AIoT_create(int host, int mosi_pin, int miso_pin, int sclk_pin,
                                int cs_pin, const char *mount_point) {
    if ((host != SPI2_HOST && host != SPI3_HOST) || sclk_pin < 0 || cs_pin < 0 ||
        mount_point == nullptr || mount_point[0] != '/' || strlen(mount_point) >= 32) {
        ESP_LOGE(TAG, "Invalid arguments (host %d, CS %d, mount '%s')",
                 host, cs_pin, mount_point ? mount_point : "NULL");
        return nullptr;
    }
    SD_AIoT *obj = new (std::nothrow) SD_AIoT(host, mosi_pin, miso_pin, sclk_pin, cs_pin, mount_point);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for SD object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<SD_AIoT_handle_t>(obj);
}

esp_err_t SD_AIoT_set_max_freq_khz(SD_AIoT_handle_t handle, int freq_khz) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->setFreq(freq_khz) : ESP_ERR_INVALID_ARG;
}

esp_err_t SD_AIoT_init(SD_AIoT_handle_t handle) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void SD_AIoT_destroy(SD_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

bool SD_AIoT_is_mounted(SD_AIoT_handle_t handle) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->mounted() : false;
}

const char *SD_AIoT_get_mount_point(SD_AIoT_handle_t handle) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->mountPoint() : "";
}

esp_err_t SD_AIoT_get_info(SD_AIoT_handle_t handle, uint64_t *total_bytes, uint64_t *free_bytes) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->info(total_bytes, free_bytes) : ESP_ERR_INVALID_ARG;
}

esp_err_t SD_AIoT_write_file(SD_AIoT_handle_t handle, const char *name,
                             const void *data, size_t len, bool append) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->writeFile(name, data, len, append) : ESP_ERR_INVALID_ARG;
}

esp_err_t SD_AIoT_append_line(SD_AIoT_handle_t handle, const char *name, const char *line) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->appendLine(name, line) : ESP_ERR_INVALID_ARG;
}

int SD_AIoT_read_file(SD_AIoT_handle_t handle, const char *name, void *buffer, size_t max_len) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->readFile(name, buffer, max_len) : -1;
}

bool SD_AIoT_exists(SD_AIoT_handle_t handle, const char *name) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->exists(name) : false;
}

esp_err_t SD_AIoT_remove(SD_AIoT_handle_t handle, const char *name) {
    SD_AIoT *obj = toObj(handle);
    return obj ? obj->removeFile(name) : ESP_ERR_INVALID_ARG;
}

} // extern "C"
