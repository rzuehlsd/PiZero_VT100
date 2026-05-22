//------------------------------------------------------------------------------
// Module:        CKernel
// Description:   Coordinates VT100 subsystem initialization and main control loop.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-18
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-18     R. Zuehlsdorff        Initial creation
// 2026-02-10     R. Zuehlsdorff        Added periodic VTTest tick and key routing
//------------------------------------------------------------------------------

// Include class header
#include "../include/kernel.h"

// Full class definitions for classes used in this module
// Include Circle core components
#include <circle/bcmframebuffer.h>
#include <circle/sched/task.h>
#include <circle/spinlock.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/net/mdnsdaemon.h>
#include <circle/net/in.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Include application components
#include "TRenderer.h"
#include "TRendererProjector.h"
#include "TRendererSurface.h"
#include "TFontConverter.h"
#include "TKeyboard.h"
#include "TConfig.h"
#include "TShadowBuffer.h"
#include "TUART.h"
#include "TFileLog.h"
#include "TWlanLog.h"
#include "TWlanHost.h"
#include "TSetup.h"
#include "VTTest.h"

LOGMODULE("CKernel");

namespace
{
    static const char DriveRoot[] = "SD:";
    static const char FirmwarePath[] = "SD:/firmware/";
    static const char SupplicantConfig[] = "SD:/wpa_supplicant.conf";
    static const char DefaultHostname[] = "PiVT100";
    static const unsigned TerminalPort = 2323;
    static const char StartupBannerPrefix[] = "VT100 Terminal Emulation with Circle on Pi zero V0.9";
    static const unsigned StartupBannerDelayMs = 2000;

    class CSetupScreenGateDevice : public CDevice
    {
    public:
        CSetupScreenGateDevice(CKernel *pKernel, CDevice *pTarget)
            : m_pKernel(pKernel), m_pTarget(pTarget)
        {
        }

        int Write(const void *pBuffer, size_t nCount) override
        {
            if (m_pKernel != nullptr && m_pKernel->IsScreenOutputBlocked())
            {
                return static_cast<int>(nCount);
            }

            if (m_pTarget == nullptr)
            {
                return static_cast<int>(nCount);
            }

            return m_pTarget->Write(pBuffer, nCount);
        }

    private:
        CKernel *m_pKernel;
        CDevice *m_pTarget;
    };

    static const char *GetWlanModeName(unsigned int mode)
    {
        switch (mode)
        {
        case 1U:
            return "WLAN remote log";
        case 2U:
            return "WLAN shell client";
        case 0U:
        default:
            return "WLAN disabled";
        }
    }

}

static volatile unsigned s_f12PressCount = 0;
static volatile unsigned s_f11PressCount = 0;
static volatile unsigned s_f10PressCount = 0;

// Singleton instance creation and access
// teardown handled by runtime
// CAUTION: Only possible if constructor does not need parameters
static CKernel *s_pThis = 0;
CKernel *CKernel::Get(void)
{
    if (s_pThis == 0)
    {
        s_pThis = new CKernel();
    }
    return s_pThis;
}

// internal Task Class to handle periodic test actions
#define PERIODIC_TASK_INTERVAL_MS 50

class CPeriodicTask : public CTask
{
public:
    CPeriodicTask() : CTask()
    {
        SetName("HeartBeat");
        Suspend();
    }

    void Run(void) override
    {
        while (true)
        {
            CKernel *kernel = CKernel::Get();
            if (kernel == nullptr)
            {
                CScheduler::Get()->MsSleep(100);
                continue;
            }

            if (s_f12PressCount != 0)
            {
                --s_f12PressCount;
                kernel->ToggleSetupDialog();
            }

            if (s_f11PressCount != 0)
            {
                --s_f11PressCount;
                kernel->ShowModernSetupDialog();
            }

            if (s_f10PressCount != 0)
            {
                --s_f10PressCount;
                kernel->ToggleLocalMode();
            }

            kernel->RunVTTestTick();

            CScheduler::Get()->MsSleep(PERIODIC_TASK_INTERVAL_MS);
        }
    }
};

