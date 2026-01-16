#include "stm32f10x.h"
#include <stddef.h>

//================== 电机引脚定义 ==================
#define MOTOR_X_GPIO_PORT      GPIOB
#define MOTOR_X_STEP_PIN       GPIO_Pin_0     // PB0 - TIM3_CH3
#define MOTOR_X1_DIR_PIN       GPIO_Pin_12
#define MOTOR_X2_DIR_PIN       GPIO_Pin_13
#define MOTOR_X_EN_PIN         GPIO_Pin_14

#define MOTOR_Y_GPIO_PORT      GPIOA
#define MOTOR_Y_STEP_PIN       GPIO_Pin_3     // PA3 - TIM2_CH4
#define MOTOR_Y_DIR_PIN        GPIO_Pin_4
#define MOTOR_Y_EN_PIN         GPIO_Pin_5

#define MOTOR_Z_GPIO_PORT      GPIOB
#define MOTOR_Z_STEP_PIN       GPIO_Pin_9     // PB9 - TIM4_CH4
#define MOTOR_Z_DIR_PIN        GPIO_Pin_8
#define MOTOR_Z_EN_PIN         GPIO_Pin_5

//================ 电机相关参数 ==================
#define MOTOR_X 0
#define MOTOR_Y 1
#define MOTOR_Z 2

#define PWM_ARR 999
#define PWM_PSC 71

#define M_ENABLE  1
#define M_DISABLE 0

//================ 全局变量 ==================
volatile int32_t motor_pos_x = 0;
volatile int32_t motor_pos_y = 0;
volatile int32_t motor_pos_z = 0;

static volatile uint32_t step_count_x = 0, target_steps_x = 0;
static volatile uint32_t step_count_y = 0, target_steps_y = 0;
static volatile uint32_t step_count_z = 0, target_steps_z = 0;

static volatile uint8_t dir_x = 1, dir_y = 1, dir_z = 1;

typedef void (*Stepper_Callback_t)(uint8_t motor_id);
static Stepper_Callback_t cb_x = NULL;
static Stepper_Callback_t cb_y = NULL;
static Stepper_Callback_t cb_z = NULL;

void Stepper_RegisterCallback(uint8_t MotorID, Stepper_Callback_t cb)
{
    switch (MotorID)
    {
        case MOTOR_X: cb_x = cb; break;
        case MOTOR_Y: cb_y = cb; break;
        case MOTOR_Z: cb_z = cb; break;
    }
}
//-----------------------------------------------------------------GPIO引脚初始化-----------------------------------------------------------------
static void Motor_X_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    // DIR & EN
    GPIO_InitStructure.GPIO_Pin = MOTOR_X1_DIR_PIN | MOTOR_X2_DIR_PIN | MOTOR_X_EN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_X_GPIO_PORT, &GPIO_InitStructure);

    // STEP (PWM output)
    GPIO_InitStructure.GPIO_Pin = MOTOR_X_STEP_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(MOTOR_X_GPIO_PORT, &GPIO_InitStructure);

    // 默认状态：EN=高（禁用）
    GPIO_SetBits(MOTOR_X_GPIO_PORT, MOTOR_X_EN_PIN);
    GPIO_ResetBits(MOTOR_X_GPIO_PORT, MOTOR_X1_DIR_PIN);
    GPIO_SetBits(MOTOR_X_GPIO_PORT, MOTOR_X2_DIR_PIN);
}

static void Motor_Y_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    // DIR & EN
    GPIO_InitStructure.GPIO_Pin = MOTOR_Y_DIR_PIN | MOTOR_Y_EN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_Y_GPIO_PORT, &GPIO_InitStructure);

    // STEP (PWM output)
    GPIO_InitStructure.GPIO_Pin = MOTOR_Y_STEP_PIN; // PA3 TIM2_CH4
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(MOTOR_Y_GPIO_PORT, &GPIO_InitStructure);

    GPIO_SetBits(MOTOR_Y_GPIO_PORT, MOTOR_Y_EN_PIN);
    GPIO_ResetBits(MOTOR_Y_GPIO_PORT, MOTOR_Y_DIR_PIN);
}

static void Motor_Z_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    // DIR & EN
    GPIO_InitStructure.GPIO_Pin = MOTOR_Z_DIR_PIN | MOTOR_Z_EN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_Z_GPIO_PORT, &GPIO_InitStructure);

    // STEP (PWM output)
    GPIO_InitStructure.GPIO_Pin = MOTOR_Z_STEP_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(MOTOR_Z_GPIO_PORT, &GPIO_InitStructure);

    GPIO_SetBits(MOTOR_Z_GPIO_PORT, MOTOR_Z_EN_PIN);
    GPIO_ResetBits(MOTOR_Z_GPIO_PORT, MOTOR_Z_DIR_PIN);
}

