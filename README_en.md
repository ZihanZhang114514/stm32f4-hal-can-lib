# STM32F4 HAL CAN Library

A lightweight CAN communication library based on the STM32 HAL driver for STM32F4 series microcontrollers.

This project was originally developed and tested on an **STM32F427** and encapsulates CAN transmission, reception, filtering, interrupt handling, and receive callbacks into a simple and reusable API.

> **Note:** You can use `dist/configure_can_lib.exe` to configure and integrate the CAN library automatically.

## Features

* Based on the STM32 HAL CAN driver
* Supports standard and extended CAN IDs
* Supports data frames and remote frames
* 32-bit ID mask filtering
* FIFO0 / FIFO1 support
* Interrupt-based reception
* Receive callback support
* Internal receive queue
* Polling-based reception
* Simple transmission APIs
* CAN bit timing configured through STM32CubeMX
* Designed to be easily reused in other STM32F4 projects

---

## Project Structure

| File                         | Description                                                     |
| ---------------------------- | --------------------------------------------------------------- |
| `Core/Inc/can_lib.h`         | CAN library header, API declarations, structures, and constants |
| `Core/Src/can_lib.c`         | CAN library implementation                                      |
| `Core/Src/main.c`            | Example application                                             |
| `Core/Src/stm32f4xx_it.c`    | CAN interrupt service routines                                  |
| `configure_can_lib.py`       | CAN library configuration script                                |
| `dist/configure_can_lib.exe` | Standalone configuration tool                                   |

The library depends on the STM32 HAL CAN driver (`stm32f4xx_hal_can.c`) but does not depend on application-specific logic. It can therefore be reused in other STM32F4 projects.

---

## Quick Start

Include the library header:

```c
#include "can_lib.h"
```

### 1. Configure CAN with STM32CubeMX

CAN bit timing parameters such as:

* Prescaler
* Time Segment 1 (BS1)
* Time Segment 2 (BS2)
* Synchronization Jump Width (SJW)

are configured through STM32CubeMX.

The generated `MX_CAN1_Init()` function must be executed before initializing the CAN library.

---

### 2. Configure the Receive Filter

The following example configures a 32-bit ID mask filter:

```c
CAN_Lib_FilterTypeDef can_filter = {
    .Bank      = 13,                    /* Filter bank: 0~27 */
    .IdType    = CAN_LIB_ID_STD,        /* Standard CAN frame */
    .FrameType = CAN_LIB_FRAME_DATA,     /* Data frame */
    .Id        = 0x114,                  /* Receive ID */
    .MaskId    = 0x7FF,                  /* Mask: 0 means "don't care" */
    .Fifo      = CAN_LIB_FIFO1,          /* Use FIFO1 */
};
```

---

### 3. Initialize the CAN Library

```c
CAN_Lib_Init(&hcan1);
CAN_Lib_FilterConfig(&hcan1, &can_filter);
```

`CAN_Lib_Init()` starts the CAN peripheral and enables reception interrupts for FIFO0 and FIFO1.

---

### 4. Register a Receive Callback

You can optionally register a callback function:

```c
CAN_Lib_RegisterRxCallback(&hcan1, MyRxCallback);
```

Example:

```c
static void MyRxCallback(CAN_HandleTypeDef *hcan,
                         CAN_Lib_RxMsgTypeDef *msg)
{
    (void)hcan;

    /* msg->Header contains StdId/ExtId/IDE/RTR/DLC, etc. */

    uint8_t first_byte = msg->Data[0];

    /* Your processing code */
}
```

> The callback runs in interrupt context. Keep it short and avoid blocking operations.

---

### 5. Send a Standard CAN Data Frame

```c
uint8_t data[8] = {0x01};

CAN_Lib_SendStdData(&hcan1, 0x114, data, 1, 0);
```

---

# API Reference

All APIs return `HAL_StatusTypeDef`.

| Return Value  | Meaning                               |
| ------------- | ------------------------------------- |
| `HAL_OK`      | Operation successful                  |
| `HAL_ERROR`   | Invalid parameter or operation failed |
| `HAL_TIMEOUT` | Operation timed out                   |

---

## Initialization and Configuration

### `CAN_Lib_Init`

```c
HAL_StatusTypeDef CAN_Lib_Init(CAN_HandleTypeDef *hcan);
```

