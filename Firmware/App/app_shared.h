#ifndef APP_SHARED_H
#define APP_SHARED_H

#include "app_types.h"
#include "FreeRTOS.h"
#include "event_groups.h"

/* 全局事件组句柄 (Control任务和ISR会用到) */
extern EventGroupHandle_t g_controlEventGroup; 

/* 初始化 */
void shared_init(void); 

/* 数据交互接口 */
void shared_set_packet(const NrfDataPacket_t *p); // 写入数据（生产者：NRF）
void shared_copy_packet(NrfDataPacket_t *out);    // 读取数据（消费者：Control/OLED）

/* 任务同步接口 */
BaseType_t shared_wait_oled(TickType_t ticks);    // OLED 等待新数据
BaseType_t shared_wait_control(TickType_t ticks); // Control 等待新数据

#endif
