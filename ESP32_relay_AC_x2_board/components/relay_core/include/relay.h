/**
 * @file    relay.h
 * @brief   Điều khiển 2 relay của bo ESP32_Relay_AC_X2.
 *
 * - Kênh đánh số 1..RELAY_COUNT (giống nhãn trên bo).
 * - Khởi động luôn ở trạng thái TẮT (an toàn cho tải 220 V).
 * - Trạng thái đọc lại từ GPIO_OUT_REG (không chỉ tin biến nhớ) – relay_self_check()
 *   phát hiện sai khác giữa biến nhớ và thanh ghi.
 * - Bảo vệ tiếp điểm: không cho đảo trạng thái cùng một relay nhanh hơn min_switch_ms
 *   (chống "spam" lệnh qua MQTT/Web làm cháy tiếp điểm).
 * - Không re-entrant: tầng ứng dụng phải khoá mutex khi gọi từ nhiều task.
 */
#ifndef RELAY_H
#define RELAY_H

#include "rc_types.h"

#define RELAY_COUNT   (2U)

typedef struct
{
    uint8_t  pin[RELAY_COUNT];   /**< Số GPIO của từng relay (bo này: 16, 17). */
    bool     active_high;        /**< true: mức 1 = relay hút. */
    uint32_t min_switch_ms;      /**< Khoảng cách tối thiểu giữa hai lần đảo cùng relay. */
} relay_config_t;

/** Cấu hình chân, đưa tất cả relay về TẮT. */
rc_status_t relay_init(const relay_config_t *cfg);

/**
 * Bật/tắt relay @p channel (1..RELAY_COUNT).
 * @param changed  (có thể NULL) nhận true nếu trạng thái thực sự thay đổi.
 * @return RC_ERR_BUSY nếu đảo trạng thái quá sớm; đặt lại đúng trạng thái hiện có luôn RC_OK.
 */
rc_status_t relay_set(uint8_t channel, bool on, uint32_t now_ms, bool *changed);

/** Đảo trạng thái relay @p channel. */
rc_status_t relay_toggle(uint8_t channel, uint32_t now_ms, bool *changed);

/** Đọc trạng thái relay từ thanh ghi GPIO_OUT. */
rc_status_t relay_get(uint8_t channel, bool *on);

/** Bit0 = relay 1, bit1 = relay 2 (theo biến nhớ). 0 nếu chưa init. */
uint8_t relay_get_mask(void);

/** Số lần đóng/cắt kể từ khi khởi động (để theo dõi tuổi thọ tiếp điểm). */
uint32_t relay_get_switch_count(uint8_t channel);

/** RC_OK nếu thanh ghi GPIO_OUT khớp biến nhớ cho mọi kênh, RC_ERR_STATE nếu lệch. */
rc_status_t relay_self_check(void);

#endif /* RELAY_H */
