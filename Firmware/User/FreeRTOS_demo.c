#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// 硬件驱动相关头文件
#include "NRF24L01.h"
#include "OLED.h"                
#include "Stepper_motor_Drive.h"
#include "servo.h"     


// 任务相关头文件
#include "app_types.h"
#include "app_shared.h"
#include "NRF24L01_task.h"
#include "OLED_task.h"
#include "Control_task.h"
//-------------------------------------------------------任务参数------------------------------------------------------//

#define START_TASK_PRIO       1            //开机启动任务
#define START_TASK_STACK_SIZE 128
TaskHandle_t star_task_handle;
void start_task( void * pvParameters );

void freertos_demo(void)
{
	// 中断优先级分组配置
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); // 将中断优先级分组为 0-15的抢占优先级
	
	shared_init();
	
	// 硬件外设初始化
	NRF24L01_Init();
	OLED_Init();
	Stepper_Init_All();
	Servo_Init();
	
	// 创建启动任务
	xTaskCreate(start_task, "start_task", START_TASK_STACK_SIZE, NULL, START_TASK_PRIO, &star_task_handle);		

	// 开启任务调度器
	vTaskStartScheduler();
							
}
// 启动任务，创建接下来需要执行的任务
void start_task( void * pvParameters )
{  
	taskENTER_CRITICAL();  // 进入临界区
	
	NRF24L01_Task();
	OLED_Task();
	Control_Task();
	
	taskEXIT_CRITICAL(); // 退出临界区
	
	vTaskDelete(NULL);
}