//-----------------------------------------------------------------PWM初始化-----------------------------------------------------------------
static void PWM_ChannelInit(TIM_TypeDef* TIMx, uint8_t channel, uint32_t arr, uint32_t psc)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    // 时基
    TIM_TimeBaseStructure.TIM_Period = (arr == 0) ? 1 : (arr - 1);
    TIM_TimeBaseStructure.TIM_Prescaler = (psc == 0) ? 0 : (psc - 1);
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIMx, &TIM_TimeBaseStructure);

    // PWM 输出配置
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = (TIMx->ARR + 1) / 2; // 默认 50% 占空比
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;

    if (TIMx == TIM3 && channel == 3) {
        TIM_OC3Init(TIMx, &TIM_OCInitStructure);
        TIM_OC3PreloadConfig(TIMx, TIM_OCPreload_Enable);
    } else if (TIMx == TIM2 && channel == 4) {
        TIM_OC4Init(TIMx, &TIM_OCInitStructure);
        TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);
    } else if (TIMx == TIM4 && channel == 4) {
        TIM_OC4Init(TIMx, &TIM_OCInitStructure);
        TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);
    }

    TIM_ARRPreloadConfig(TIMx, ENABLE);
    // TIM_Cmd(TIMx, ENABLE); // 不立即启动，Step_Move 时启动
}


//==================== NVIC 初始化 ====================
static void Stepper_NVIC_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    // TIM3
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 12;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // TIM2
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 13;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);

    // TIM4
    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 14;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);
}

//==================== 初始化所有电机 ====================
void Stepper_Init_All(void)
{
    Motor_X_GPIO_Init();
    Motor_Y_GPIO_Init();
    Motor_Z_GPIO_Init();

    // 开时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3 | RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM4, ENABLE);

    // 默认频率（小心：arr/psc 会在 SetFreq 时覆盖）
    PWM_ChannelInit(TIM3, 3, PWM_ARR, PWM_PSC);
    PWM_ChannelInit(TIM2, 4, PWM_ARR, PWM_PSC);
    PWM_ChannelInit(TIM4, 4, PWM_ARR, PWM_PSC);

    Stepper_NVIC_Init();
}

//==================== 设置PWM频率 ====================
void Stepper_SetFreq(uint8_t motor, uint16_t arr, uint16_t psc)
{
    if (arr == 0) arr = 1;
    switch (motor)
    {
        case MOTOR_X:
            TIM3->CR1 &= ~TIM_CR1_CEN; // 关计数器，安全设置
            TIM3->PSC = (psc == 0) ? 0 : (psc - 1);
            TIM3->ARR = arr - 1;
            TIM3->CCR3 = (arr) / 2;
            TIM3->EGR = TIM_EGR_UG; // 更新
            break;

        case MOTOR_Y:
            TIM2->CR1 &= ~TIM_CR1_CEN;
            TIM2->PSC = (psc == 0) ? 0 : (psc - 1);
            TIM2->ARR = arr - 1;
            TIM2->CCR4 = (arr) / 2;
            TIM2->EGR = TIM_EGR_UG;
            break;

        case MOTOR_Z:
            TIM4->CR1 &= ~TIM_CR1_CEN;
            TIM4->PSC = (psc == 0) ? 0 : (psc - 1);
            TIM4->ARR = arr - 1;
            TIM4->CCR4 = (arr) / 2;
            TIM4->EGR = TIM_EGR_UG;
            break;
    }
}

//==================== 电机使能控制 ====================
void Motor_SetEnable(uint8_t MotorID, uint8_t Enable)
{
    switch (MotorID)
    {
        case MOTOR_X:
            (Enable)?GPIO_ResetBits(MOTOR_X_GPIO_PORT,MOTOR_X_EN_PIN):GPIO_SetBits(MOTOR_X_GPIO_PORT,MOTOR_X_EN_PIN);
            break;
        case MOTOR_Y:
            (Enable)?GPIO_ResetBits(MOTOR_Y_GPIO_PORT,MOTOR_Y_EN_PIN):GPIO_SetBits(MOTOR_Y_GPIO_PORT,MOTOR_Y_EN_PIN);
            break;
        case MOTOR_Z:
            (Enable)?GPIO_ResetBits(MOTOR_Z_GPIO_PORT,MOTOR_Z_EN_PIN):GPIO_SetBits(MOTOR_Z_GPIO_PORT,MOTOR_Z_EN_PIN);
            break;
    }
}

