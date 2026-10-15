/*
 * File: components/Frame_AIoT/include/Frame_AIoT.h
 * Description: Binary data frame sent by the SENSOR NODE to the CrowPanel
 *              (communication node) over ESP-NOW.
 *              THIS FILE MUST BE IDENTICAL IN BOTH PROJECTS: copy the whole
 *              Frame_AIoT folder into the components of the receiver.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Layout (little endian, packed, 198 bytes, fits the 250-byte ESP-NOW limit):
 *
 *   offset size  field
 *   0      1     magic        0xA5
 *   1      1     version      AIOT_FRAME_VERSION
 *   2      1     node_id      sensor node number
 *   3      1     n_samples    valid samples in s[] (1..AIOT_FRAME_SAMPLES)
 *   4      4     seq          frame counter (gaps = lost frames)
 *   8      8     t0_us        node time of s[0] in microseconds (esp_timer)
 *   16     4     period_us    time between samples
 *   20     2     status       AIOT_STATUS_* bits
 *   22     2     reserved     0
 *   24     4     flow_lps     flow Q [L/s] (NaN = no flow meter)
 *   28     4     volume_l     accumulated volume [L]
 *   32     4     temp_c       IMU die temperature [degC]
 *   36     160   s[5]         samples, 32 bytes each (see AIoT_sample_t)
 *   196    2     crc          CRC-16/CCITT-FALSE of bytes 0..195
 *
 * Python (receiver / server):
 *   struct.unpack("<BBBBIQIHHfff" + "8f" * 5 + "H", payload)
 *   crc check: binascii.crc_hqx(payload[:196], 0xFFFF) == crc
 *
 * Unavailable values (sensor missing or faulty) are sent as NaN.
 */
#ifndef FRAME_AIOT_H
#define FRAME_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIOT_FRAME_MAGIC          0xA5
#define AIOT_FRAME_VERSION        1
#define AIOT_FRAME_SAMPLES        5      /* samples per frame */
#define AIOT_FRAME_PRESSURE_CH    2      /* pressure inputs per sample */

/* Status bits (a set bit means a problem during this frame) */
#define AIOT_STATUS_P1_FAULT      (1U << 0)   /* pressure 1: loop open / out of range */
#define AIOT_STATUS_P2_FAULT      (1U << 1)   /* pressure 2: loop open / out of range */
#define AIOT_STATUS_IMU_FAULT     (1U << 2)   /* IMU missing or read error */
#define AIOT_STATUS_FLOW_FAULT    (1U << 3)   /* flow meter not running */
#define AIOT_STATUS_ADC_OVERRUN   (1U << 4)   /* ADC data lost (DMA pool full) */
#define AIOT_STATUS_TIMING_LATE   (1U << 5)   /* a sampling tick was missed */
#define AIOT_STATUS_TX_DROPPED    (1U << 6)   /* an earlier frame was dropped (TX queue full) */

/* One sample: pressures + IMU at the same instant (32 bytes) */
typedef struct __attribute__((packed)) {
    float p_kpa[AIOT_FRAME_PRESSURE_CH];   /* gauge pressure [kPa] */
    float acc[3];                          /* acceleration x, y, z [m/s2] */
    float gyr[3];                          /* angular rate x, y, z [rad/s] */
} AIoT_sample_t;

/* Complete frame (198 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t       magic;
    uint8_t       version;
    uint8_t       node_id;
    uint8_t       n_samples;
    uint32_t      seq;
    uint64_t      t0_us;
    uint32_t      period_us;
    uint16_t      status;
    uint16_t      reserved;
    float         flow_lps;
    float         volume_l;
    float         temp_c;
    AIoT_sample_t s[AIOT_FRAME_SAMPLES];
    uint16_t      crc;
} AIoT_frame_t;

#ifdef __cplusplus
static_assert(sizeof(AIoT_sample_t) == 32, "AIoT_sample_t must be 32 bytes");
static_assert(sizeof(AIoT_frame_t) == 198, "AIoT_frame_t must be 198 bytes");
static_assert(sizeof(AIoT_frame_t) <= 250, "ESP-NOW payload limit is 250 bytes");
#else
_Static_assert(sizeof(AIoT_sample_t) == 32, "AIoT_sample_t must be 32 bytes");
_Static_assert(sizeof(AIoT_frame_t) == 198, "AIoT_frame_t must be 198 bytes");
_Static_assert(sizeof(AIoT_frame_t) <= 250, "ESP-NOW payload limit is 250 bytes");
#endif

/**
 * @brief CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection).
 *        Same result as Python binascii.crc_hqx(data, 0xFFFF).
 */
static inline uint16_t AIoT_frame_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/** @brief Fills magic / version and computes the CRC (call last, before sending) */
static inline void AIoT_frame_seal(AIoT_frame_t *frame) {
    frame->magic    = AIOT_FRAME_MAGIC;
    frame->version  = AIOT_FRAME_VERSION;
    frame->reserved = 0;
    frame->crc = AIoT_frame_crc16((const uint8_t *)frame, offsetof(AIoT_frame_t, crc));
}

/**
 * @brief Validates a received payload (size, magic, version, sample count, CRC).
 * @return true if the payload can be used as an AIoT_frame_t
 */
static inline bool AIoT_frame_check(const void *payload, size_t len) {
    if (payload == NULL || len != sizeof(AIoT_frame_t)) {
        return false;
    }
    const AIoT_frame_t *f = (const AIoT_frame_t *)payload;
    if (f->magic != AIOT_FRAME_MAGIC || f->version != AIOT_FRAME_VERSION ||
        f->n_samples == 0 || f->n_samples > AIOT_FRAME_SAMPLES) {
        return false;
    }
    return AIoT_frame_crc16((const uint8_t *)payload, offsetof(AIoT_frame_t, crc)) == f->crc;
}

#ifdef __cplusplus
}
#endif

#endif /* FRAME_AIOT_H */
