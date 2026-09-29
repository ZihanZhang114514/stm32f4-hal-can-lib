/**
 ******************************************************************************
 * @file    can_lib.c
 * @brief   STM32F4 CAN 收发封装库实现
 *
 * 说明：
 *   - 外设位时序（波特率 / Prescaler / BS1 / BS2 / SJW 等）由 CubeMX 配置并
 *     生成 MX_CANx_Init()，库不再提供这些参数的配置接口。
 *   - 库强定义了 HAL 的 HAL_CAN_RxFifo0MsgPendingCallback /
 *     HAL_CAN_RxFifo1MsgPendingCallback 弱回调，中断消息自动存入内部环形队列，
 *     并转发给用户通过 CAN_Lib_RegisterRxCallback 注册的回调。
 *   - 用户工程中不要再自行定义上述两个 HAL 回调，否则会重复定义。
 *   - CAN IRQ 处理函数（如 CAN1_RX0_IRQHandler）中仍需调用 HAL_CAN_IRQHandler()。
 ******************************************************************************
 */
#include "can_lib.h"

/* Private typedef -----------------------------------------------------------*/
/* 内部实例表（每个 CAN 外设一个实例） */
typedef struct
{
    uint8_t used;                                      /* 槽位是否占用 */
    CAN_HandleTypeDef *hcan;                           /* CAN 句柄 */
    CAN_Lib_RxMsgTypeDef queue[CAN_LIB_RX_QUEUE_SIZE]; /* 接收环形队列 */
    volatile uint8_t head;                             /* 写入位置（中断上下文） */
    volatile uint8_t tail;                             /* 读取位置（用户上下文） */
    volatile uint8_t count;                            /* 队列中消息数 */
    CAN_Lib_RxCallback cb;                             /* 用户注册的回调 */
} CAN_Lib_InstanceTypeDef;

/* Private variables ---------------------------------------------------------*/
static CAN_Lib_InstanceTypeDef s_can_inst[CAN_LIB_MAX_INSTANCES];

/* Private function prototypes -----------------------------------------------*/
static CAN_Lib_InstanceTypeDef *CAN_Lib_GetInstance(CAN_HandleTypeDef *hcan);
static CAN_Lib_InstanceTypeDef *CAN_Lib_AllocInstance(CAN_HandleTypeDef *hcan);
static HAL_StatusTypeDef CAN_Lib_WaitUntil(uint32_t start_tick, uint32_t timeout);
static void CAN_Lib_PushRxMsg(CAN_HandleTypeDef *hcan, uint32_t fifo);

/* Private functions ---------------------------------------------------------*/
/**
 * @brief  在实例表中查找 hcan 对应的实例
 */
static CAN_Lib_InstanceTypeDef *CAN_Lib_GetInstance(CAN_HandleTypeDef *hcan)
{
    uint8_t i;

    for (i = 0; i < CAN_LIB_MAX_INSTANCES; i++)
    {
        if ((s_can_inst[i].used != 0U) && (s_can_inst[i].hcan == hcan))
        {
            return &s_can_inst[i];
        }
    }
    return NULL;
}

/**
 * @brief  查找实例，不存在则分配一个空闲槽位
 */
static CAN_Lib_InstanceTypeDef *CAN_Lib_AllocInstance(CAN_HandleTypeDef *hcan)
{
    CAN_Lib_InstanceTypeDef *inst;
    uint8_t i;

    inst = CAN_Lib_GetInstance(hcan);
    if (inst != NULL)
    {
        return inst;
    }

    for (i = 0; i < CAN_LIB_MAX_INSTANCES; i++)
    {
        if (s_can_inst[i].used == 0U)
        {
            s_can_inst[i].used = 1U;
            s_can_inst[i].hcan = hcan;
            s_can_inst[i].head = 0U;
            s_can_inst[i].tail = 0U;
            s_can_inst[i].count = 0U;
            s_can_inst[i].cb = NULL;
            return &s_can_inst[i];
        }
    }
    return NULL;
}

/**
 * @brief  超时判断：未超时返回 HAL_OK，超时返回 HAL_TIMEOUT
 * @note   timeout = HAL_MAX_DELAY 表示无限等待
 */
static HAL_StatusTypeDef CAN_Lib_WaitUntil(uint32_t start_tick, uint32_t timeout)
{
    if (timeout == HAL_MAX_DELAY)
    {
        return HAL_OK;
    }
    if ((HAL_GetTick() - start_tick) >= timeout)
    {
        return HAL_TIMEOUT;
    }
    return HAL_OK;
}

/**
 * @brief  读取 FIFO 消息：入队并转发给用户回调
 */
