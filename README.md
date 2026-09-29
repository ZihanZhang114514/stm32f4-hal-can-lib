# RM_Aboard_CAN_test

基于 STM32F427 的 CAN 总线测试工程，将 CAN 的发送与接收封装为独立库 `can_lib`。外设位时序（波特率、Prescaler、BS1/BS2、SJW 等）由 CubeMX 配置，库通过简洁的 API 完成启动、滤波配置、收发与中断接收。

## 文件结构

| 文件                      | 说明                                        |
| ------------------------- | ------------------------------------------- |
| `Core/Inc/can_lib.h`      | CAN 库头文件（API 声明、参数结构体、常量）  |
| `Core/Src/can_lib.c`      | CAN 库实现（启动 / 发送 / 接收 / 中断回调） |
| `Core/Src/main.c`         | 应用示例：启动 CAN、周期发送、接收回调点灯  |
| `Core/Src/stm32f4xx_it.c` | 中断服务函数（`CAN1_RX0/RX1_IRQHandler`）   |

> 库依赖 HAL 的 CAN 驱动（`stm32f4xx_hal_can.c`），不依赖具体应用逻辑，可整体复用到其他 STM32F4 工程。

## 快速开始

```c
#include "can_lib.h"

/* 0. 外设初始化：位时序（波特率 / Prescaler / BS1 / BS2 / SJW 等）在 CubeMX
 *    中配置，生成的 MX_CAN1_Init() 已在 main() 里调用 */

/* 1. 接收滤波器（32 位 ID 掩码模式） */
CAN_Lib_FilterTypeDef can_filter = {
    .Bank      = 13,                    /* 滤波器编号 0~27 */
    .IdType    = CAN_LIB_ID_STD,        /* 标准帧 */
    .FrameType = CAN_LIB_FRAME_DATA,    /* 数据帧 */
    .Id        = 0x114,                 /* 接收 ID */
    .MaskId    = 0x7FF,                 /* 掩码：0 的位不关心 */
    .Fifo      = CAN_LIB_FIFO1,         /* 绑定 FIFO1 */
};

/* 2. 启动 CAN 并使能接收中断 + 配置滤波器 */
CAN_Lib_Init(&hcan1);
CAN_Lib_FilterConfig(&hcan1, &can_filter);

/* 3. 注册接收回调（可选，也可用轮询/队列方式接收） */
CAN_Lib_RegisterRxCallback(&hcan1, MyRxCallback);

/* 4. 发送标准数据帧 */
uint8_t data[8] = {0x01};
CAN_Lib_SendStdData(&hcan1, 0x114, data, 1, 0);
```

接收回调示例：

```c
static void MyRxCallback(CAN_HandleTypeDef *hcan, CAN_Lib_RxMsgTypeDef *msg)
{
    (void)hcan;
    /* msg->Header 中含 StdId/ExtId/IDE/RTR/DLC 等帧头信息 */
    uint8_t first_byte = msg->Data[0];
    /* ... */
}
```

## API 说明

所有 API 返回 `HAL_StatusTypeDef`：`HAL_OK` 成功，`HAL_ERROR` 参数错误 / 失败，`HAL_TIMEOUT` 等待超时。

### 初始化与配置

#### `CAN_Lib_Init`

```c
HAL_StatusTypeDef CAN_Lib_Init(CAN_HandleTypeDef *hcan);
```

初始化库并启动 CAN：注册实例 → `HAL_CAN_Start` → 使能 FIFO0/FIFO1 接收中断。

外设位时序（波特率、Prescaler、BS1/BS2、SJW 等）由 CubeMX 配置并生成 `MX_CANx_Init()`，库不提供这些参数的配置接口；调用本函数前需确保 `MX_CANx_Init()` 已执行（即 `HAL_CAN_Init()` 已完成）。

#### `CAN_Lib_FilterConfig`

```c
HAL_StatusTypeDef CAN_Lib_FilterConfig(CAN_HandleTypeDef *hcan, CAN_Lib_FilterTypeDef *cfg);
```

配置接收滤波器，固定使用 32 位 ID 掩码模式（`CAN_FILTERMODE_IDMASK` + `CAN_FILTERSCALE_32BIT`）。

- 标准帧：`Id` 为 11 位（0~0x7FF）；
- 扩展帧：`Id` 为 29 位（0~0x1FFFFFFF）；
- 掩码 `MaskId` 中为 0 的位不参与比较；
- `Fifo` 决定匹配的消息进入 FIFO0 还是 FIFO1。

### 发送

#### `CAN_Lib_Send`

```c
HAL_StatusTypeDef CAN_Lib_Send(CAN_HandleTypeDef *hcan, uint32_t id, uint32_t ide,
                               uint32_t rtr, uint8_t *data, uint8_t len, uint32_t timeout);
```

通用发送，`ide` 取 `CAN_LIB_ID_STD` / `CAN_LIB_ID_EXT`，`rtr` 取 `CAN_LIB_FRAME_DATA` / `CAN_LIB_FRAME_REMOTE`，`len` 为 0~8。