Initializes and starts the CAN library:

1. Registers the CAN instance
2. Calls `HAL_CAN_Start()`
3. Enables FIFO0 receive interrupts
4. Enables FIFO1 receive interrupts

CAN bit timing is configured by STM32CubeMX and generated through `MX_CANx_Init()`.

Make sure `MX_CANx_Init()` has already been executed before calling this function.

---

### `CAN_Lib_FilterConfig`

```c
HAL_StatusTypeDef CAN_Lib_FilterConfig(
    CAN_HandleTypeDef *hcan,
    CAN_Lib_FilterTypeDef *cfg
);
```

Configures a CAN receive filter.

The library uses:

```c
CAN_FILTERMODE_IDMASK
CAN_FILTERSCALE_32BIT
```

The supported ID ranges are:

* Standard CAN ID: 11 bits (`0x000` ~ `0x7FF`)
* Extended CAN ID: 29 bits (`0x00000000` ~ `0x1FFFFFFF`)

For the mask:

* `0` means the corresponding ID bit is ignored
* `1` means the corresponding ID bit must match

The `Fifo` parameter determines whether matching messages are routed to FIFO0 or FIFO1.

---

# Transmission

## `CAN_Lib_Send`

```c
HAL_StatusTypeDef CAN_Lib_Send(
    CAN_HandleTypeDef *hcan,
    uint32_t id,
    uint32_t ide,
    uint32_t rtr,
    uint8_t *data,
    uint8_t len,
    uint32_t timeout
);
```

Generic CAN transmission function.

### Parameters

`ide`:

```c
CAN_LIB_ID_STD
CAN_LIB_ID_EXT
```

`rtr`:

```c
CAN_LIB_FRAME_DATA
CAN_LIB_FRAME_REMOTE
```

`len`:

```text
0 ~ 8 bytes
```

---

## `CAN_Lib_SendStdData`

```c
HAL_StatusTypeDef CAN_Lib_SendStdData(
    CAN_HandleTypeDef *hcan,
    uint32_t id,
    uint8_t *data,
    uint8_t len,
    uint32_t timeout
);
```

A convenient API for sending a standard CAN data frame.

---

## Timeout

The `timeout` parameter is used by both transmission and reception APIs.

| Value           | Meaning                           |
| --------------- | --------------------------------- |
| `0`             | Do not wait; return immediately   |
| `n > 0`         | Wait for at most `n` milliseconds |
| `HAL_MAX_DELAY` | Wait indefinitely                 |

> Avoid using `HAL_MAX_DELAY` for transmission when the CAN bus may be disconnected or have no acknowledgement. Otherwise, the application may remain blocked while the CAN controller repeatedly attempts transmission.

---

# Reception

The library provides three reception methods.

## 1. Interrupt Callback

**Recommended when low latency is required.**

```c
HAL_StatusTypeDef CAN_Lib_RegisterRxCallback(
    CAN_HandleTypeDef *hcan,
    CAN_Lib_RxCallback cb
);
```

After registration, the callback is invoked immediately when a CAN message is received.

```c
CAN_Lib_RegisterRxCallback(&hcan1, MyRxCallback);
```

> The callback executes in interrupt context. Do not perform blocking operations or long-running processing inside the callback.

---

## 2. Internal Receive Queue

```c
HAL_StatusTypeDef CAN_Lib_ReceiveMsg(
    CAN_HandleTypeDef *hcan,
    CAN_Lib_RxMsgTypeDef *msg,
    uint32_t timeout
);

uint32_t CAN_Lib_RxCount(
    CAN_HandleTypeDef *hcan
);
```

Received messages are automatically stored in an internal ring buffer.

The default queue depth is:

```c
CAN_LIB_RX_QUEUE_SIZE
```

with a default value of **16**.

Messages can then be retrieved from the main loop in FIFO order:

```c
CAN_Lib_ReceiveMsg(...)
```

The number of pending messages can be checked with:

```c
CAN_Lib_RxCount(...)
```

If the queue is full, newly received messages are discarded.

---

## 3. Polling Reception

```c
HAL_StatusTypeDef CAN_Lib_Receive(
    CAN_HandleTypeDef *hcan,
    uint32_t fifo,
    CAN_Lib_RxMsgTypeDef *msg,
    uint32_t timeout
);
```