static void CAN_Lib_PushRxMsg(CAN_HandleTypeDef *hcan, uint32_t fifo)
{
    CAN_Lib_InstanceTypeDef *inst = CAN_Lib_GetInstance(hcan);
    CAN_Lib_RxMsgTypeDef msg;

    if (inst == NULL)
    {
        return;
    }

    if (HAL_CAN_GetRxMessage(hcan, fifo, &msg.Header, msg.Data) != HAL_OK)
    {
        return;
    }

    /* 入队（队列满则丢弃新消息） */
    if (inst->count < CAN_LIB_RX_QUEUE_SIZE)
    {
        inst->queue[inst->head] = msg;
        inst->head = (uint8_t)((inst->head + 1U) % CAN_LIB_RX_QUEUE_SIZE);
        inst->count++;
    }

    /* 转发给用户回调 */
    if (inst->cb != NULL)
    {
        inst->cb(hcan, &msg);
    }
}

/* Exported functions --------------------------------------------------------*/
/**
 * @brief  初始化库并启动 CAN：注册实例 + 启动 + 使能接收中断
 * @note   外设位时序等参数由 CubeMX 生成的 MX_CANx_Init() 配置，
 *         调用本函数前需已完成外设初始化（HAL_CAN_Init 已执行）
 */
HAL_StatusTypeDef CAN_Lib_Init(CAN_HandleTypeDef *hcan)
{
    if (hcan == NULL)
    {
        return HAL_ERROR;
    }

    if (CAN_Lib_AllocInstance(hcan) == NULL)
    {
        return HAL_ERROR; /* 实例表已满 */
    }

    /* 启动 CAN */
    if (HAL_CAN_Start(hcan) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 使能 FIFO0 / FIFO1 接收中断 */
    __HAL_CAN_ENABLE_IT(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    __HAL_CAN_ENABLE_IT(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);

    return HAL_OK;
}

/**
 * @brief  配置接收滤波器（32 位 ID 掩码模式）
 * @note   标准帧：ID[10:0] 放在 FilterIdHigh[15:5]；扩展帧：ID[28:13] 放
 *         FilterIdHigh、ID[12:0] 放在 FilterIdLow[15:3]。掩码只过滤 ID 位。
 */
HAL_StatusTypeDef CAN_Lib_FilterConfig(CAN_HandleTypeDef *hcan, CAN_Lib_FilterTypeDef *cfg)
{
    CAN_FilterTypeDef filter;

    if ((hcan == NULL) || (cfg == NULL) || (cfg->Bank > 27U) ||
        ((cfg->Fifo != CAN_FILTER_FIFO0) && (cfg->Fifo != CAN_FILTER_FIFO1)))
    {
        return HAL_ERROR;
    }

    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterActivation = ENABLE;
    filter.FilterBank = cfg->Bank;
    filter.FilterFIFOAssignment = cfg->Fifo;
    filter.SlaveStartFilterBank = 14U;

    if (cfg->IdType == CAN_LIB_ID_EXT)
    {
        /* 扩展帧：ID[28:13] -> FilterIdHigh，ID[12:0] -> FilterIdLow[15:3]，
           IDE 位在 FilterIdLow bit2，RTR 位在 bit1 */
        filter.FilterIdHigh = (uint16_t)((cfg->Id >> 13) & 0xFFFFU);
        filter.FilterIdLow = (uint16_t)(((cfg->Id & 0x1FFFU) << 3) | (1U << 2) |
                                        ((cfg->FrameType == CAN_LIB_FRAME_REMOTE) ? (1U << 1) : 0U));
        filter.FilterMaskIdHigh = (uint16_t)((cfg->MaskId >> 13) & 0xFFFFU);
        filter.FilterMaskIdLow = (uint16_t)((cfg->MaskId & 0x1FFFU) << 3);
    }
    else
    {
        /* 标准帧：ID[10:0] -> FilterIdHigh[15:5]，RTR 位在 bit4 */
        filter.FilterIdHigh = (uint16_t)(((cfg->Id & 0x7FFU) << 5) |
                                         ((cfg->FrameType == CAN_LIB_FRAME_REMOTE) ? (1U << 4) : 0U));
        filter.FilterIdLow = 0x0000U;
        filter.FilterMaskIdHigh = (uint16_t)((cfg->MaskId & 0x7FFU) << 5);
        filter.FilterMaskIdLow = 0x0000U;
    }

    return HAL_CAN_ConfigFilter(hcan, &filter);
}

/**
 * @brief  发送一帧 CAN 消息（标准帧 / 扩展帧 / 遥控帧通用）
 */
HAL_StatusTypeDef CAN_Lib_Send(CAN_HandleTypeDef *hcan, uint32_t id, uint32_t ide, uint32_t rtr,
                               uint8_t *data, uint8_t len, uint32_t timeout)
{
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;
    uint32_t start_tick;
    HAL_StatusTypeDef status;

    if ((hcan == NULL) || (len > 8U) || ((data == NULL) && (len != 0U)))
    {
        return HAL_ERROR;
    }

    header.DLC = len;
    header.IDE = ide;
    header.RTR = rtr;
    header.StdId = (ide == CAN_LIB_ID_EXT) ? 0U : id;
    header.ExtId = (ide == CAN_LIB_ID_EXT) ? id : 0U;
    header.TransmitGlobalTime = DISABLE;

    start_tick = HAL_GetTick();

    /* 循环尝试发送，直到成功或超时 */
    do
    {
        status = HAL_CAN_AddTxMessage(hcan, &header, data, &mailbox);
        if (status == HAL_OK)
        {
            return HAL_OK;
        }
    } while (CAN_Lib_WaitUntil(start_tick, timeout) == HAL_OK);

    return status;
}

/**
 * @brief  便捷发送：标准帧 + 数据帧
 */
HAL_StatusTypeDef CAN_Lib_SendStdData(CAN_HandleTypeDef *hcan, uint32_t id, uint8_t *data,
                                      uint8_t len, uint32_t timeout)
{
    return CAN_Lib_Send(hcan, id, CAN_LIB_ID_STD, CAN_LIB_FRAME_DATA, data, len, timeout);
}

/**
 * @brief  轮询接收：直读指定 FIFO
 */
HAL_StatusTypeDef CAN_Lib_Receive(CAN_HandleTypeDef *hcan, uint32_t fifo,
                                  CAN_Lib_RxMsgTypeDef *msg, uint32_t timeout)
{
    uint32_t start_tick;

    if ((hcan == NULL) || (msg == NULL) ||
        ((fifo != CAN_RX_FIFO0) && (fifo != CAN_RX_FIFO1)))
    {
        return HAL_ERROR;
    }

    start_tick = HAL_GetTick();

    do
    {
        if (HAL_CAN_GetRxMessage(hcan, fifo, &msg->Header, msg->Data) == HAL_OK)
        {
            return HAL_OK;
        }
    } while (CAN_Lib_WaitUntil(start_tick, timeout) == HAL_OK);

    return HAL_TIMEOUT;
}

/**
 * @brief  从内部接收队列取一条消息
 */
HAL_StatusTypeDef CAN_Lib_ReceiveMsg(CAN_HandleTypeDef *hcan, CAN_Lib_RxMsgTypeDef *msg,
                                     uint32_t timeout)
{
    CAN_Lib_InstanceTypeDef *inst = CAN_Lib_GetInstance(hcan);
    uint32_t start_tick;

    if ((inst == NULL) || (msg == NULL))
    {
        return HAL_ERROR;
    }

    start_tick = HAL_GetTick();

    do
    {
        if (inst->count > 0U)
        {
            *msg = inst->queue[inst->tail];
            inst->tail = (uint8_t)((inst->tail + 1U) % CAN_LIB_RX_QUEUE_SIZE);
            inst->count--;
            return HAL_OK;
        }
    } while (CAN_Lib_WaitUntil(start_tick, timeout) == HAL_OK);

    return HAL_TIMEOUT;
}

/**
 * @brief  获取内部接收队列中待取的消息数
 */
uint32_t CAN_Lib_RxCount(CAN_HandleTypeDef *hcan)
{
    CAN_Lib_InstanceTypeDef *inst = CAN_Lib_GetInstance(hcan);

    if (inst == NULL)
    {
        return 0U;
    }
    return (uint32_t)inst->count;
}

/**
 * @brief  注册接收回调
 */
HAL_StatusTypeDef CAN_Lib_RegisterRxCallback(CAN_HandleTypeDef *hcan, CAN_Lib_RxCallback cb)
{
    CAN_Lib_InstanceTypeDef *inst = CAN_Lib_GetInstance(hcan);

    if (inst == NULL)
    {
        return HAL_ERROR;
    }

    inst->cb = cb;
    return HAL_OK;
}

/**
 * @brief  HAL CAN FIFO0 消息挂起回调（库内部：入队并转发）
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_Lib_PushRxMsg(hcan, CAN_RX_FIFO0);
}

/**
 * @brief  HAL CAN FIFO1 消息挂起回调（库内部：入队并转发）
 */
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_Lib_PushRxMsg(hcan, CAN_RX_FIFO1);
}
