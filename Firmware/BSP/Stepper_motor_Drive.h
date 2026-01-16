#ifndef __STEPPER_MOTOR_DRIVE_H
#define __STEPPER_MOTOR_DRIVE_H

#include "stm32f10x.h"

//================ 电机编号 ==================
#define MOTOR_X 0
#define MOTOR_Y 1
#define MOTOR_Z 2

//================ 对外变量 ==================
extern volatile int32_t motor_pos_x;
extern volatile int32_t motor_pos_y;
extern volatile int32_t motor_pos_z;

typedef void (*Stepper_Callback_t)(uint8_t motor_id);

// 对外接口
void Stepper_Init_All(void);
void Stepper_SetFreq(uint8_t motor, uint16_t arr, uint16_t psc); // 设置 ARR/PSC (频率)
void Stepper_Move(uint8_t MotorID, int32_t target_pulse);       // 启动移动（硬件 PWM 输出 + 使能比较中断）
void Stepper_Stop(uint8_t MotorID);                             // 立即停止（关 PWM，禁中断）
void Stepper_RegisterCallback(uint8_t MotorID, Stepper_Callback_t cb); // 注册每轴回调

void Motor_SetEnable(uint8_t MotorID, uint8_t Enable);
void Motor_SetDirection(uint8_t MotorID, uint8_t Direction);

#endif
