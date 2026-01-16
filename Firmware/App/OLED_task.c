#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "app_shared.h"
#include "app_types.h"
#include "OLED.h"

// OLED任务参数设置
#define OLED_TASK_PRIO    3       // 优先级
#define OLED_TASK_STACK   512     // 栈大小，单位：word
static TaskHandle_t oled_handle;

void oled(void *pvParameters)
{
	NrfDataPacket_t oled;
	
	// 初始布局初始化
	OLED_Clear();
	OLED_ShowString(0,0,"start",OLED_8X16);
	OLED_ShowString(0,16,"X:0000 Z:000",OLED_8X16);
	OLED_ShowString(0,32,"Y:0000 A:000",OLED_8X16);
	OLED_ShowString(0,48,"Stage: IDLE     ",OLED_8X16);
	OLED_Update();	
	
	uint8_t state;
	while(1)
	{
		if(shared_wait_oled(0) == pdTRUE)
		{
			shared_copy_packet(&oled);
			
			OLED_ShowNum(16,16,oled.posX,4,OLED_8X16);
			OLED_ShowNum(16,32,oled.posY,4,OLED_8X16);
			OLED_ShowNum(72,16,oled.posZ,3,OLED_8X16);
			OLED_ShowNum(72,32,oled.angle,3,OLED_8X16);
			switch(oled.label){
				case 1:
					OLED_ShowString(0,0,"Recyclable Waste",OLED_8X16);
					break;
				case 2:
					OLED_ShowString(0,0,"Other      Waste",OLED_8X16);
					break;
				case 3:
					OLED_ShowString(0,0,"Kitchen    Waste",OLED_8X16);
					break;
				case 4:
					OLED_ShowString(0,0,"Hazardous  Waste",OLED_8X16);
					break;
				default:
					OLED_ShowString(0,0,"Err Label:      ",OLED_8X16); 
					OLED_ShowHexNum(80,0, oled.label, 2, OLED_8X16); // 显示十六进制，比如 0x31
			}
			OLED_Update();	
		}

		if (xQueueReceive(Control_StateQueue, &state, 0) == pdTRUE)
		{
			switch(state)
			{
				case CTRL_IDLE:        OLED_ShowString(48,48," IDLE     ",OLED_8X16); break;
				case CTRL_MOVE_XY:     OLED_ShowString(48,48," Moving XY",OLED_8X16); break;
				case CTRL_MOVE_Z_DOWN: OLED_ShowString(48,48," Z down   ",OLED_8X16); break;
				case CTRL_GRAB:        OLED_ShowString(48,48," Grabbing ",OLED_8X16); break;
				case CTRL_MOVE_Z_UP:   OLED_ShowString(48,48," Z up     ",OLED_8X16); break;
				case CTRL_MOVE_TO_BIN: OLED_ShowString(48,48," To bin   ",OLED_8X16); break;
				case CTRL_RELEASE:     OLED_ShowString(48,48," Releasing",OLED_8X16); break;
			}
			OLED_Update(); // 刷屏
		}
		vTaskDelay(5); 
	}
}

void OLED_Task(void)
{
	xTaskCreate(oled, "oled", OLED_TASK_STACK, NULL, OLED_TASK_PRIO, &oled_handle);
}