static void onKeyPressed(const char *pString)
{
    if (pString == nullptr)
    {
        return;
    }

    CTSetup *setup = CTSetup::Get();
    if (setup != nullptr && setup->IsVisible())
    {
        setup->HandleVisibleKeyPressed(pString);
        return;
    }

    CKernel *kernel = CKernel::Get();
    if (kernel != nullptr)
    {
        if (kernel->HandleVTTestKey(pString))
        {
            return;
        }

        if (kernel->HandleShellClientKey(pString))
        {
            return;
        }

        if (kernel->IsLocalModeEnabled())
        {
            CTRenderer *renderer = CTRenderer::Get();
            if (renderer != nullptr && !kernel->IsScreenOutputBlocked())
            {
                renderer->Write(pString, strlen(pString));
            }
            return;
        }

        kernel->SendHostOutput(pString, strlen(pString));
        return;
    }

    CTUART::Get()->Send(pString, strlen(pString));
}

static void onKeyPressedRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    CTSetup *setup = CTSetup::Get();
    if (setup != nullptr && setup->IsVisible())
    {
        setup->HandleVisibleRawKeyStatus(ucModifiers, RawKeys);
        return;
    }

    static bool s_f12Down = false;
    static bool s_f11Down = false;
    static bool s_f10Down = false;
    bool f12Down = false;
    bool f11Down = false;
    bool f10Down = false;

    for (unsigned i = 0; i < 6; ++i)
    {
        if (RawKeys[i] == 0x44)
        {
            f11Down = true;
        }
        if (RawKeys[i] == 0x43)
        {
            f10Down = true;
        }
        if (RawKeys[i] == 0x45)
        {
            f12Down = true;
        }
    }

    if (f11Down && !s_f11Down)
    {
        ++s_f11PressCount;
    }

    if (f12Down && !s_f12Down)
    {
        ++s_f12PressCount;
    }

    if (f10Down && !s_f10Down)
    {
        ++s_f10PressCount;
    }

    s_f11Down = f11Down;
    s_f12Down = f12Down;
    s_f10Down = f10Down;
}

static CPeriodicTask *s_pPeriodicTask = nullptr;

CKernel::CKernel(void)
    : m_Options(),
      m_DeviceNameService(),
      m_Screen(m_Options.GetWidth(), m_Options.GetHeight()),
      m_Interrupt(),
      m_Logger(m_Options.GetLogLevel()),
      m_Timer(&m_Interrupt),
      m_Scheduler(),
      m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
      m_ExceptionHandler(),
      m_EMMC(&m_Interrupt, &m_Timer),
      m_FileSystem(),
      m_HAL(&m_Interrupt, &m_Timer),
      m_WLAN(FirmwarePath),
      m_Net(nullptr, nullptr, nullptr, nullptr, DefaultHostname, NetDeviceTypeWLAN),
      m_WpaSupplicant(SupplicantConfig),
      m_pRenderer(nullptr),
      m_pShadowBuffer(nullptr),
      m_pRendererSurface(nullptr),
      m_pRendererProjector(nullptr),
      m_pFontConverter(nullptr),
      m_pKeyboard(nullptr),
      m_pConfig(nullptr),
      m_pUART(nullptr),
      m_pFileLog(nullptr),
      m_pWlanLog(nullptr),
      m_pWlanHost(nullptr),
      m_pSetup(nullptr),
      m_pVTTest(nullptr),
      m_pLogTarget(nullptr),
      m_pScreenLogGate(nullptr),
      m_pNullLog(nullptr),
      m_bWlanLoggerEnabled(FALSE),
      m_bMDNSAdvertised(FALSE),
      m_bSerialTaskStarted(false),
      m_bTelnetReady(false),
      m_bWaitingMessageActive(false),
      m_bWaitingMessageShowsIP(false),
      m_bScreenLoggerEnabled(true),
      m_bLocalModeEnabled(false)
{
    s_pThis = this;

    // Create singleton instances of application components
    m_pFontConverter = CTFontConverter::Get();
    m_pRenderer = CTRenderer::Get();
    m_pKeyboard = CTKeyboard::Get();
    m_pConfig = CTConfig::Get();
    m_pUART = CTUART::Get();
    m_pFileLog = CTFileLog::Get();
    m_pWlanLog = CTWlanLog::Get();
    m_pWlanHost = CTWlanHost::Get();
    m_pSetup = CTSetup::Get();
    m_pVTTest = new CVTTest();
    s_pPeriodicTask = new CPeriodicTask();
}

