#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "app_types.h"
#include "app_shared.h"
#include "NRF24L01.h"

// NRF任务参数设置
#define NRF24L01_TASK_PRIO       3  
#define NRF24L01_TASK_STACK_SIZE 512
static TaskHandle_t nrf24l01_task_handle;

static EventGroupHandle_t nrf_event_group = NULL; // 局部私有事件组

void nrf_notify_tx(void)
{
    if (nrf_event_group != NULL) {
        xEventGroupSetBits(nrf_event_group, NRF_EVT_CMD_SEND);
    }
}

void NRF_RtosCallback(uint8_t event)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (nrf_event_group != NULL) 
    {
        if (event == 1)      xEventGroupSetBitsFromISR(nrf_event_group, NRF_EVT_IRQ_RX_DR, &xHigherPriorityTaskWoken);
        else if (event == 2) xEventGroupSetBitsFromISR(nrf_event_group, NRF_EVT_IRQ_TX_DS, &xHigherPriorityTaskWoken);
        else if (event == 3) xEventGroupSetBitsFromISR(nrf_event_group, NRF_EVT_IRQ_MAX_RT, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void nrf24l01_task(void *pvParameters)
{
    NrfDataPacket_t tmp;
    EventBits_t uxBits;

    // 硬件初始化后，先清空一下旧状态
    NRF24L01_FlushTx();
    NRF24L01_FlushRx();
    NRF24L01_WriteReg(NRF24L01_STATUS, 0x70); 
    NRF24L01_Rx(); // 默认进入接收模式

    while(1)
    {
        // 核心：一句话等待所有事件
        // 只要有任意一个位被置 1，任务就会醒来
        uxBits = xEventGroupWaitBits(nrf_event_group, 
                                     NRF_EVT_IRQ_RX_DR | NRF_EVT_IRQ_TX_DS | NRF_EVT_IRQ_MAX_RT | NRF_EVT_CMD_SEND,
                                     pdTRUE,   // 退出时自动清除这些位
                                     pdFALSE,  // 任意一位满足即可
                                     portMAX_DELAY);

        // 1. 处理接收 (RX_DR)
        if (uxBits & NRF_EVT_IRQ_RX_DR)
        {
            if (NRF24L01_ReadStatus() & 0x40)
            {
                NRF24L01_ReadRxPayload(NRF24L01_RxPacket, NRF24L01_RX_PACKET_WIDTH);
                NRF24L01_WriteReg(NRF24L01_STATUS, 0x40); // 清硬件标志

                // 数据解析
                tmp.label = NRF24L01_RxPacket[1]; // 真实的 03 在这里
				tmp.posX  = (NRF24L01_RxPacket[2] << 8) | NRF24L01_RxPacket[3];
				tmp.posY  = (NRF24L01_RxPacket[4] << 8) | NRF24L01_RxPacket[5];
				tmp.posZ  = (NRF24L01_RxPacket[6] << 8) | NRF24L01_RxPacket[7];
				tmp.angle = NRF24L01_RxPacket[8];

                // 收到命令后的立即应答 (ACK)
                NRF24L01_TxPacket[1] = 0xFF; 
                NRF24L01_Send(); 

                // 把数据扔到共享区
                shared_set_packet(&tmp); 
            }
        }

        // 2. 处理软件发送请求 (CMD_SEND)
        if (uxBits & NRF_EVT_CMD_SEND)
        {
            NRF24L01_TxPacket[1] = 0xAA; // 任务完成标志
            NRF24L01_Send();
        }

        // 3. 处理发送完成 (TX_DS)
        if (uxBits & NRF_EVT_IRQ_TX_DS)
        {
            NRF24L01_WriteReg(NRF24L01_STATUS, 0x20); // 清硬件标志
            NRF24L01_Rx();
        }

        // 4. 处理最大重发 (MAX_RT)
        if (uxBits & NRF_EVT_IRQ_MAX_RT)
        {
            NRF24L01_WriteReg(NRF24L01_STATUS, 0x10); // 清硬件标志
            NRF24L01_FlushTx();
            NRF24L01_Rx();
        }
    }
}

void NRF24L01_Task(void)
{	
    if (nrf_event_group == NULL) {
        nrf_event_group = xEventGroupCreate();
        configASSERT(nrf_event_group != NULL);
    }
	
	xTaskCreate(nrf24l01_task, "nrf24l01_task", NRF24L01_TASK_STACK_SIZE, NULL, NRF24L01_TASK_PRIO, &nrf24l01_task_handle);
	
	NRF24L01_RegisterCallback(NRF_RtosCallback);  // 注册回调函数
	NRF24L01_TxPacket[0] = 0x01;                   // 因为回复数据仅有一个字节，所以直接写死
}

