//------------------------------------------------------------------------------
// Module:        CTUART
// Description:   Provides buffered UART handling as a Circle task abstraction.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-27
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-27     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

#include <circle/sched/task.h>
#include <circle/spinlock.h>
#include <circle/serial.h>

/**
 * @file TUART.h
 * @brief Declares the cooperative UART task abstraction.
 * @details CTUART hides Circle's low-level serial device behind a task wrapper.
 * It primarily manages the serial device initialization and provides access for
 * higher layers to drain the hardware FIFO.
 */

/**
 * @class CTUART
 * @brief Task responsible for UART initialization and data retrieval.
 * @details The singleton wraps the serial device. It exposes a hook so higher
 * layers can drain received data directly from the hardware FIFO using
 * interrupt-safe mechanisms in the kernel.
 */
class CTUART : public CTask
{
public:
    /// \brief Access the singleton UART task instance.
    static CTUART *Get(void);

    /**
     * @brief Construct a CTUART task object.
     */
    CTUART();

    /**
     * @brief Destructor for CUART.
     */
    ~CTUART();

    /**
     * @brief Initialize the serial port.
     * @param pInterruptSystem Pointer to the system interrupt controller.
     * @param recvFunc Deprecated/Unused callback pointer. Pass nullptr.
     * @return true if initialization succeeded, false otherwise.
     */
    typedef void (*ReceiveHandler)(const char*, size_t);
    bool Initialize(CInterruptSystem *pInterruptSystem, ReceiveHandler recvFunc);

    /**
     * @brief Ensure the UART task is running; safe to call multiple times.
     */
    bool EnsureStarted();

    /**
     * @brief Suspend the UART task so host input is buffered only.
     */
    void SuspendTask();

    /**
     * @brief Send a message through the serial interface.
     * @param buf Pointer to data buffer.
     * @param len Number of bytes to send.
     */
    void Send(const char* buf, size_t len);


    /**
     * @brief Main task loop for UART (currently idle, extend for polling/interrupt).
     */
    void Run() override;

    /**
     * @brief Drain available serial input from the ring buffer into a destination buffer.
     * @param dest Destination buffer.
     * @param maxLen Maximum number of bytes to drain.
     * @return Number of bytes drained, or negative error code.
     */
    int DrainSerialInput(char *dest, size_t maxLen);

private:
    class CSerialDeviceWithAccess : public CSerialDevice
    {
    public:
        using CSerialDevice::CSerialDevice;
        unsigned RxAvailable() { return AvailableForRead(); }
    };

    CSerialDeviceWithAccess *m_pSerial = nullptr;
    CInterruptSystem *m_pInterruptSystem = nullptr;
    bool m_bTaskRunning = false;
    bool m_bEverStarted = false;
    bool m_bSoftwareFlowControl = false;
    bool m_bFlowStopped = false;
    unsigned m_FlowHighThreshold = 0;
    unsigned m_FlowLowThreshold = 0;

    // Static receive handler pointer
    static ReceiveHandler g_ReceiveHandler;
};