bool CKernel::IsLocalModeEnabled() const
{
    return m_bLocalModeEnabled;
}
bool CKernel::HandleShellClientKey(const char *pString)
{
    if (m_pWlanHost == nullptr)
    {
        return false;
    }

    if (!m_pWlanHost->IsEnabled())
    {
        return false;
    }

    return m_pWlanHost->HandleKey(pString);
}

void CKernel::ToggleLocalMode()
{
    m_bLocalModeEnabled = !m_bLocalModeEnabled;

    if (m_pRenderer != nullptr)
    {
        static const char LocalOnMsg[] = "\r\nVT100 local mode ON\r\n";
        static const char LocalOffMsg[] = "\r\nVT100 local mode OFF\r\n";
        const char *msg = m_bLocalModeEnabled ? LocalOnMsg : LocalOffMsg;
        const size_t len = m_bLocalModeEnabled ? (sizeof LocalOnMsg - 1) : (sizeof LocalOffMsg - 1);
        WriteScreenMessage(msg, len);
    }

    LOGNOTE("Local mode %s", m_bLocalModeEnabled ? "enabled" : "disabled");
}

CKernel::~CKernel(void)
{
    delete m_pRendererProjector;
    m_pRendererProjector = nullptr;

    delete m_pRendererSurface;
    m_pRendererSurface = nullptr;

    delete m_pShadowBuffer;
    m_pShadowBuffer = nullptr;

    delete m_pScreenLogGate;
}

bool CKernel::IsScreenOutputBlocked() const
{
    return m_pSetup != nullptr && m_pSetup->IsVisible();
}

void CKernel::WriteScreenMessage(const char *pData, size_t nLength)
{
    if (pData == nullptr || nLength == 0 || m_pRenderer == nullptr || IsScreenOutputBlocked())
    {
        return;
    }

    m_pRenderer->Write(pData, nLength);
}

void CKernel::EnsureSerialTaskStarted()
{
    if (m_bSerialTaskStarted)
    {
        return;
    }

    if (m_pUART != nullptr && m_pUART->EnsureStarted())
    {
        m_bSerialTaskStarted = true;
    }
}

void CKernel::ToggleSetupDialog()
{
    if (m_pSetup != nullptr)
    {
        m_pSetup->Toggle();
    }
}

void CKernel::ShowModernSetupDialog()
{
    if (m_pSetup != nullptr)
    {
        m_pSetup->ShowModern();
    }
}

void CKernel::RunVTTestTick()
{
    if (m_pVTTest != nullptr)
    {
        m_pVTTest->Tick();
    }
}

bool CKernel::HandleVTTestKey(const char *pString)
{
    if (m_pVTTest != nullptr && m_pVTTest->IsActive())
    {
        return m_pVTTest->OnKeyPress(pString);
    }
    return false;
}

boolean CKernel::initFilesystem(void)
{
    FRESULT mountRes;
    boolean bOK = false;

    bOK = m_EMMC.Initialize();

    if (bOK)
    {
        // Add delay to ensure EMMC is ready
        m_Timer.MsDelay(100);

        // Mount filesystem globally for all file operations
        mountRes = f_mount(&m_FileSystem, DriveRoot, 1);

        if (mountRes != FR_OK)
        {
            LOGERR("Filesystem mount failed with error: %d", (int)mountRes);
            bOK = false;
        }
        else
        {
            LOGNOTE("Filesystem mounted successfully");
        }
    }
    else
    {
        LOGERR("EMMC initialization failed");
    }
    return bOK;
}

