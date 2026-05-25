//------------------------------------------------------------------------------
// Module:        CUART
// Description:   Provides buffered UART handling as a singleton service.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-27
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-27     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

#include <circle/spinlock.h>
#include <circle/serial.h>

/**
 * @file CUART.h
 * @brief Declares the UART service abstraction.
 * @details CUART hides Circle's low-level serial device behind a singleton
 * service that initializes the hardware and exposes direct polling helpers.
 * It primarily manages the serial device initialization and provides access for
 * higher layers to drain the hardware FIFO.
 */

/**
 * @class CUART
 * @brief Service responsible for UART initialization and data retrieval.
 * @details The singleton wraps the serial device and exposes polling helpers
 * so higher layers can drain received data directly from the hardware FIFO.
 */
class CUART
{
public:
    /// \brief Access the singleton UART service instance.
    static CUART *Get(void);

    /**
     * @brief Construct a CUART service object.
     */
    CUART();

    /**
     * @brief Destructor for CUART.
     */
    ~CUART();

    /**
     * @brief Initialize the serial port.
     * @param pInterruptSystem Pointer to the system interrupt controller.
     * @param recvFunc Deprecated/Unused callback pointer. Pass nullptr.
     * @return true if initialization succeeded, false otherwise.
     */
    typedef void (*ReceiveHandler)(const char *, size_t);
    bool Initialize(CInterruptSystem *pInterruptSystem, ReceiveHandler recvFunc);

    /**
     * @brief Send a message through the serial interface.
     * @param buf Pointer to data buffer.
     * @param len Number of bytes to send.
     */
    void Send(const char *buf, size_t len);

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
    bool m_bSoftwareFlowControl = false;
    bool m_bFlowStopped = false;
    unsigned m_FlowHighThreshold = 0;
    unsigned m_FlowLowThreshold = 0;

    // Static receive handler pointer
    static ReceiveHandler g_ReceiveHandler;
};
