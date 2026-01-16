#include "app_shared.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "event_groups.h"

EventGroupHandle_t g_controlEventGroup = NULL;  //创建全局事件组句柄

static volatile NrfDataPacket_t g_latestPacket;

/* 内部信号量（只在此模块管理） */
static SemaphoreHandle_t oled_sem = NULL;
static SemaphoreHandle_t control_sem = NULL;

void shared_init(void)
{
    /* 创建二值信号量：NRF写入后给 sem，消费者等待 sem */
    if (oled_sem == NULL) {
        oled_sem = xSemaphoreCreateBinary();
        configASSERT(oled_sem != NULL);
    }
    if (control_sem == NULL) {
        control_sem = xSemaphoreCreateBinary();
        configASSERT(control_sem != NULL);
    }
	
	// 创建全局使用的事件组
	if (g_controlEventGroup == NULL) {
        g_controlEventGroup = xEventGroupCreate();
        configASSERT(g_controlEventGroup != NULL);
    }
}

/* 写入共享区（由 NRF 任务调用） */
void shared_set_packet(const NrfDataPacket_t *p)
{
    taskENTER_CRITICAL();
	
    g_latestPacket = *p;      /* 结构体拷贝 */
	
    taskEXIT_CRITICAL();

    /* 通知消费者 */
    if (oled_sem) {
        xSemaphoreGive(oled_sem);
    }
    if (control_sem) {
        xSemaphoreGive(control_sem);
    }
}

/* 消费者等待接口（OLED 使用） */
BaseType_t shared_wait_oled(TickType_t ticks)
{
    return xSemaphoreTake(oled_sem, ticks);
}

/* 消费者等待接口（Control 使用） */
BaseType_t shared_wait_control(TickType_t ticks)
{
    return xSemaphoreTake(control_sem, ticks);
}

/* 从共享区拷贝出来（消费者调用，尽量短） */
void shared_copy_packet(NrfDataPacket_t *out)
{
    taskENTER_CRITICAL();
    *out = g_latestPacket;
    taskEXIT_CRITICAL();
}