boolean CKernel::Initialize(void)
{
    boolean bOK = TRUE;

    auto configureLogOutputs = [&](bool logToScreen, bool logToFile, bool wlanEnabled)
    {
        m_bWlanLoggerEnabled = wlanEnabled ? TRUE : FALSE;
        m_bScreenLoggerEnabled = logToScreen;

        if (!logToScreen)
        {
            if (m_pNullLog == nullptr)
            {
                m_pNullLog = new CNullDevice();
            }
            m_pLogTarget = m_pNullLog;
            m_Logger.SetNewTarget(m_pLogTarget);
        }

        if (logToFile && m_pFileLog != nullptr)
        {
            CDevice *fallbackTarget = logToScreen ? m_pLogTarget : nullptr;
            if (!m_pFileLog->Initialize(m_Logger, m_pConfig->GetLogFileName(), fallbackTarget))
            {
                LOGERR("File logging: failed to initialize %s", m_pConfig->GetLogFileName());
            }
            else if (!m_pFileLog->Start())
            {
                LOGERR("File logging: failed to activate log target");
            }
            else
            {
                m_pLogTarget = m_pFileLog;
                LOGNOTE("File logging: active (%s)", m_pConfig->GetLogFileName());
            }
        }

        if (m_pWlanLog != nullptr)
        {
            m_pWlanLog->SetFallback(m_pLogTarget);
            if (!m_bWlanLoggerEnabled)
            {
                m_pWlanLog->Stop();
            }
        }
    };

    if (bOK)
    {
        bOK = m_Screen.Initialize();
        if (bOK)
            LOGNOTE("CKernel: Screen initialized");
    }

    if (bOK)
    {
        CDevice *pTarget = m_DeviceNameService.GetDevice(m_Options.GetLogDevice(), FALSE);
        if (pTarget == 0)
        {
            pTarget = &m_Screen;
        }

        if (pTarget == &m_Screen)
        {
            if (m_pScreenLogGate == nullptr)
            {
                m_pScreenLogGate = new CSetupScreenGateDevice(this, &m_Screen);
            }
            pTarget = m_pScreenLogGate;
        }

        m_pLogTarget = pTarget;

        bOK = m_Logger.Initialize(pTarget);
        if (bOK)
            LOGNOTE("Log Device initialized");
    }

    if (bOK)
    {
        bOK = m_Interrupt.Initialize();
        if (bOK)
            LOGNOTE("Interrupt initialized");
    }

    if (bOK)
    {
        bOK = m_Timer.Initialize();
        if (bOK)
            LOGNOTE("Timer initialized");
    }

    if (bOK)
    {
        bOK = m_USBHCI.Initialize();
        if (bOK)
            LOGNOTE("USB HCI initialized");
    }

    if (bOK)
    {
        bOK = m_HAL.Initialize();
        if (bOK)
            LOGNOTE("HAL initialized");
    }

    if (!initFilesystem())
    {
        LOGERR("Failed to initialize filesystem");
        bOK = FALSE;
    }

    if (m_pConfig == nullptr || !m_pConfig->Initialize())
    {
        LOGERR("Failed to initialize config module");
        bOK = FALSE;
    }
    else
    {
        if (!m_pConfig->LoadFromFile())
        {
            LOGWARN("Config: Using defaults because VT100.txt could not be read");
        }
        m_pConfig->logConfig();

        m_HAL.ConfigureBuzzerVolume(m_pConfig->GetBuzzerVolume());
        m_HAL.ConfigureRxTxSwap(m_pConfig->GetSwitchTxRx() != 0);

        bool logToScreen = true;
        bool logToFile = false;
        bool logToWlan = false;
        m_pConfig->ResolveLogOutputs(logToScreen, logToFile, logToWlan);
        const unsigned int wlanModePolicy = m_pConfig->GetWlanHostAutoStart();
        const bool wlanEnabled = (wlanModePolicy == 1U);
        configureLogOutputs(logToScreen, logToFile, wlanEnabled);

        m_bTelnetReady = false;
        m_bWaitingMessageActive = false;
        m_bWaitingMessageShowsIP = false;
    }

    if (m_pFontConverter == nullptr || !m_pFontConverter->Initialize())
    {
        LOGERR("Failed to initialize font converter module");
        bOK = FALSE;
    }

    if (m_pShadowBuffer == nullptr)
    {
        m_pShadowBuffer = new CShadowBuffer();
    }

    if (m_pRendererSurface == nullptr)
    {
        m_pRendererSurface = new CRendererSurface();
    }

    if (m_pRendererProjector == nullptr && m_pShadowBuffer != nullptr && m_pRendererSurface != nullptr)
    {
        m_pRendererProjector = new CTRendererProjector(*m_pShadowBuffer, *m_pRendererSurface);
    }

    if (m_pRenderer == nullptr || m_pShadowBuffer == nullptr || m_pRendererSurface == nullptr || m_pRendererProjector == nullptr)
    {
        LOGERR("Failed to create renderer render-stack modules");
        bOK = FALSE;
    }
    else
    {
        m_pRenderer->AttachRenderStack(m_pShadowBuffer, m_pRendererSurface, m_pRendererProjector);
        m_pRendererProjector->AttachRenderer(m_pRenderer);
    }

    if (m_pRenderer == nullptr || !m_pRenderer->Initialize())
    {
        LOGERR("Failed to initialize renderer module");
        bOK = FALSE;
    }

    if (bOK && m_pRendererProjector != nullptr && !m_pRendererProjector->Initialize())
    {
        LOGERR("Failed to initialize renderer projector task");
        bOK = FALSE;
    }

    if (m_pWlanHost != nullptr)
    {
        m_pWlanHost->Initialize(m_WLAN, m_Net, m_WpaSupplicant, m_Logger, m_pRenderer, &m_HAL);
        if (m_pConfig != nullptr)
        {
            const bool shellClientModeSelected = (m_pConfig->GetWlanHostAutoStart() == 2U);
            m_pWlanHost->SetEnabled(shellClientModeSelected);
        }
    }
    else if (m_pConfig != nullptr)
    {
        m_pRenderer->SetColors(m_pConfig->GetTextColor(), m_pConfig->GetBackgroundColor());
        m_pRenderer->SetVT52Mode(m_pConfig->GetVT52ModeEnabled() ? TRUE : FALSE);
        m_pRenderer->SetSmoothScrollEnabled(m_pConfig->GetSmoothScrollEnabled() ? TRUE : FALSE);
        m_pRenderer->SetSmoothScrollLineMs(m_pConfig->GetSmoothScrollLineMs());
        m_pRenderer->ClearDisplay();
    }

    if (m_pVTTest != nullptr)
    {
        m_pVTTest->Initialize(m_pRenderer);
    }

    if (m_pKeyboard != nullptr)
    {
        unsigned repeatDelayMs = 500U;
        unsigned repeatRateCps = 20U;
        if (m_pConfig != nullptr)
        {
            repeatDelayMs = m_pConfig->GetKeyRepeatDelayMs();
            repeatRateCps = m_pConfig->GetKeyRepeatRateCps();
        }

        m_pKeyboard->Configure(&onKeyPressed, &onKeyPressedRaw, &m_USBHCI, repeatDelayMs, repeatRateCps);
        if (!m_pKeyboard->Initialize())
        {
            LOGERR("Failed to initialize keyboard module");
            bOK = FALSE;
        }
    }
    else
    {
        LOGERR("Keyboard module not available");
        bOK = FALSE;
    }

    if (m_pUART == nullptr || !m_pUART->Initialize(&m_Interrupt, nullptr))
    {
        LOGERR("Failed to initialize UART module");
        bOK = FALSE;
    }

    if (m_bWlanLoggerEnabled)
    {
        if (m_pWlanLog == nullptr || !m_pWlanLog->Initialize(m_WLAN, m_Net, m_WpaSupplicant, m_Logger, TerminalPort, m_pLogTarget))
        {
            LOGERR("WLAN logging: initialization failed");
            m_bWlanLoggerEnabled = FALSE;
        }
    }

    if (bOK)
    {
        s_pPeriodicTask->Start();
        LOGNOTE("CKernel initialized successfully");

        CString banner;
        banner.Format("\r\n%s (%s %s)\r\n", StartupBannerPrefix, __DATE__, __TIME__);

        if (m_pRenderer != nullptr)
        {
            WriteScreenMessage(banner.c_str(), banner.GetLength());
        }

        LOGNOTE("Startup: %s", (const char *)banner);
        m_Timer.MsDelay(StartupBannerDelayMs);

        if (m_pConfig != nullptr && m_pRenderer != nullptr)
        {
            CString wlanStatus;
            const unsigned int wlanModePolicy = m_pConfig->GetWlanHostAutoStart();
            wlanStatus.Format("\r\nConfig loaded: %s mode active\r\n", GetWlanModeName(wlanModePolicy));
            WriteScreenMessage(wlanStatus.c_str(), wlanStatus.GetLength());
            LOGNOTE("Config loaded: %s mode active", GetWlanModeName(wlanModePolicy));
        }
    }

    if (!m_bWlanLoggerEnabled)
    {
        MarkTelnetReady();
    }

    if (m_pSetup == nullptr || !m_pSetup->Initialize(m_pRenderer, m_pConfig, m_pKeyboard))
    {
        LOGERR("Failed to initialize setup dialog");
        bOK = FALSE;
    }

    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    LOGNOTE("Compile time: " __DATE__ " " __TIME__);

    if (m_bWlanLoggerEnabled && m_pWlanLog != nullptr)
    {
        m_pWlanLog->SetFallback(m_pLogTarget);
        if (!m_pWlanLog->Start())
        {
            LOGERR("WLAN logging: failed to activate telnet console task");
            m_bWlanLoggerEnabled = FALSE;
            MarkTelnetReady();
        }
    }

    m_HAL.BEEP();

    if (m_bWlanLoggerEnabled)
    {
        MarkTelnetWaiting();
    }

    while (1)
    {
        ProcessSerial();

        if (m_pWlanHost != nullptr && m_pWlanHost->IsEnabled())
        {
            m_pWlanHost->Tick();
        }

        if (m_bWlanLoggerEnabled)
        {
            m_Net.Process();

            if (m_pWlanLog != nullptr && !m_pWlanLog->IsClientConnected() && !IsTelnetReady())
            {
                MarkTelnetWaiting();
            }

            if (!m_bMDNSAdvertised && m_Net.IsRunning())
            {
                CmDNSDaemon *mdns = CmDNSDaemon::Get();
                if (mdns != nullptr && mdns->IsRunning())
                {
                    CString hostname = mdns->GetHostname();
                    CString fullName = hostname;
                    fullName.Append(".local");

                    LOGNOTE("WLAN logging: advertised via mDNS as %s", (const char *)fullName);
                    LOGNOTE("WLAN logging: connect via 'telnet %s %u'", (const char *)fullName, TerminalPort);

                    m_bMDNSAdvertised = TRUE;
                }
            }
        }

        CScheduler::Get()->Yield();
        m_HAL.Update();
    }

    return ShutdownHalt;
}

