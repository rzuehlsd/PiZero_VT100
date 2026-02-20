//------------------------------------------------------------------------------
// Module:        CTWlanLog
// Description:   Provides a WLAN-backed logging sink and telnet-style mirror.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-02-02
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-02-02     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

// Circle core components
#include <circle/device.h>
#include <circle/net/ipaddress.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/sched/task.h>
#include <circle/spinlock.h>
#include <circle/string.h>
#include <circle/types.h>

/**
 * @file TWlanLog.h
 * @brief Declares the WLAN-backed logging device and task.
 * @details CTWlanLog exposes Circle logger messages over a TCP socket while
 * mirroring output to a local fallback. It coordinates Wi-Fi bring-up,
 * handles client lifecycle, and provides hooks for preprocessing log lines so
 * the VT100 firmware can offer remote diagnostics similar to telnet access.
 */

// Forward declarations
class CLogger;
class CBcm4343Device;
class CWPASupplicant;

/**
 * @class CTWlanLog
 * @brief Network logging endpoint streaming log traffic to remote clients.
 * @details The singleton listens on a configurable TCP port, attaches to the
 * kernel logger, and forwards log lines to the connected client. It enforces
 * thread-safe send and connection management with spin locks and can fall back
 * to a local device whenever the WLAN link is unavailable.
 */
class CTWlanLog : public CDevice, public CTask
{
public:
    /// \brief Access the singleton WLAN log device.
    static CTWlanLog *Get();

    /// \brief Construct the WLAN logging task.
    CTWlanLog();
    /// \brief Ensure graceful shutdown of sockets.
    ~CTWlanLog() override;

    /// \brief Initialize sockets, WLAN hardware access and logger integration.
    bool Initialize(CBcm4343Device &wlan,
                    CNetSubSystem &net,
                    CWPASupplicant &supplicant,
                    CLogger &logger,
                    unsigned port,
                    CDevice *fallback);

    /// \brief Change the fallback logging device for pass-through output.
    void SetFallback(CDevice *fallback);

    /// \brief Start accepting clients and attach to the logger.
    bool Start();
    /// \brief Stop serving remote clients and detach from logger.
    void Stop();
    /// \brief Check whether a remote client is currently connected.
    bool IsClientConnected() const;
    /// \brief Check whether active session is in TCP host bridge mode.
    bool IsHostModeActive() const;

    /// \brief Send raw data to the active client if present.
    void Send(const char *buffer, size_t length);
    /// \brief Send host-bound data when host bridge mode is active.
    bool SendHostData(const char *buffer, size_t length);
    /// \brief Send a newline-terminated string to the client.
    void SendLine(const char *line);
    /// \brief Send the command-mode prompt to the active client.
    void SendCommandPrompt();

    /// \brief Write intercepted log output to the remote client and fallback.
    int Write(const void *buffer, size_t count) override;

    /// \brief Scheduler entry point handling socket activity.
    void Run() override;

protected:
    /// \brief Hook for processing complete log lines before transmit.
    virtual void ProcessLine(const char *line);

private:
    enum ETelnetRxState
    {
        TelnetStateData,
        TelnetStateIAC,
        TelnetStateCommand,
        TelnetStateSubnegotiation,
        TelnetStateSubnegotiationIAC
    };

    /// \brief Ensure the listening socket is created and bound.
    bool EnsureListenSocket();
    /// \brief Accept a pending client connection.
    void AcceptClient();
    /// \brief Close client connection with optional notification.
    void CloseClient(const char *reason, bool sendLocked = false);
    /// \brief Handle inbound data from the connected client.
    void HandleIncomingData();
    /// \brief Process one received byte including telnet control handling.
    void HandleIncomingByte(u8 byte);
    /// \brief Interpret in-band control characters from client input.
    void HandleCommandChar(char ch);
    /// \brief Consume telnet protocol bytes and negotiate options.
    bool HandleTelnetByte(u8 byte);
    /// \brief Send one telnet command triplet (IAC verb option).
    void SendTelnetCommand(u8 verb, u8 option);
    /// \brief Emit a minimal telnet negotiation set for terminal clients.
    void SendTelnetNegotiation();
    /// \brief Log client connection information.
    void AnnounceConnection(const CIPAddress &remoteIP, u16 remotePort);
    /// \brief Reset connection state tracking variables.
    void ResetConnectionState();

private:
    static CTWlanLog *s_pInstance;

    CBcm4343Device *m_pWlan;
    CNetSubSystem *m_pNet;
    CLogger *m_pLogger;
    CWPASupplicant *m_pSupplicant;
    CDevice *m_pFallback;
    unsigned m_Port;

    CSocket *m_pListenSocket;
    CSocket *m_pClientSocket;

    bool m_Initialized;
    bool m_Activated;
    bool m_StopRequested;
    bool m_LoggerAttached;
    bool m_RemoteLoggingActive;
    bool m_HostModeActive;
    bool m_CommandPromptVisible;
    bool m_LogLastWasCR;
    bool m_CloseRequested;
    bool m_LastRxWasCR;
    bool m_TelnetNegotiated;
    ETelnetRxState m_TelnetRxState;
    u8 m_TelnetCommand;

    CString m_RxLineBuffer;
    mutable CSpinLock m_ConnectionLock;
    mutable CSpinLock m_SendLock;
};
