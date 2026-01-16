#ifndef __APP_TYPES_H__
#define __APP_TYPES_H__

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"

extern QueueHandle_t Control_StateQueue;

// --- NRF 任务专用事件位 ---
#define NRF_EVT_IRQ_RX_DR    (1 << 0)  // 硬件：收到数据
#define NRF_EVT_IRQ_TX_DS    (1 << 1)  // 硬件：发送完成
#define NRF_EVT_IRQ_MAX_RT   (1 << 2)  // 硬件：达到最大重发
#define NRF_EVT_CMD_SEND     (1 << 3)  // 软件：请求发送数据

//--- Control 任务专用事件位 ---
#define EVT_X_DONE           (1 << 0)  
#define EVT_Y_DONE           (1 << 1)  
#define EVT_Z_DONE           (1 << 2)

/* NRF通信包 */
typedef struct {
    uint8_t  label;     // 标签编号 1~4
    uint16_t posX;
    uint16_t posY;
    uint16_t posZ;
    uint8_t  angle;
} NrfDataPacket_t;

// 控制任务的状态机
typedef enum {
    CTRL_IDLE = 0,
    CTRL_MOVE_XY,
    CTRL_MOVE_Z_DOWN,
    CTRL_GRAB,
    CTRL_MOVE_Z_UP,
    CTRL_MOVE_TO_BIN,
    CTRL_RELEASE,
} SystemStage_t; 

void nrf_notify_tx(void);

#endif
