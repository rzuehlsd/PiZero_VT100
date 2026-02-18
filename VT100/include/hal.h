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

#pragma once


// Include Circle core components
#include <circle/types.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usertimer.h>
#include <circle/gpiopin.h>

/**
 * @file hal.h
 * @brief Declares the hardware abstraction layer for the VT100 firmware.
 * @details CHAL offers a narrow interface around GPIO driven features such as
 * the buzzer and the optional RX/TX swap relay. It owns the lightweight timer
 * infrastructure used to generate tones and ensures consistent access to hardware
 * primitives regardless of where the calls originate.
 */

// Forward declarations and includes for classes used in this module


/**
 * @class CHAL
 * @brief Provides buzzer control, GPIO switching, and timing utilities.
 * @details The hardware abstraction maintains the PWM backing timer, supervises
 * auto-stop behavior, and exposes helper methods for the kernel to trigger
 * audible feedback. It also implements logic for dynamically swapping the UART
 * pins when RS-232 wiring requires it.
 */
class CHAL
{
public:
    /// \brief Construct the hardware abstraction layer with timer context.
    CHAL(CInterruptSystem *pInterruptSystem, CTimer *pTimer);
    /// \brief Ensure peripherals are quiesced on shutdown.
    ~CHAL(void);

    // \return Pointer to singleton CHAL instance
    /// \brief Access the singleton HAL instance.
    static CHAL *Get(void);

    // Initialize PWM hardware (call once during startup)
    /// \brief Prepare GPIO and timer resources required for HAL features.
    boolean Initialize(void);

    // 800 Hz PWM Buzzer with configured volume and 250ms duration
    /// \brief Play a fixed-duration beep using current volume setting.
    void BEEP(void);

    // 800 Hz Click sound for key press
    /// \brief Play a short click feedback tone.
    void Click(void);

    // Start Buzzer with specified duty cycle and duration
    // duty: 0-100% duty cycle
    // duration: milliseconds (0 = continuous)
    /// \brief Begin buzzer output with optional auto-stop duration.
    void StartBuzzer(unsigned duty, unsigned duration = 0);

    // Stop Buzzer output
    /// \brief Halt buzzer activity immediately.
    void StopBuzzer(void);

    // Switch RxD <-> TxD pins for RS-232 mode
    /// \brief Toggle the hardware RX/TX pin swap relay.
    void SwitchRxTx(void);

    // Apply configured RX/TX swap state
    /// \brief Set the RX/TX swap mode explicitly.
    void ConfigureRxTxSwap(boolean enableSwap);

    // Apply configured buzzer volume (0-100)
    /// \brief Store the buzzer volume percentage.
    void ConfigureBuzzerVolume(unsigned volumePercent);

    // Update function - call regularly from main loop to handle auto-stop
    /// \brief Update fast timers to manage auto-stop behavior.
    void Update(void);

private:
    /// \brief Callback invoked from the user timer to toggle PWM state.
    static void UserTimerHandler(CUserTimer *pTimer, void *pParam);
    /// \brief Handle per-tick updates of the buzzer PWM cycle.
    void HandleTimerTick();
    /// \brief Stop buzzer operations and optionally log the event.
    void StopInternal(boolean logMessage);

private:
    enum
    {
        PWMFrequencyHz  = 800,
        PWMPeriodMicros = 1000000U / PWMFrequencyHz,
        PWMGPIOPin      = 12,
        RxTXSwitchPin   = 16
    };

    CInterruptSystem  *m_pInterruptSystem;
    CTimer            *m_pTimer;
    CUserTimer         m_UserTimer;
    CGPIOPin           m_Pin;
    CGPIOPin           m_RxTXSwitchPin;

    boolean            m_bGPIO16Configured;
    boolean            m_bBuzzerPinConfigured;
    boolean            m_bTimerInitialized;
    boolean            m_bActive;
    boolean            m_bUseTimer;
    boolean            m_bHighPhase;
    boolean            m_bRxTxSwitchMode;
    unsigned           m_nConfiguredBuzzerVolume;
    unsigned           m_nStopTicks;
    unsigned           m_nOnMicros;
    unsigned           m_nOffMicros;
};