//==================== 电机方向控制 ====================
void Motor_SetDirection(uint8_t MotorID, uint8_t Direction)
{
    switch (MotorID)
    {
        case MOTOR_X:
            if (Direction) { GPIO_SetBits(MOTOR_X_GPIO_PORT, MOTOR_X1_DIR_PIN); GPIO_ResetBits(MOTOR_X_GPIO_PORT, MOTOR_X2_DIR_PIN); }
            else           { GPIO_ResetBits(MOTOR_X_GPIO_PORT, MOTOR_X1_DIR_PIN); GPIO_SetBits(MOTOR_X_GPIO_PORT, MOTOR_X2_DIR_PIN); }
            break;
        case MOTOR_Y:
            (Direction) ? GPIO_SetBits(MOTOR_Y_GPIO_PORT, MOTOR_Y_DIR_PIN) : GPIO_ResetBits(MOTOR_Y_GPIO_PORT, MOTOR_Y_DIR_PIN);
            break;
        case MOTOR_Z:
            (Direction) ? GPIO_SetBits(MOTOR_Z_GPIO_PORT, MOTOR_Z_DIR_PIN) : GPIO_ResetBits(MOTOR_Z_GPIO_PORT, MOTOR_Z_DIR_PIN);
            break;
    }
}

//==================== 电机移动 ====================
void Stepper_Move(uint8_t MotorID, int32_t target_pulse)
{
    int32_t diff;
    switch (MotorID)
    {
        case MOTOR_X:
            diff = target_pulse - motor_pos_x;
            if (diff == 0) return;
            dir_x = (diff > 0) ? 1 : 0;
            target_steps_x = (diff > 0) ? (uint32_t)diff : (uint32_t)(-diff);
            step_count_x = 0;
            Motor_SetDirection(MOTOR_X, dir_x);
            Motor_SetEnable(MOTOR_X, M_ENABLE);
            TIM_ClearFlag(TIM3, TIM_FLAG_Update);              
            TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
            TIM3->CNT = 0;
            TIM_Cmd(TIM3, ENABLE); // 启动计数器 -> 硬件开始输出 PWM
            break;

        case MOTOR_Y:
            diff = target_pulse - motor_pos_y;
            if (diff == 0) return;
            dir_y = (diff > 0) ? 1 : 0;
            target_steps_y = (diff > 0) ? (uint32_t)diff : (uint32_t)(-diff);
            step_count_y = 0;
            Motor_SetDirection(MOTOR_Y, dir_y);
            Motor_SetEnable(MOTOR_Y, M_ENABLE);
            TIM_ClearFlag(TIM2, TIM_FLAG_Update);
            TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE); // 比较中断（CH4）
            TIM2->CNT = 0;
            TIM_Cmd(TIM2, ENABLE);
            break;

        case MOTOR_Z:
            diff = target_pulse - motor_pos_z;
            if (diff == 0) return;
            dir_z = (diff > 0) ? 1 : 0;
            target_steps_z = (diff > 0) ? (uint32_t)diff : (uint32_t)(-diff);
            step_count_z = 0;
            Motor_SetDirection(MOTOR_Z, dir_z);
            Motor_SetEnable(MOTOR_Z, M_ENABLE);
            TIM_ClearFlag(TIM4, TIM_FLAG_Update);
            TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE); // 比较中断（CH4）
            TIM4->CNT = 0;
            TIM_Cmd(TIM4, ENABLE);
            break;
    }
}

void Stepper_Stop(uint8_t MotorID)
{
    switch (MotorID)
    {
        case MOTOR_X:
            TIM_ITConfig(TIM3, TIM_IT_Update, DISABLE);
						TIM_Cmd(TIM3, DISABLE);
            Motor_SetEnable(MOTOR_X, M_DISABLE);
            break;
        case MOTOR_Y:
            TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);
						TIM_Cmd(TIM2, DISABLE);
            Motor_SetEnable(MOTOR_Y, M_DISABLE);
            break;
        case MOTOR_Z:
            TIM_ITConfig(TIM4, TIM_IT_Update, DISABLE);
						TIM_Cmd(TIM4, DISABLE);
            Motor_SetEnable(MOTOR_Z, M_DISABLE);
            break;
    }
}

//==================== 中断处理函数 ====================
void TIM3_IRQHandler(void)
{
    // X = TIM3_CH3 -> CC3
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

        step_count_x++;
        motor_pos_x += (dir_x ? 1 : -1);

        if (step_count_x >= target_steps_x)
        {
            TIM_Cmd(TIM3, DISABLE);
            TIM_ITConfig(TIM3, TIM_IT_Update, DISABLE);

            if (cb_x) cb_x(MOTOR_X); // 回调（ISR 中调用）
        }
    }
}

void TIM2_IRQHandler(void)
{
    // Y = TIM2_CH4 -> CC4
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        step_count_y++;
        motor_pos_y += (dir_y ? 1 : -1);

        if (step_count_y >= target_steps_y)
        {
            TIM_Cmd(TIM2, DISABLE);
            TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);

            if (cb_y) cb_y(MOTOR_Y);
        }
    }
}

void TIM4_IRQHandler(void)
{
    // Z = TIM4_CH4 -> CC4
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

        step_count_z++;
        motor_pos_z += (dir_z ? 1 : -1);

        if (step_count_z >= target_steps_z)
        {
            TIM_Cmd(TIM4, DISABLE);
            TIM_ITConfig(TIM4, TIM_IT_Update, DISABLE);

            if (cb_z) cb_z(MOTOR_Z);
        }
    }
}
