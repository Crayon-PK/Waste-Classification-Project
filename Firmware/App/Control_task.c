#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "app_types.h"
#include "app_shared.h"
#include "Stepper_motor_Drive.h"
#include "servo.h"

// Control任务参数设置
#define CONTROL_TASK_PRIO       	3             // 优先级
#define CONTROL_TASK_STACK_SIZE 	512			  // 栈大小
static TaskHandle_t control_task_handle;

QueueHandle_t Control_StateQueue; // 控制状态队列

// 垃圾桶坐标定义 [X, Y]
uint16_t binPos_t[4][2] = 
{
    {2000,750},
    {2000,1500},
    {2000,2250},
    {2000,3000}
};

static NrfDataPacket_t local; 			// 本地控制数据副本 
static SystemStage_t stage = CTRL_IDLE; // 当前控制阶段

// 辅助函数：更新状态并发送到队列（通知OLED）
void Control_SetState(SystemStage_t new_state)
{
    uint8_t s = (uint8_t)new_state;
    xQueueOverwrite(Control_StateQueue, &s);
}

// 电机中断回调
static void stepper_isr_cb(uint8_t motor_id)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    switch (motor_id) {
        case MOTOR_X: xEventGroupSetBitsFromISR(g_controlEventGroup, EVT_X_DONE, &xHigherPriorityTaskWoken); break;
        case MOTOR_Y: xEventGroupSetBitsFromISR(g_controlEventGroup, EVT_Y_DONE, &xHigherPriorityTaskWoken); break;
        case MOTOR_Z: xEventGroupSetBitsFromISR(g_controlEventGroup, EVT_Z_DONE, &xHigherPriorityTaskWoken); break;
        default: break;
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void control(void *pvParameters)
{
    EventBits_t wait_bits = 0; // 用于记录当前真正需要等待的标志位

    while (1)
    {
        if (stage == CTRL_IDLE)        // 等待新的数据包
        {
            if (shared_wait_control(portMAX_DELAY) == pdTRUE)
            {
                shared_copy_packet(&local);
                stage = CTRL_MOVE_XY;
            }
        }
        // 根据当前阶段推进
        switch (stage)
        {
            case CTRL_MOVE_XY:
				Control_SetState(stage); // 更新任务状态
				
				// --- 先移动Y轴并等待完成（防撞） ---
                if (local.posY != motor_pos_y)
                {
                    Stepper_Move(MOTOR_Y, local.posY);
                    xEventGroupWaitBits(g_controlEventGroup, EVT_Y_DONE, 
                                        pdTRUE, pdTRUE, portMAX_DELAY);
                }
				
                // --- 再移动X轴并等待完成 ---
                if (local.posX != motor_pos_x)
                {
                    Stepper_Move(MOTOR_X, local.posX);
                    xEventGroupWaitBits(g_controlEventGroup, EVT_X_DONE, 
                                        pdTRUE, pdTRUE, portMAX_DELAY);
                }
				
                // 等待 XY 两轴完成
				if (local.label == 0xFF)
                {
                    stage = CTRL_RELEASE; // 如果是复位，直接跳到释放阶段恢复舵机角度并结束
                    break;
                }
                stage = CTRL_MOVE_Z_DOWN;
                break;

            case CTRL_MOVE_Z_DOWN:
				Control_SetState(stage); // 更新任务状态
               // --- Z轴下放逻辑 ---
                if (local.posZ != motor_pos_z)
                {
                    Stepper_Move(MOTOR_Z, local.posZ);
                    // 等待 Z 完成
                    xEventGroupWaitBits(g_controlEventGroup, EVT_Z_DONE, 
                                        pdTRUE, pdTRUE, portMAX_DELAY);
                }
                stage = CTRL_GRAB;
                break;

            case CTRL_GRAB:
				Control_SetState(stage); // 更新任务状态
                Servo_SetAngle(local.angle);
                vTaskDelay(2000);
                stage = CTRL_MOVE_Z_UP;
                break;

            case CTRL_MOVE_Z_UP:
				Control_SetState(stage); // 更新任务状态
                if (motor_pos_z != 0) // 如果当前不在0点，才归零
                {
                    Stepper_Move(MOTOR_Z, 1);
                    xEventGroupWaitBits(g_controlEventGroup, EVT_Z_DONE, 
                                        pdTRUE, pdTRUE, portMAX_DELAY);
                }
                stage = CTRL_MOVE_TO_BIN;
                break;

            case CTRL_MOVE_TO_BIN:
			{
				Control_SetState(stage); // 更新任务状态
				
                uint8_t safe_idx = local.label - 1;	// 防止数组越界
				if (safe_idx > 3) safe_idx = 0; // 如果收到非法标签，默认去第1个桶，防止死机
				
                uint16_t binX = binPos_t[local.label - 1][0];
                uint16_t binY = binPos_t[local.label - 1][1];
                // --- X轴移动 ---
                if (binX != motor_pos_x)
                {
                    Stepper_Move(MOTOR_X, binX);
                    wait_bits |= EVT_X_DONE;
                }

                // --- Y轴移动 ---
                if (binY != motor_pos_y)
                {
                    Stepper_Move(MOTOR_Y, binY);
                    wait_bits |= EVT_Y_DONE;
                }

                // --- 等待 ---
                if (wait_bits != 0)
                {
                    xEventGroupWaitBits(g_controlEventGroup, wait_bits, 
                                        pdTRUE, pdTRUE, portMAX_DELAY);
                }
                stage = CTRL_RELEASE;
                break;
			}
            case CTRL_RELEASE:
				Control_SetState(stage); // 更新任务状态
                Servo_SetAngle(90);
                vTaskDelay(500);
			
                stage = CTRL_IDLE;  // 完成本包处理
				Control_SetState(stage); // 更新任务状态
				nrf_notify_tx();
                break;

            default:
                stage = CTRL_IDLE;
				Control_SetState(stage); // 更新任务状态
                break;
        }
    }
}


void Control_Task(void)
{
	// 确保队列只创建一次
    if (Control_StateQueue == NULL) {
        Control_StateQueue = xQueueCreate(1, sizeof(uint8_t));
    }
	
	xTaskCreate(control, "control", CONTROL_TASK_STACK_SIZE, NULL, CONTROL_TASK_PRIO, &control_task_handle);
	
	Stepper_RegisterCallback(MOTOR_X, stepper_isr_cb);
    Stepper_RegisterCallback(MOTOR_Y, stepper_isr_cb);
    Stepper_RegisterCallback(MOTOR_Z, stepper_isr_cb);
}