bool CKernel::IsTelnetReady() const
{
    return m_bTelnetReady;
}

void CKernel::MarkTelnetReady()
{
    if (m_bTelnetReady)
    {
        return;
    }

    m_bTelnetReady = true;
    m_bWaitingMessageActive = false;
    m_bWaitingMessageShowsIP = false;

    if (m_bWlanLoggerEnabled && m_pRenderer != nullptr)
    {
        static const char ReadyMsg[] = "\r\nTelnet client connected - enabling local output\r\n";
        WriteScreenMessage(ReadyMsg, sizeof ReadyMsg - 1);
    }

    EnsureSerialTaskStarted();
}

void CKernel::ApplyRuntimeConfig()
{
    if (m_pConfig == nullptr)
    {
        return;
    }

    if (m_pWlanHost != nullptr)
    {
        const bool shellClientModeSelected = (m_pConfig->GetWlanHostAutoStart() == 2U);
        m_pWlanHost->SetEnabled(shellClientModeSelected);
    }

    if (m_pRenderer != nullptr)
    {
        m_pRenderer->SetColors(m_pConfig->GetTextColor(), m_pConfig->GetBackgroundColor());
        m_pRenderer->SetFont(m_pConfig->GetFontSelection(), CCharGenerator::FontFlagsNone);
        m_pRenderer->SetCursorBlock(m_pConfig->GetCursorBlock());
        m_pRenderer->SetBlinkingCursor(m_pConfig->GetCursorBlinking(), 500);
        m_pRenderer->SetVT52Mode(m_pConfig->GetVT52ModeEnabled() ? TRUE : FALSE);
        m_pRenderer->SetSmoothScrollEnabled(m_pConfig->GetSmoothScrollEnabled() ? TRUE : FALSE);
        m_pRenderer->SetSmoothScrollLineMs(m_pConfig->GetSmoothScrollLineMs());
        m_pRenderer->SetWrapAroundMode(m_pConfig->GetWrapAroundEnabled() ? TRUE : FALSE);
    }

    m_HAL.ConfigureBuzzerVolume(m_pConfig->GetBuzzerVolume());
    m_HAL.ConfigureRxTxSwap(m_pConfig->GetSwitchTxRx() != 0);
}

