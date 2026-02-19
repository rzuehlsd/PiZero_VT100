//------------------------------------------------------------------------------
// Module:        kernel.h
// Description:   Main entry point for Pi_VT100, a VT100 emulator running
//                on Pi zero bare metal implementation based on Circle
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
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/device.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/net/netsubsystem.h>
#include <circle/serial.h>
#include <circle/screen.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/nulldevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

// Forward declarations for class references used in this module
class CTWlanLog;
class CTRenderer;
class CTFontConverter;
class CTKeyboard;
class CTConfig;
class CTUART;
class CTFileLog;
class CTSetup;
class CVTTest;

#include "hal.h"

/**
 * @file kernel.h
 * @brief Declares the top-level kernel orchestrating VT100 subsystems.
 * @details CKernel ties together Circle services, initializes every subsystem
 * in the correct order, and owns the cooperative scheduler loop. The header
 * documents the object graph to ease navigation when troubleshooting boot or
 * shutdown flows.
 */

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

/**
 * @class CKernel
 * @brief Central coordinator for hardware bring-up and runtime control.
 * @details The kernel constructs core Circle devices, starts cooperative tasks
 * such as the renderer and keyboard, and mediates communication between
 * subsystems. It also exposes entry points for serial data, telnet readiness,
 * and orderly shutdown management.
 */
class CKernel
{
public:
    /// \brief Construct the kernel and wire up Circle subsystems.
    CKernel(void);
    /// \brief Release kernel-owned resources.
    ~CKernel(void);

    /// \brief Access the singleton kernel instance.
    static CKernel *Get(void);

    /// \brief Initialize all VT100 subsystems and dependencies.
    boolean Initialize(void);

    /// \brief Enter the cooperative scheduler and main control loop.
    TShutdownMode Run(void);

    /// \brief Check whether the telnet service finished setup.
    bool IsTelnetReady() const;
    /// \brief Mark telnet service as ready for clients.
    void MarkTelnetReady();
    /// \brief Mark telnet service as waiting for configuration.
    void MarkTelnetWaiting();
    /// \brief Apply current CTConfig values to runtime subsystems.
    void ApplyRuntimeConfig();

    /// \brief Toggle the on-screen setup dialog.
    void ToggleSetupDialog();
    /// \brief Show the modern on-screen setup dialog.
    void ShowModernSetupDialog();
    /// \brief Check whether keyboard input loopback mode is active.
    bool IsLocalModeEnabled() const;
    /// \brief Toggle local keyboard loopback mode.
    void ToggleLocalMode();

    /// \brief Forward a key press to the VT test runner (manual confirmation).
    bool HandleVTTestKey(const char *pString);

    /// \brief Run periodic VT test tick (if enabled).
    void RunVTTestTick();

    /// \brief Forward keyboard-generated host output to TCP host mode or UART fallback.
    void SendHostOutput(const char *pData, size_t nLength);
    /// \brief Consume bytes received from WLAN host bridge and render them.
    void HandleWlanHostRx(const char *pData, size_t nLength);

protected:
    /// \brief Mount the filesystem and prepare SD card access.
    boolean initFilesystem(void);

private:
    /// \brief Ensure the UART task is running prior to serial operations.
    void EnsureSerialTaskStarted();
    /// \brief Drain the buffered UART input and pass to renderer.
    void ProcessSerial();

    // do not change this order - some members depend on others
    CKernelOptions m_Options;
    CDeviceNameService m_DeviceNameService;
    CScreenDevice m_Screen;
    CInterruptSystem m_Interrupt;
    CLogger m_Logger;
    CTimer m_Timer;
    CScheduler m_Scheduler;
    CUSBHCIDevice m_USBHCI;
    CExceptionHandler m_ExceptionHandler;
    CEMMCDevice m_EMMC;
    FATFS m_FileSystem;
    CHAL m_HAL;
    CBcm4343Device m_WLAN;
    CNetSubSystem m_Net;
    CWPASupplicant m_WpaSupplicant;

    // Tasks are created inside constructor and
    // initialized in kernel::Initialize()
    CTRenderer *m_pRenderer;
    CTFontConverter *m_pFontConverter;
    CTKeyboard *m_pKeyboard;
    CTConfig *m_pConfig;
    CTUART *m_pUART;
    CTFileLog *m_pFileLog;
    CTWlanLog *m_pWlanLog;
    CTSetup *m_pSetup;
    CVTTest *m_pVTTest;
    CDevice *m_pLogTarget;
    CNullDevice *m_pNullLog;

    boolean m_bWlanLoggerEnabled;
    boolean m_bMDNSAdvertised;
    bool m_bSerialTaskStarted;
    bool m_bTelnetReady;
    bool m_bWaitingMessageActive;
    bool m_bWaitingMessageShowsIP;
    bool m_bScreenLoggerEnabled;
    bool m_bLocalModeEnabled;
};
