//------------------------------------------------------------------------------
// Module:        CHAL
// Description:   Hardware abstraction for buzzer control and serial pin routing.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-18
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-18     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#include "hal.h"

LOGMODULE("CHAL");

// Singleton instance creation and access

static CHAL *s_pThis = 0;
CHAL *CHAL::Get(void)
{
    return s_pThis;
}



CHAL::CHAL(CInterruptSystem *pInterruptSystem, CTimer *pTimer)
:   m_pInterruptSystem(pInterruptSystem),
    m_pTimer(pTimer),
    m_UserTimer(pInterruptSystem, UserTimerHandler, this),
    m_Pin(),
    m_bTimerInitialized(FALSE),
    m_bActive(FALSE),
    m_bUseTimer(FALSE),
    m_bHighPhase(FALSE),
    m_nConfiguredBuzzerVolume(100),
    m_nStopTicks(0),
    m_nOnMicros(0),
    m_nOffMicros(0)
{
    m_bGPIO16Configured = FALSE;
    m_bBuzzerPinConfigured = FALSE;
    m_bRxTxSwitchMode = FALSE;
    s_pThis = this;
}

CHAL::~CHAL(void)
{
    StopInternal(FALSE);

    if (m_bTimerInitialized)
    {
        m_UserTimer.Stop();
        m_bTimerInitialized = FALSE;
    }
}

boolean CHAL::Initialize(void)
{
    if (m_bTimerInitialized)
    {
        LOGNOTE("HAL already initialized");
        return TRUE;
    }

    if (m_pInterruptSystem == 0 || m_pTimer == 0)
    {
        LOGERR("HAL init failed: dependencies missing (InterruptSystem=%p Timer=%p)",
               m_pInterruptSystem, m_pTimer);
        return FALSE;
    }

    m_RxTXSwitchPin.AssignPin(RxTXSwitchPin);
    m_RxTXSwitchPin.SetMode(GPIOModeOutput);
    m_RxTXSwitchPin.Write(LOW);
    m_bGPIO16Configured = TRUE;
    LOGNOTE("GPIO%u configured as output RxTX Switch", RxTXSwitchPin);

    m_Pin.AssignPin(PWMGPIOPin);
    m_Pin.SetMode(GPIOModeOutput);
    m_Pin.Write(LOW);
    m_bBuzzerPinConfigured = TRUE;
    LOGNOTE("GPIO%u configured as output for PWM", PWMGPIOPin);

    if (!m_UserTimer.Initialize())
    {
        LOGERR( "Failed to initialize user timer");
        return FALSE;
    }

    m_bTimerInitialized = TRUE;
    LOGNOTE("Software PWM initialized (%u Hz)", PWMFrequencyHz);

    return TRUE;
}

void CHAL::BEEP(void)
{
    StartBuzzer(m_nConfiguredBuzzerVolume, 250);
}

void CHAL::Click(void)
{
    StartBuzzer(m_nConfiguredBuzzerVolume, 25);
}

void CHAL::StartBuzzer (unsigned duty, unsigned duration)
{
    if (!m_bTimerInitialized)
    {
        LOGERR( "Start requested before PWM initialized");
        return;
    }

    if (duty > 100)
    {
        duty = 100;
    }

    if (duty == 0)
    {
        StopInternal(FALSE);
        return;
    }

    m_nOnMicros = (PWMPeriodMicros * duty) / 100;
    if (m_nOnMicros == 0)
    {
        m_nOnMicros = 1;  // guarantee progress for very small values
    }
    m_nOffMicros = PWMPeriodMicros - m_nOnMicros;

    m_bActive = TRUE;
    m_bHighPhase = TRUE;
    m_bUseTimer = (m_nOnMicros > 0 && m_nOffMicros > 0);

    m_Pin.Write(HIGH);

    if (m_bUseTimer)
    {
        m_UserTimer.Start(m_nOnMicros);
    }

    if (duration > 0)
    {
        unsigned ticks = MSEC2HZ(duration);
        if (ticks == 0)
        {
            ticks = 1;
        }
        m_nStopTicks = m_pTimer->GetTicks() + ticks;
    }
    else
    {
        m_nStopTicks = 0;
    }
}

void CHAL::StopBuzzer(void)
{
    StopInternal(TRUE);
}

void CHAL::StopInternal(boolean logMessage)
{
    boolean wasActive = m_bActive || m_bUseTimer;

    m_bActive = FALSE;
    m_bUseTimer = FALSE;
    m_bHighPhase = FALSE;
    m_nStopTicks = 0;
    m_nOnMicros = 0;
    m_nOffMicros = 0;

    if (m_bTimerInitialized)
    {
        m_Pin.Write(LOW);
    }

    if (wasActive && logMessage)
    {
        // keep quiet in steady-state operation
    }
}

void CHAL::Update(void)
{
    if (m_pTimer == 0)
    {
        LOGERR("HAL update skipped: timer not available");
        return;
    }

    if (m_bActive && m_nStopTicks > 0)
    {
        unsigned now = m_pTimer->GetTicks();
        if ((int)(now - m_nStopTicks) >= 0)
        {
            StopInternal(FALSE);
        }
    }
}

void CHAL::UserTimerHandler(CUserTimer *pTimer, void *pParam)
{
    (void) pTimer;

    CHAL *pThis = static_cast<CHAL *>(pParam);
    if (pThis != 0)
    {
        pThis->HandleTimerTick();
    }
}

void CHAL::HandleTimerTick()
{
    if (!m_bActive)
    {
        if (m_bTimerInitialized)
        {
            m_Pin.Write(LOW);
        }
        return;
    }

    if (!m_bUseTimer)
    {
        return;
    }

    if (m_bHighPhase)
    {
        m_Pin.Write(LOW);
        m_bHighPhase = FALSE;
        m_UserTimer.Start(m_nOffMicros);
    }
    else
    {
        m_Pin.Write(HIGH);
        m_bHighPhase = TRUE;
        m_UserTimer.Start(m_nOnMicros);
    }
}

void CHAL::SwitchRxTx(void)
{
    if (!m_bGPIO16Configured)
    {
        LOGWARN("GPIO%u not configured for RxTx switch", RxTXSwitchPin);
        return;
    }

    ConfigureRxTxSwap(TRUE);
}

void CHAL::ConfigureRxTxSwap(boolean enableSwap)
{
    if (!m_bGPIO16Configured)
    {
        LOGWARN("GPIO%u not configured for RxTx switch", RxTXSwitchPin);
        return;
    }

    if (m_bRxTxSwitchMode == enableSwap)
    {
        LOGNOTE("RxTx wiring already %s", enableSwap ? "swapped" : "normal");
        return;
    }

    m_RxTXSwitchPin.Write(enableSwap ? HIGH : LOW);
    m_bRxTxSwitchMode = enableSwap;
    LOGNOTE("GPIO%u set %s for RxTx %s mode", RxTXSwitchPin,
            enableSwap ? "HIGH" : "LOW",
            enableSwap ? "swapped" : "normal");
}

void CHAL::ConfigureBuzzerVolume(unsigned volumePercent)
{
    if (volumePercent > 100)
    {
        volumePercent = 100;
    }

    if (m_nConfiguredBuzzerVolume == volumePercent)
    {
        LOGNOTE("Buzzer volume unchanged at %u%%", volumePercent);
        return;
    }

    m_nConfiguredBuzzerVolume = volumePercent;
    LOGNOTE("Buzzer volume set to %u%%", m_nConfiguredBuzzerVolume);
}
