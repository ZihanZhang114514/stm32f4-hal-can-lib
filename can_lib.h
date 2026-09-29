/**
 ******************************************************************************
 * @file    can_lib.h
 * @brief   STM32F4 CAN 收发封装库
 *
 * 功能：
 *   - CAN 启动 / 接收中断使能（位时序等外设参数由 CubeMX 配置）
 *   - 接收滤波器配置（标准帧 / 扩展帧，ID 掩码模式）
 *   - 发送（标准帧 / 扩展帧 / 遥控帧，带超时等待邮箱）
 *   - 接收：轮询直读 FIFO，或中断自动入队后从队列读取
 *   - 接收回调注册（中断中收到消息立即回调）
 *
 * 依赖：stm32f4xx_hal.h；对应的 CAN IRQ 处理函数中需调用 HAL_CAN_IRQHandler()。
 ******************************************************************************
 */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_LIB_H
#define __CAN_LIB_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Exported constants --------------------------------------------------------*/
/* ID 类型（兼容 HAL 的 CAN_ID_STD / CAN_ID_EXT 取值） */
#define CAN_LIB_ID_STD CAN_ID_STD
#define CAN_LIB_ID_EXT CAN_ID_EXT

/* 帧类型（兼容 HAL 的 CAN_RTR_DATA / CAN_RTR_REMOTE 取值） */
#define CAN_LIB_FRAME_DATA CAN_RTR_DATA
#define CAN_LIB_FRAME_REMOTE CAN_RTR_REMOTE

/* 接收 FIFO 选择（滤波器绑定 / 轮询接收通用） */
#define CAN_LIB_FIFO0 CAN_RX_FIFO0
#define CAN_LIB_FIFO1 CAN_RX_FIFO1

/* 库内部参数 */
#define CAN_LIB_RX_QUEUE_SIZE 16U /*!< 每个实例的接收队列深度 */
#define CAN_LIB_MAX_INSTANCES 2U  /*!< 最大 CAN 实例数（CAN1/CAN2） */

    /* Exported types ------------------------------------------------------------*/
    /**
     * @brief CAN 接收滤波器参数（32 位 ID 掩码模式）
     */
    typedef struct
    {
        uint32_t Bank;      /*!< 滤波器编号 0 ~ 27（CAN1 用 0~13，CAN2 用 14~27） */
        uint32_t IdType;    /*!< ID 类型：CAN_LIB_ID_STD / CAN_LIB_ID_EXT */
        uint32_t FrameType; /*!< 帧类型：CAN_LIB_FRAME_DATA / CAN_LIB_FRAME_REMOTE */
        uint32_t Id;        /*!< 接收 ID */
        uint32_t MaskId;    /*!< ID 掩码（该位为 0 表示不关心） */
        uint32_t Fifo;      /*!< 绑定的 FIFO：CAN_LIB_FIFO0 / CAN_LIB_FIFO1 */
    } CAN_Lib_FilterTypeDef;

    /**
     * @brief CAN 接收消息（帧头 + 数据）
     */
    typedef struct
    {
        CAN_RxHeaderTypeDef Header; /*!< 接收帧头信息 */
        uint8_t Data[8];            /*!< 接收数据 */
    } CAN_Lib_RxMsgTypeDef;

    /**
     * @brief CAN 接收回调函数类型（中断中调用，需短小快速）
     */
    typedef void (*CAN_Lib_RxCallback)(CAN_HandleTypeDef *hcan, CAN_Lib_RxMsgTypeDef *msg);

    /* Exported functions prototypes ---------------------------------------------*/
    /**
     * @brief  初始化库并启动 CAN：注册实例 + 启动 + 使能 FIFO0/FIFO1 接收中断
     * @note   位时序（Prescaler / BS1 / BS2 / SJW 等）由 CubeMX 生成的 MX_CANx_Init()
     *         配置，调用本函数前需已完成外设初始化
     * @param  hcan CAN 句柄（需已定义，如全局 hcan1）
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_Init(CAN_HandleTypeDef *hcan);

    /**
     * @brief  配置接收滤波器（32 位 ID 掩码模式）
     * @param  hcan CAN 句柄
     * @param  cfg  滤波器参数
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_FilterConfig(CAN_HandleTypeDef *hcan, CAN_Lib_FilterTypeDef *cfg);

    /**
     * @brief  发送一帧 CAN 消息（标准帧 / 扩展帧 / 遥控帧通用）
     * @param  hcan    CAN 句柄
     * @param  id      ID（标准帧 0~0x7FF，扩展帧 0~0x1FFFFFFF）
     * @param  ide     CAN_LIB_ID_STD / CAN_LIB_ID_EXT
     * @param  rtr     CAN_LIB_FRAME_DATA / CAN_LIB_FRAME_REMOTE
     * @param  data    数据指针（遥控帧可为 NULL）
     * @param  len     数据长度 0~8
     * @param  timeout 等待空闲邮箱的超时时间(ms)，0=不等待，HAL_MAX_DELAY=无限等待
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_Send(CAN_HandleTypeDef *hcan, uint32_t id, uint32_t ide, uint32_t rtr,
                                   uint8_t *data, uint8_t len, uint32_t timeout);

    /**
     * @brief  便捷发送：标准帧 + 数据帧
     * @param  hcan    CAN 句柄
     * @param  id      标准帧 ID（0 ~ 0x7FF）
     * @param  data    数据指针
     * @param  len     数据长度 0~8
     * @param  timeout 等待空闲邮箱的超时时间(ms)
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_SendStdData(CAN_HandleTypeDef *hcan, uint32_t id, uint8_t *data,
                                          uint8_t len, uint32_t timeout);

    /**
     * @brief  轮询接收：直读指定 FIFO（适合不使用中断收发的场景）
     * @param  hcan    CAN 句柄
     * @param  fifo    CAN_LIB_FIFO0 / CAN_LIB_FIFO1
     * @param  msg     接收消息输出
     * @param  timeout 等待超时(ms)，0=不等待，HAL_MAX_DELAY=无限等待
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_Receive(CAN_HandleTypeDef *hcan, uint32_t fifo,
                                      CAN_Lib_RxMsgTypeDef *msg, uint32_t timeout);

    /**
     * @brief  从内部接收队列取一条消息（需已通过 CAN_Lib_Init 使能接收中断）
     * @note   中断收到的消息会自动入队，本函数按 FIFO 顺序出队
     * @param  hcan    CAN 句柄
     * @param  msg     接收消息输出
     * @param  timeout 等待超时(ms)，0=不等待，HAL_MAX_DELAY=无限等待
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_ReceiveMsg(CAN_HandleTypeDef *hcan, CAN_Lib_RxMsgTypeDef *msg,
                                         uint32_t timeout);

    /**
     * @brief  获取内部接收队列中待取的消息数
     * @param  hcan CAN 句柄
     * @retval 队列中消息数
     */
    uint32_t CAN_Lib_RxCount(CAN_HandleTypeDef *hcan);

    /**
     * @brief  注册接收回调：中断收到消息并入队后立即调用（可在回调中直接处理）
     * @param  hcan CAN 句柄
     * @param  cb   回调函数，传 NULL 取消回调
     * @retval HAL status
     */
    HAL_StatusTypeDef CAN_Lib_RegisterRxCallback(CAN_HandleTypeDef *hcan, CAN_Lib_RxCallback cb);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_LIB_H */