void CKernel::SendHostOutput(const char *pData, size_t nLength)
{
    if (pData == nullptr || nLength == 0)
    {
        return;
    }

    const bool shellClientModeSelected = (m_pConfig != nullptr && m_pConfig->GetWlanHostAutoStart() == 2U);

    if (shellClientModeSelected)
    {
        if (m_pWlanHost != nullptr)
        {
            (void)m_pWlanHost->SendHostData(pData, nLength);
        }
        return;
    }

    if (m_pWlanLog != nullptr && m_pWlanLog->IsHostModeActive())
    {
        if (m_pWlanLog->SendHostData(pData, nLength))
        {
            return;
        }
    }

    if (m_pUART != nullptr)
    {
        m_pUART->Send(pData, nLength);
    }
}

void CKernel::HandleWlanHostRx(const char *pData, size_t nLength)
{
    if (pData == nullptr || nLength == 0)
    {
        return;
    }

    if (m_pSetup != nullptr && m_pSetup->IsVisible())
    {
        return;
    }

    if (m_pRenderer != nullptr)
    {
        WriteScreenMessage(pData, nLength);
    }
}

void CKernel::ProcessSerial()
{
    if (m_pUART == nullptr)
    {
        return;
    }

    if (m_pWlanLog != nullptr && m_pWlanLog->IsHostModeActive())
    {
        return;
    }

    // Drain buffer in reasonable chunks to keep renderer responsive
    // but without stalling the main loop for too long.
    char buffer[2048];
    int nBytes = m_pUART->DrainSerialInput(buffer, sizeof(buffer));

    if (nBytes > 0)
    {
        if (m_pSetup != nullptr && m_pSetup->IsVisible())
        {
            return;
        }

        if (m_pRenderer != nullptr)
        {
            WriteScreenMessage(buffer, (size_t)nBytes);
        }
    }
    else if (nBytes < 0)
    {
        // Only log specific errors if needed, to avoid flooding
        if (nBytes == -SERIAL_ERROR_OVERRUN)
        {
            LOGWARN("UART input buffer overrun - data lost");
        }
    }
}

