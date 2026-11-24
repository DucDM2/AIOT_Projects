/*
 * door.h - Doc cam bien cua tu (reed switch) MC-38 NC, chong doi phim bang thoi gian on dinh,
 * dem so canh nhieu (bounce) bang ngat EXTI.
 */
#ifndef DOOR_H
#define DOOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { DOOR_CLOSED = 0, DOOR_OPEN = 1 } door_state_t;
typedef enum { DOOR_EVT_NONE = 0, DOOR_EVT_OPENED, DOOR_EVT_CLOSED } door_event_t;

void         door_init(void);
door_event_t door_poll(void);          /* goi lien tuc trong vong lap chinh */
door_state_t door_state(void);         /* trang thai da chong doi */
bool         door_raw_level(void);     /* muc logic tuc thoi cua chan */
uint32_t     door_since_ms(void);      /* thoi diem (millis) cua lan doi trang thai gan nhat */
uint32_t     door_last_bounces(void);  /* so canh EXTI trong lan chuyen trang thai gan nhat */
uint32_t     door_edge_total(void);    /* tong so canh EXTI tu luc khoi dong */

const char  *door_state_name(door_state_t s);

#endif /* DOOR_H */
