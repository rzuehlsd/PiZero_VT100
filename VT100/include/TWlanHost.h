//------------------------------------------------------------------------------
// Module:        CTWlanHost
// Description:   Dedicated WLAN shell-client task (outbound RAW TCP bridge).
// Author:        R. Zuehlsdorff + Copilot
// Created:       2026-02-21
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------

#pragma once

#include <circle/net/ipaddress.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/sched/task.h>
#include <circle/spinlock.h>
#include <circle/string.h>
#include <circle/types.h>

class CBcm4343Device;
class CWPASupplicant;
class CLogger;
class CTRenderer;
class CHAL;

/**
 * @file TWlanHost.h
 * @brief Dedicated task for WLAN shell-client (wlan_host_autostart=2).
 * @details This task isolates shell-client mode from kernel runtime and keeps
 * the kernel loop responsive even when Circle socket connect operations block.
 */
class CTWlanHost : public CTask
{
public:
    static CTWlanHost *Get();

    CTWlanHost();
    ~CTWlanHost() override;

    bool Initialize(CBcm4343Device &wlan,
                    CNetSubSystem &net,
                    CWPASupplicant &supplicant,
                    CLogger &logger,
                    CTRenderer *renderer,
                    CHAL *hal);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    bool HandleKey(const char *pString);
    void Tick();

    bool SendHostData(const char *pData, size_t nLength);

    void Run() override;

private:
    enum TShellPromptField
    {
        ShellFieldHost,
        ShellFieldDone
    };

    void ResetSessionState();
    void RenderPrompt();

    void RequestConnectIfReady();
    void CloseConnection(const char *reason);

    static bool ParseIPv4Address(const char *text, CIPAddress &outIp);
    bool ParseHostAndPort(const char *hostToken, CIPAddress &outIp, u16 &outPort);

private:
    static CTWlanHost *s_pThis;

    CBcm4343Device *m_pWlan;
    CNetSubSystem *m_pNet;
    CWPASupplicant *m_pSupplicant;
    CLogger *m_pLogger;
    CTRenderer *m_pRenderer;
    CHAL *m_pHal;

    bool m_Initialized;
    bool m_Enabled;
    bool m_NetworkingReady;

    CSpinLock m_StateLock;

    // Prompt/input state (handled in kernel context via HandleKey)
    bool m_InputActive;
    bool m_PromptShown;
    TShellPromptField m_PromptField;
    CString m_InputBuffer;
    CString m_HostToken;

    // Connection state
    CSocket *m_pSocket;
    bool m_Connected;
    bool m_ConnectInProgress;
    bool m_ConnectRequested;
    bool m_SshDetected;

    CIPAddress m_RemoteIp;
    u16 m_RemotePort;

    unsigned m_RetryBackoff;
    bool m_PromptPending;
    bool m_PromptSkipReported;
    void DisplayPromptIfReady();
};