This method does not require CAN receive interrupts.

It directly polls the specified FIFO:

```c
CAN_LIB_FIFO0
CAN_LIB_FIFO1
```

---

# Data Structures

## `CAN_Lib_FilterTypeDef`

```c
typedef struct {
    uint32_t Bank;              /* 0~27 */
    uint32_t IdType;            /* CAN_LIB_ID_STD / CAN_LIB_ID_EXT */
    uint32_t FrameType;         /* CAN_LIB_FRAME_DATA / CAN_LIB_FRAME_REMOTE */
    uint32_t Id;                /* Receive ID */
    uint32_t MaskId;            /* ID mask */
    uint32_t Fifo;              /* CAN_LIB_FIFO0 / CAN_LIB_FIFO1 */
} CAN_Lib_FilterTypeDef;
```

---

## `CAN_Lib_RxMsgTypeDef`

```c
typedef struct {
    CAN_RxHeaderTypeDef Header; /* StdId/ExtId/IDE/RTR/DLC, etc. */
    uint8_t             Data[8];
} CAN_Lib_RxMsgTypeDef;
```

---

# CAN Bit Timing Configuration

CAN bit timing is configured through STM32CubeMX:

```text
Connectivity → CAN1 → Parameter Settings
```

Typical parameters include:

* Prescaler
* Time Quanta in Bit Segment 1
* Time Quanta in Bit Segment 2
* Resynchronization Jump Width

The CAN baud rate can be calculated using:

```text
Baud Rate = PCLK1 / Prescaler / (1 + BS1 + BS2)
```

For the current example project:

```text
PCLK1 = 42 MHz
Prescaler = 9
BS1 = 2 TQ
BS2 = 2 TQ
SJW = 1 TQ
```

Therefore:

```text
42 MHz / 9 / (1 + 2 + 2) ≈ 933 kbps
```

For a 1 Mbps CAN bus, the CubeMX configuration can be changed to:

```text
Prescaler = 7
BS1       = 3 TQ
BS2       = 2 TQ
```

which gives:

```text
42 MHz / 7 / (1 + 3 + 2) = 1 Mbps
```

---

# Important Notes

### 1. Do not redefine HAL CAN receive callbacks

The library already defines:

```c
HAL_CAN_RxFifo0MsgPendingCallback
HAL_CAN_RxFifo1MsgPendingCallback
```

Do not define these functions again in your application.

Instead, use:

```c
CAN_Lib_RegisterRxCallback()
```

or the internal receive queue API.

---

### 2. Keep the CAN interrupt handlers

The following interrupt handlers must remain in `stm32f4xx_it.c`:

```c
CAN1_RX0_IRQHandler
CAN1_RX1_IRQHandler
```

They must call:

```c
HAL_CAN_IRQHandler(&hcan1);
```

---

### 3. Be careful with interrupt context

Registered receive callbacks execute inside the interrupt context.

Avoid:

* Blocking operations
* Long delays
* Blocking CAN transmission
* Time-consuming processing

For more complex processing, use the internal receive queue and process messages from the main loop instead.

---

### 4. Add the library to your project

Add:

```text
can_lib.c
```

to your Keil project build group.

Make sure the directory containing:

```text
can_lib.h
```

is included in the compiler include paths.

For the example project, `Core/Inc` is already included.

---

### 5. Avoid indefinite transmission waits

When the CAN bus has no acknowledgement or is physically disconnected, the CAN controller may repeatedly attempt transmission.

Therefore, it is recommended to use a finite timeout instead of:

```c
HAL_MAX_DELAY
```

for CAN transmission.

---

# Configuration Tool

The repository provides a standalone configuration tool:

```text
dist/configure_can_lib.exe
```

You can use it to configure and integrate the CAN library without manually editing all configuration files.

The Python source is also provided:

```text
configure_can_lib.py
```

---

# License

This project is released under the **WTFPL** license.

See [`LICENSE`](LICENSE) for details.

---

# Author

**Zihan Zhang**

GitHub: [@ZihanZhang114514](https://github.com/ZihanZhang114514)

Repository: [stm32f4-hal-can-lib](https://github.com/ZihanZhang114514/stm32f4-hal-can-lib)