#### `CAN_Lib_SendStdData`

```c
HAL_StatusTypeDef CAN_Lib_SendStdData(CAN_HandleTypeDef *hcan, uint32_t id, uint8_t *data,
                                      uint8_t len, uint32_t timeout);
```

便捷发送标准帧数据帧。

**`timeout` 语义**（发送与接收通用）：

| 取值            | 含义             |
| --------------- | ---------------- |
| `0`             | 不等待，立即返回 |
| `n`（>0）       | 最多等待 `n` ms  |
| `HAL_MAX_DELAY` | 无限等待         |

### 接收

库提供三种接收方式，按需选用：

#### 1. 中断回调（推荐，实时性最好）

```c
HAL_StatusTypeDef CAN_Lib_RegisterRxCallback(CAN_HandleTypeDef *hcan, CAN_Lib_RxCallback cb);
```

注册后，中断收到消息会立即回调 `cb`（回调运行在中断上下文，应短小快速，勿在其中阻塞）。

#### 2. 内部队列

```c
HAL_StatusTypeDef CAN_Lib_ReceiveMsg(CAN_HandleTypeDef *hcan, CAN_Lib_RxMsgTypeDef *msg, uint32_t timeout);
uint32_t         CAN_Lib_RxCount(CAN_HandleTypeDef *hcan);
```

中断收到的消息自动进入内部环形队列（深度 `CAN_LIB_RX_QUEUE_SIZE`，默认 16），在主循环中用 `CAN_Lib_ReceiveMsg` 按 FIFO 顺序取出；`CAN_Lib_RxCount` 可查询待取数量。队列满时丢弃新消息。

#### 3. 轮询直读

```c
HAL_StatusTypeDef CAN_Lib_Receive(CAN_HandleTypeDef *hcan, uint32_t fifo,
                                  CAN_Lib_RxMsgTypeDef *msg, uint32_t timeout);
```

不依赖中断，直接轮询指定 FIFO（`CAN_LIB_FIFO0` / `CAN_LIB_FIFO1`）。

## 数据结构

```c
/* 滤波器参数 */
typedef struct {
    uint32_t Bank;              /* 0~27 */
    uint32_t IdType;            /* CAN_LIB_ID_STD / CAN_LIB_ID_EXT */
    uint32_t FrameType;         /* CAN_LIB_FRAME_DATA / CAN_LIB_FRAME_REMOTE */
    uint32_t Id;                /* 接收 ID */
    uint32_t MaskId;            /* ID 掩码 */
    uint32_t Fifo;              /* CAN_LIB_FIFO0 / CAN_LIB_FIFO1 */
} CAN_Lib_FilterTypeDef;

/* 接收消息 */
typedef struct {
    CAN_RxHeaderTypeDef Header; /* 帧头：StdId/ExtId/IDE/RTR/DLC 等 */
    uint8_t             Data[8];
} CAN_Lib_RxMsgTypeDef;
```

## 位时序配置（CubeMX）

位时序参数在 CubeMX 中配置（`Connectivity → CAN1 → Parameter Settings`：Prescaler、Time Quanta in Bit Segment 1/2、ReSynchronization Jump Width 等），并生成到 `MX_CAN1_Init()`，库不再提供这些参数的配置接口。

对应换算关系（供核对）：

```
波特率 = PCLK1 / Prescaler / (1 + TimeSeg1(TQ) + TimeSeg2(TQ))
```

本工程 `PCLK1 = 42 MHz`，CubeMX 中配置 `Prescaler=9`、`BS1=2TQ`、`BS2=2TQ`、`SJW=1TQ`：

```
42 MHz / 9 / (1 + 2 + 2) ≈ 933 kbps
```

如需 1 Mbps，可在 CubeMX 中改 `Prescaler=7`、`BS1=3TQ`、`BS2=2TQ`（42M / 7 / 6 = 1 Mbps）。

## 注意事项

1. **不要重复定义 HAL 回调**：库内部已强定义 `HAL_CAN_RxFifo0MsgPendingCallback` 与 `HAL_CAN_RxFifo1MsgPendingCallback`，用户代码中勿再定义同名函数，接收处理请改用 `CAN_Lib_RegisterRxCallback` 或队列 API。
2. **中断服务函数**：`stm32f4xx_it.c` 中必须保留 `CAN1_RX0_IRQHandler` / `CAN1_RX1_IRQHandler`，并调用 `HAL_CAN_IRQHandler(&hcan1)`。
3. **中断上下文**：注册的回调在中断中执行，不要在其中调用阻塞发送、长延时等操作；如需阻塞处理，改用队列方式在主循环里取。
4. **加入工程**：将 `can_lib.c` 加入 Keil 工程编译组（`Application/User/Core`），并确保 `can_lib.h` 所在目录在 Include 路径中（默认 `Core/Inc` 已包含）。
5. **发送建议**：总线异常（无应答）时 CAN 会反复仲裁重发，建议发送 `timeout` 不要用 `HAL_MAX_DELAY`，避免卡死在发送函数中。
