# Frame_AIoT

Binary frame sent by the **sensor node** to the **CrowPanel** over ESP-NOW.
Header-only component. Part of the **Transitorios AIoT** project.

**This folder must be identical in both projects.** Copy it unchanged into
`components/` of the CrowPanel project and add `Frame_AIoT` to the `REQUIRES`
list of the component that receives the frames.

## Contents

| Item | Description |
|---|---|
| `AIoT_sample_t` | One sample: `p_kpa[2]` [kPa], `acc[3]` [m/s2], `gyr[3]` [rad/s] (32 bytes) |
| `AIoT_frame_t` | Header + 5 samples + CRC (198 bytes, packed, little endian) |
| `AIOT_STATUS_*` | Fault bits (pressure, IMU, flow, ADC overrun, late tick, TX drop) |
| `AIoT_frame_seal(f)` | Sets magic / version and computes the CRC (sender) |
| `AIoT_frame_check(data, len)` | Validates size, magic, version and CRC (receiver) |
| `AIoT_frame_crc16(data, len)` | CRC-16/CCITT-FALSE, same as Python `binascii.crc_hqx(data, 0xFFFF)` |

## Receiver example (CrowPanel)

```c
ESPNOW_AIoT_packet_t pkt;
if (ESPNOW_AIoT_receive(link, &pkt, 100) == ESP_OK &&
    AIoT_frame_check(pkt.data, pkt.len)) {
    AIoT_frame_t f;
    memcpy(&f, pkt.data, sizeof(f));             /* aligned copy */
    float p1 = f.s[f.n_samples - 1].p_kpa[0];    /* latest pressure 1 [kPa] */
}
```

## Notes

- Copy the payload into an `AIoT_frame_t` variable (as above) instead of
  reading floats through a pointer into the receive buffer.
- Values not available on the node are `NaN`; always check the `status` bits.
- Any change of the layout must increase `AIOT_FRAME_VERSION` on both boards.