void CKernel::MarkTelnetWaiting()
{
    if (!m_bWlanLoggerEnabled)
    {
        m_bTelnetReady = true;
        m_bWaitingMessageActive = false;
        m_bWaitingMessageShowsIP = false;
        return;
    }

    bool haveIPNow = false;
    const CNetConfig *waitingConfig = m_Net.GetConfig();
    if (waitingConfig != nullptr && waitingConfig->GetIPAddress() != nullptr)
    {
        const CIPAddress *ip = waitingConfig->GetIPAddress();
        haveIPNow = ip->IsSet() && !ip->IsNull();
    }

    m_bTelnetReady = false;
    m_bWaitingMessageActive = true;

    if (m_pUART != nullptr)
    {
        m_pUART->SuspendTask();
    }
    m_bSerialTaskStarted = false;

    if (!haveIPNow)
    {
        return;
    }

    if (m_bWaitingMessageShowsIP)
    {
        return;
    }

    if (m_pRenderer != nullptr)
    {
        const bool shellClientMode = (m_pConfig != nullptr && m_pConfig->GetWlanHostAutoStart() == 2U);
        CString waitingMsg;
        if (shellClientMode)
        {
            waitingMsg = "\r\nWaiting for shell-client TCP connection...\r\n";
        }
        else
        {
            waitingMsg = "\r\nWaiting for telnet client connection...\r\n";
        }

        CString ipString;
        bool haveIP = false;
        const CNetConfig *config = m_Net.GetConfig();
        if (config != nullptr && config->GetIPAddress() != nullptr)
        {
            const CIPAddress *ip = config->GetIPAddress();
            haveIP = ip->IsSet() && !ip->IsNull();
            if (haveIP)
            {
                ip->Format(&ipString);
                haveIP = ipString.GetLength() > 0;
            }
        }

        CString connectHint;

        CString hostName = m_Net.GetHostname();
        if (shellClientMode)
        {
            if (hostName.GetLength() > 0)
            {
                connectHint.Format("Connect shell client via TCP: %s %u or %s.local %u\r\n",
                                   (const char *)ipString,
                                   TerminalPort,
                                   (const char *)hostName,
                                   TerminalPort);
            }
            else
            {
                connectHint.Format("Connect shell client via TCP: %s %u\r\n", (const char *)ipString, TerminalPort);
            }
        }
        else if (hostName.GetLength() > 0)
        {
            connectHint.Format("Connect via: telnet %s %u or telnet %s.local %u\r\n",
                               (const char *)ipString,
                               TerminalPort,
                               (const char *)hostName,
                               TerminalPort);
        }
        else
        {
            connectHint.Format("Connect via: telnet %s %u\r\n", (const char *)ipString, TerminalPort);
        }

        waitingMsg += connectHint;

        WriteScreenMessage(waitingMsg.c_str(), waitingMsg.GetLength());
        m_bWaitingMessageShowsIP = haveIP;
    }
}
