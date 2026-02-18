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

#include "TUART.h"
#include "TConfig.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <string.h>

LOGMODULE("CTUART");

// Singleton instance creation and access
// teardown handled by runtime
// CAUTION: Only possible if constructor does not need parameters
static CTUART *s_pThis = 0;
CTUART::ReceiveHandler CTUART::g_ReceiveHandler = nullptr;
CTUART *CTUART::Get(void)
{
    if (s_pThis == 0)
    {
        s_pThis = new CTUART();
    }
    return s_pThis;
}


CTUART::CTUART()
        : CTask(),
            m_pSerial(nullptr),
            m_pInterruptSystem(nullptr),
            m_bTaskRunning(false),
            m_bEverStarted(false),
            m_bSoftwareFlowControl(false),
            m_bFlowStopped(false),
            m_FlowHighThreshold(0),
            m_FlowLowThreshold(0)
{
    SetName("UART");
    Suspend();
    LOGERR("UART: CTUART constructed");
}

CTUART::~CTUART() {

};

bool CTUART::Initialize(CInterruptSystem *pInterruptSystem, ReceiveHandler recvFunc)
{
    m_pInterruptSystem = pInterruptSystem;
    if (m_pSerial) {
        delete m_pSerial;
        m_pSerial = nullptr;
    }
    m_pSerial = new CSerialDeviceWithAccess(m_pInterruptSystem);
    unsigned int baud = 115200;
    unsigned int dataBits = 8;
    CSerialDevice::TParity parity = CSerialDevice::ParityNone;

    m_bTaskRunning = false;
    m_bEverStarted = false;

    LOGNOTE("Initializing serial port...");

    g_ReceiveHandler = recvFunc;

    CTConfig *pConfig = CTConfig::Get();
    if (pConfig)
    {
        baud = pConfig->GetBaudRate();
        dataBits = pConfig->GetSerialDataBits();
        const unsigned int parityMode = pConfig->GetSerialParityMode();
        if (parityMode == 1U)
        {
            parity = CSerialDevice::ParityEven;
        }
        else if (parityMode == 2U)
        {
            parity = CSerialDevice::ParityOdd;
        }
        LOGNOTE("Configured baud rate: %u", baud);

        m_bSoftwareFlowControl = pConfig->GetSoftwareFlowControl();
        const unsigned bufSize = SERIAL_BUF_SIZE;
        m_FlowHighThreshold = (bufSize * 60U) / 100U;
        m_FlowLowThreshold = (bufSize * 30U) / 100U;
        m_bFlowStopped = false;
    }

    if (!m_pSerial->Initialize(baud, dataBits, 1U, parity))
    {
        LOGERR("Serial port initialization failed");
        return false;
    }
    // Use polling reads in the UART task (no ISR handler registration)
        LOGNOTE("Serial port initialized at %u baud (%u%c1)",
            baud,
            dataBits,
            parity == CSerialDevice::ParityEven ? 'E' : (parity == CSerialDevice::ParityOdd ? 'O' : 'N'));
    
    return true;
}

bool CTUART::EnsureStarted()
{
    if (m_bTaskRunning)
    {
        return true;
    }

    if (m_pSerial == nullptr)
    {
        LOGWARN("UART task start requested before initialization");
        return false;
    }

    bool wasEverStarted = m_bEverStarted;
    if (!wasEverStarted)
    {
        Start();
        m_bEverStarted = true;
    }
    else
    {
        Resume();
    }

    m_bTaskRunning = true;
    LOGNOTE("UART task %s", wasEverStarted ? "resumed" : "started");
    return true;
}

void CTUART::SuspendTask()
{
    if (!m_bTaskRunning)
    {
        return;
    }

    Suspend();
    m_bTaskRunning = false;
    LOGNOTE("UART task suspended");
}

void CTUART::Send(const char *buf, size_t len)
{
    if (m_pSerial)
        m_pSerial->Write(buf, len);
}

void CTUART::Run()
{
    // The UART task is now a placeholder as ProcessSerial in kernel calls DrainSerialInput directly.
    while (!IsSuspended())
    {
        CScheduler::Get()->Yield();
    }
}

int CTUART::DrainSerialInput(char *dest, size_t maxLen)
{
    if (dest == nullptr || maxLen == 0)
    {
        return 0;
    }

    if (m_pSerial != nullptr)
    {
        if (m_bSoftwareFlowControl)
        {
            const unsigned available = m_pSerial->RxAvailable();
            if (!m_bFlowStopped && available >= m_FlowHighThreshold)
            {
                const char xoff = 0x13; // XOFF
                m_pSerial->Write(&xoff, 1);
                m_bFlowStopped = true;
            }
            else if (m_bFlowStopped && available <= m_FlowLowThreshold)
            {
                const char xon = 0x11; // XON
                m_pSerial->Write(&xon, 1);
                m_bFlowStopped = false;
            }
        }

        return m_pSerial->Read(dest, maxLen);
    }

    return 0;
}
