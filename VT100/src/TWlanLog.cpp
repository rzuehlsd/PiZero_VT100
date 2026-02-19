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

#include "TWlanLog.h"
#include "kernel.h"
#include "TConfig.h"

#include <circle/logger.h>
#include <circle/net/in.h>
#include <circle/net/netconfig.h>
#include <circle/sched/scheduler.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#include <string.h>

namespace
{
static const unsigned RxChunkSize = 256;
static const char FromTerminal[] = "wlan-log";
static const unsigned NetworkWaitQuantumMs = 100;
static const unsigned NetworkWaitTimeoutSec = 60;
static const char HostEscapeSequence[] = "+++";

static const u8 TelnetIAC  = 255;
static const u8 TelnetDONT = 254;
static const u8 TelnetDO   = 253;
static const u8 TelnetWONT = 252;
static const u8 TelnetWILL = 251;
static const u8 TelnetSB   = 250;
static const u8 TelnetSE   = 240;

static const u8 TelnetOptEcho = 1;
static const u8 TelnetOptSuppressGoAhead = 3;
static const u8 TelnetOptLineMode = 34;

bool TryFormatIPAddress(const CIPAddress *ip, CString &out)
{
    out = "";

    if (ip == nullptr)
    {
        return false;
    }

    if (!ip->IsSet())
    {
        return false;
    }

    if (ip->IsNull())
    {
        return false;
    }

    ip->Format(&out);
    return out.GetLength() > 0;
}
}

CTWlanLog *CTWlanLog::s_pInstance = nullptr;

CTWlanLog *CTWlanLog::Get()
{
    if (s_pInstance == nullptr)
    {
        s_pInstance = new CTWlanLog();
    }
    return s_pInstance;
}

CTWlanLog::CTWlanLog()
    : CTask()
    , m_pWlan(nullptr)
    , m_pNet(nullptr)
    , m_pLogger(nullptr)
    , m_pSupplicant(nullptr)
    , m_pFallback(nullptr)
    , m_Port(0)
    , m_pListenSocket(nullptr)
    , m_pClientSocket(nullptr)
    , m_Initialized(false)
    , m_Activated(false)
    , m_StopRequested(false)
    , m_LoggerAttached(false)
    , m_RemoteLoggingActive(false)
    , m_HostModeActive(false)
    , m_HostDataPrimed(false)
    , m_HostEscapeMatch(0)
    , m_CommandPromptVisible(false)
    , m_LogLastWasCR(false)
    , m_CloseRequested(false)
    , m_LastRxWasCR(false)
    , m_TelnetNegotiated(false)
    , m_TelnetRxState(TelnetStateData)
    , m_TelnetCommand(0)
    , m_RxLineBuffer()
    , m_ConnectionLock()
    , m_SendLock()
{
    SetName("wlan-log");
    Suspend();
}

CTWlanLog::~CTWlanLog()
{
    Stop();
    WaitForTermination();
    CloseClient("shutting down");

    if (m_pListenSocket)
    {
        delete m_pListenSocket;
        m_pListenSocket = nullptr;
    }
}

bool CTWlanLog::Initialize(CBcm4343Device &wlan,
                           CNetSubSystem &net,
                           CWPASupplicant &supplicant,
                           CLogger &logger,
                           unsigned port,
                           CDevice *fallback)
{
    if (m_Initialized)
    {
        m_pFallback = fallback;
        return true;
    }

    m_pWlan = &wlan;
    m_pNet = &net;
    m_pLogger = &logger;
    m_pSupplicant = &supplicant;
    m_Port = port;
    m_pFallback = fallback;

    m_StopRequested = false;
    m_Activated = false;

    if (!m_pWlan->Initialize())
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogError, "WLAN logging: firmware load failed (check firmware files)");
        }
        return false;
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: firmware loaded");
    }

    if (!m_pNet->Initialize(FALSE))
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogError, "WLAN logging: network stack initialization failed");
        }
        return false;
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: network stack initialized");
    }

    if (!m_pSupplicant->Initialize())
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogError, "WLAN logging: WPA supplicant initialization failed");
        }
        return false;
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: WPA supplicant started");
        m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: telnet console prepared on port %u", m_Port);
    }

    m_Initialized = true;
    Start();
    return true;
}

void CTWlanLog::SetFallback(CDevice *fallback)
{
    m_pFallback = fallback;
}

bool CTWlanLog::Start()
{
    if (!m_Initialized)
    {
        return false;
    }

    m_StopRequested = false;
    m_Activated = true;
    m_RemoteLoggingActive = false;

    if (IsSuspended())
    {
        Resume();
    }

    if (!m_LoggerAttached && m_pLogger != nullptr)
    {
        m_pLogger->SetNewTarget(this);
        m_LoggerAttached = true;
        m_pLogger->Write(FromTerminal, LogNotice,
                         "WLAN logging: routing log output through telnet hub (local fallback retained)");
    }

    return true;
}

void CTWlanLog::Stop()
{
    m_StopRequested = true;
    m_Activated = false;

    if (m_LoggerAttached && m_pLogger != nullptr && m_pFallback != nullptr)
    {
        m_pLogger->SetNewTarget(m_pFallback);
        m_LoggerAttached = false;
        m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: reverted to local logging only");
    }
}

bool CTWlanLog::IsClientConnected() const
{
    m_ConnectionLock.Acquire();
    bool connected = m_pClientSocket != nullptr;
    m_ConnectionLock.Release();
    return connected;
}

bool CTWlanLog::IsHostModeActive() const
{
    return m_HostModeActive;
}

void CTWlanLog::Send(const char *buffer, size_t length)
{
    if (buffer == nullptr || length == 0)
    {
        return;
    }

    while (length > 0)
    {
        m_SendLock.Acquire();

        CSocket *client = nullptr;
        m_ConnectionLock.Acquire();
        client = m_pClientSocket;
        m_ConnectionLock.Release();

        if (client == nullptr)
        {
            m_SendLock.Release();
            return;
        }

        int sent = client->Send(buffer, length, 0);
        if (sent <= 0)
        {
            CloseClient("send failed", true);
            m_SendLock.Release();
            return;
        }

        m_SendLock.Release();

        buffer += sent;
        length -= static_cast<size_t>(sent);
    }
}

bool CTWlanLog::SendHostData(const char *buffer, size_t length)
{
    if (!m_HostModeActive)
    {
        return false;
    }

    if (!IsClientConnected())
    {
        return false;
    }

    Send(buffer, length);
    return true;
}

void CTWlanLog::SendLine(const char *line)
{
    if (line == nullptr)
    {
        return;
    }

    CString payload(line);
    payload += "\r\n";
    Send(payload.c_str(), payload.GetLength());
    m_CommandPromptVisible = false;
}

void CTWlanLog::SendCommandPrompt()
{
    static const char Prompt[] = ">: ";
    Send(Prompt, sizeof Prompt - 1);
    m_CommandPromptVisible = true;
}

int CTWlanLog::Write(const void *buffer, size_t count)
{
    if (buffer == nullptr || count == 0)
    {
        return 0;
    }

    if (m_pFallback)
    {
        m_pFallback->Write(buffer, count);
    }

    if (m_HostModeActive)
    {
        return static_cast<int>(count);
    }

    const char *text = static_cast<const char *>(buffer);

    CString normalized;
    bool endsWithLineBreak = false;
    for (size_t index = 0; index < count; ++index)
    {
        const char ch = text[index];

        if (ch == '\n')
        {
            if (!m_LogLastWasCR)
            {
                normalized += "\r";
            }
            normalized += "\n";
            m_LogLastWasCR = false;
            endsWithLineBreak = true;
            continue;
        }

        if (ch == '\r')
        {
            normalized += "\r";
            m_LogLastWasCR = true;
            endsWithLineBreak = true;
            continue;
        }

        normalized.Append(ch);
        m_LogLastWasCR = false;
        endsWithLineBreak = false;
    }

    if (!m_HostModeActive && IsClientConnected())
    {
        if (m_CommandPromptVisible)
        {
            static const char NewLine[] = "\r\n";
            Send(NewLine, sizeof NewLine - 1);
            m_CommandPromptVisible = false;
        }

        if (normalized.GetLength() > 0)
        {
            Send(normalized.c_str(), normalized.GetLength());
        }

        if (!endsWithLineBreak)
        {
            static const char NewLine[] = "\r\n";
            Send(NewLine, sizeof NewLine - 1);
        }

        SendCommandPrompt();
        return static_cast<int>(count);
    }

    if (normalized.GetLength() > 0)
    {
        Send(normalized.c_str(), normalized.GetLength());
    }
    return static_cast<int>(count);
}

void CTWlanLog::Run()
{
    if (!m_Initialized || m_pNet == nullptr)
    {
        return;
    }

    const unsigned logIntervalIterations = NetworkWaitQuantumMs != 0 ? (1000U / NetworkWaitQuantumMs) : 10U;
    unsigned waitIterations = 0;
    bool networkReadyAnnounced = false;
    bool waitingAnnounced = false;
    bool readyNoticeLogged = false;

    while (!m_StopRequested)
    {
        if (!m_Activated)
        {
            waitIterations = 0;
            networkReadyAnnounced = false;
            waitingAnnounced = false;
            readyNoticeLogged = false;
            CScheduler::Get()->MsSleep(50);
            continue;
        }

        if (m_pNet == nullptr || !m_pNet->IsRunning())
        {
            readyNoticeLogged = false;

            if (!waitingAnnounced && m_pLogger)
            {
                m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: waiting for network connection...");
                waitingAnnounced = true;
            }

            ++waitIterations;

            if (logIntervalIterations != 0 && waitIterations % logIntervalIterations == 0)
            {
                unsigned elapsedSeconds = (waitIterations * NetworkWaitQuantumMs) / 1000U;
                boolean associated = CWPASupplicant::IsConnected();
                if (m_pLogger)
                {
                    m_pLogger->Write(FromTerminal, LogNotice,
                                     "WLAN logging: still waiting (%us elapsed, supplicant %s)",
                                     elapsedSeconds, associated ? "connected" : "not connected");
                }

                if ((elapsedSeconds == 15 || elapsedSeconds == 30) && m_pWlan != nullptr)
                {
                    m_pWlan->DumpStatus();
                }
            }

            if (waitIterations * NetworkWaitQuantumMs >= NetworkWaitTimeoutSec * 1000U)
            {
                if (m_pLogger)
                {
                    m_pLogger->Write(FromTerminal, LogError,
                                     "WLAN logging: network not ready after %us – remote console disabled",
                                     NetworkWaitTimeoutSec);
                }
                readyNoticeLogged = false;

                if (m_RemoteLoggingActive)
                {
                    CloseClient("network offline");
                }

                if (m_LoggerAttached && m_pLogger != nullptr && m_pFallback != nullptr)
                {
                    m_pLogger->SetNewTarget(m_pFallback);
                    m_LoggerAttached = false;
                }

                m_Activated = false;
                waitIterations = 0;
                networkReadyAnnounced = false;
                waitingAnnounced = false;
                readyNoticeLogged = false;
            }

            CScheduler::Get()->MsSleep(NetworkWaitQuantumMs);
            continue;
        }

        waitIterations = 0;

        if (!networkReadyAnnounced)
        {
            CString ipString;
            bool haveIP = false;
            const CNetConfig *config = m_pNet->GetConfig();
            if (config != nullptr)
            {
                haveIP = TryFormatIPAddress(config->GetIPAddress(), ipString);
            }

            if (m_pLogger)
            {
                if (haveIP)
                {
                    m_pLogger->Write(FromTerminal, LogNotice,
                                     "WLAN logging: network ready – IP %s", (const char *)ipString);
                    m_pLogger->Write(FromTerminal, LogNotice,
                                     "WLAN logging: connect via 'telnet %s %u'",
                                     (const char *)ipString, m_Port);
                }
                else
                {
                    m_pLogger->Write(FromTerminal, LogNotice,
                                     "WLAN logging: network ready – IP address pending");
                    m_pLogger->Write(FromTerminal, LogNotice,
                                     "WLAN logging: telnet console listening on port %u", m_Port);
                }

                m_pLogger->Write(FromTerminal, LogNotice,
                                 "WLAN logging: type 'help' for available telnet commands");
            }

            networkReadyAnnounced = true;
            waitingAnnounced = false;
            readyNoticeLogged = false;

            if (!m_LoggerAttached && m_pLogger != nullptr)
            {
                m_pLogger->SetNewTarget(this);
                m_LoggerAttached = true;
                m_pLogger->Write(FromTerminal, LogNotice,
                                 "WLAN logging: routing log output through telnet hub (local fallback retained)");
            }
        }

        if (!EnsureListenSocket())
        {
            CScheduler::Get()->MsSleep(100);
            continue;
        }

        if (!readyNoticeLogged && networkReadyAnnounced && m_pLogger && m_pListenSocket != nullptr)
        {
            CString ipString;
            const CNetConfig *config = m_pNet->GetConfig();
            bool haveIP = config != nullptr && TryFormatIPAddress(config->GetIPAddress(), ipString);
            if (haveIP)
            {
                m_pLogger->Write(FromTerminal, LogNotice,
                                 "WLAN logging: telnet console ready on %s:%u",
                                 (const char *)ipString, m_Port);
            }
            else
            {
                m_pLogger->Write(FromTerminal, LogNotice,
                                 "WLAN logging: telnet console ready on port %u", m_Port);
            }
            readyNoticeLogged = true;
        }

        AcceptClient();
        HandleIncomingData();

        CScheduler::Get()->MsSleep(10);
    }

    CloseClient("server stopped");

    if (m_LoggerAttached && m_pLogger != nullptr && m_pFallback != nullptr)
    {
        m_pLogger->SetNewTarget(m_pFallback);
        m_LoggerAttached = false;
    }
}

void CTWlanLog::ProcessLine(const char *line)
{
    if (line == nullptr)
    {
        return;
    }

    if (line[0] == '\0')
    {
        SendLine("");
        return;
    }

    if (strcmp(line, "help") == 0)
    {
        SendLine("Available commands:");
        SendLine("  help   - show this text");
        SendLine("  status - show WLAN status");
        SendLine("  echo <text> - repeat text back to you");
        SendLine("  host on  - bridge TCP session as terminal host");
        SendLine("  exit   - disconnect this session");
        SendLine("In host mode: use Ctrl-C or type +++ to return.");
        SendLine("Other text is logged at notice level.");
        return;
    }

    if (strcmp(line, "host on") == 0)
    {
        m_HostModeActive = true;
        m_HostDataPrimed = false;
        SendLine("Host bridge mode enabled.");
        SendLine("Keyboard TX and screen RX now use TCP.");
        SendLine("Press Ctrl-C or type +++ to return to command mode.");
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogNotice, "Host bridge mode enabled");
        }
        return;
    }

    if (strncmp(line, "echo", 4) == 0)
    {
        const char *payload = line + 4;
        while (*payload == ' ')
        {
            ++payload;
        }

        if (*payload == '\0')
        {
            SendLine("Usage: echo <text>");
        }
        else
        {
            SendLine(payload);
        }
        return;
    }

    if (strcmp(line, "exit") == 0)
    {
        SendLine("Closing connection. Bye.");
        m_CloseRequested = true;
        return;
    }

    if (strcmp(line, "status") == 0)
    {
        if (m_pNet == nullptr || !m_pNet->IsRunning())
        {
            SendLine("Network stack not running yet – wait for DHCP/authentication");
            return;
        }

        auto *config = m_pNet->GetConfig();
        if (config == nullptr || config->GetIPAddress() == nullptr)
        {
            SendLine("Network configuration not available yet");
            return;
        }

        CString ipString;
        bool haveIP = TryFormatIPAddress(config->GetIPAddress(), ipString);

        CString gatewayString;
        bool haveGateway = TryFormatIPAddress(config->GetDefaultGateway(), gatewayString);

        CString dnsString;
        bool haveDNS = TryFormatIPAddress(config->GetDNSServer(), dnsString);

        CString hostname;
        hostname = m_pNet->GetHostname();

        CString statusLine;
        statusLine.Format("Hostname: %s", hostname.c_str());
        SendLine(statusLine.c_str());

        if (haveIP)
        {
            statusLine.Format("IP: %s", (const char *)ipString);
            SendLine(statusLine.c_str());
        }
        else
        {
            SendLine("IP: pending");
        }

        if (haveGateway)
        {
            statusLine.Format("Gateway: %s", (const char *)gatewayString);
            SendLine(statusLine.c_str());
        }

        if (haveDNS)
        {
            statusLine.Format("DNS: %s", (const char *)dnsString);
            SendLine(statusLine.c_str());
        }

        return;
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogNotice, "Remote: %s", line);
    }
    SendLine("Logged your message. Use status/help for built-in commands.");
}

bool CTWlanLog::EnsureListenSocket()
{
    if (m_pListenSocket != nullptr)
    {
        return true;
    }

    if (m_pNet == nullptr)
    {
        return false;
    }

    m_pListenSocket = new CSocket(m_pNet, IPPROTO_TCP);
    if (m_pListenSocket == nullptr)
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogError, "Unable to allocate listen socket");
        }
        CScheduler::Get()->MsSleep(500);
        return false;
    }

    if (m_pListenSocket->Bind(m_Port) < 0)
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogError, "Cannot bind port %u", m_Port);
        }
        delete m_pListenSocket;
        m_pListenSocket = nullptr;
        CScheduler::Get()->MsSleep(1000);
        return false;
    }

    if (m_pListenSocket->Listen(1) < 0)
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogError, "Cannot listen on port %u", m_Port);
        }
        delete m_pListenSocket;
        m_pListenSocket = nullptr;
        CScheduler::Get()->MsSleep(1000);
        return false;
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogNotice, "Waiting for TCP client on port %u", m_Port);
    }
    return true;
}

void CTWlanLog::AcceptClient()
{
    if (m_pListenSocket == nullptr)
    {
        return;
    }

    m_ConnectionLock.Acquire();
    bool hasClient = m_pClientSocket != nullptr;
    m_ConnectionLock.Release();
    if (hasClient)
    {
        return;
    }

    CIPAddress remoteIP;
    u16 remotePort = 0;
    CSocket *newClient = m_pListenSocket->Accept(&remoteIP, &remotePort);
    if (newClient == nullptr)
    {
        return;
    }

    m_ConnectionLock.Acquire();
    m_pClientSocket = newClient;
    m_ConnectionLock.Release();

    ResetConnectionState();
    CTConfig *config = CTConfig::Get();
    const bool autoHostMode = (config != nullptr && config->GetWlanHostAutoStart() != 0U);
    if (autoHostMode)
    {
        m_HostModeActive = true;
        m_HostDataPrimed = false;
    }

    if (!autoHostMode)
    {
        SendTelnetNegotiation();
    }

    if (!autoHostMode)
    {
        AnnounceConnection(remoteIP, remotePort);
    }
    else if (m_pLogger)
    {
        CString ipString;
        bool haveIP = TryFormatIPAddress(&remoteIP, ipString);
        if (haveIP)
        {
            m_pLogger->Write(FromTerminal, LogNotice,
                             "Client connected from %s:%u (host auto-start active)",
                             (const char *)ipString, remotePort);
        }
        else
        {
            m_pLogger->Write(FromTerminal, LogNotice,
                             "Client connected (address pending): port %u (host auto-start active)",
                             remotePort);
        }
    }

    if (autoHostMode)
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogNotice, "Host bridge mode auto-enabled");
        }
    }
    else
    {
        SendCommandPrompt();
    }

    m_RemoteLoggingActive = true;

    if (CKernel *kernel = CKernel::Get())
    {
        kernel->MarkTelnetReady();
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogNotice, "WLAN logging: mirroring logs to remote console");
    }
}

void CTWlanLog::CloseClient(const char *reason, bool sendLocked)
{
    bool disconnected = false;

    if (!sendLocked)
    {
        m_SendLock.Acquire();
    }

    CSocket *client = nullptr;
    m_ConnectionLock.Acquire();
    client = m_pClientSocket;
    m_pClientSocket = nullptr;
    m_ConnectionLock.Release();

    if (client)
    {
        disconnected = true;
        delete client;
        ResetConnectionState();
        m_RemoteLoggingActive = false;

        if (CKernel *kernel = CKernel::Get())
        {
            CString disconnectMsg;
            bool resumeLocalAfterDisconnect = false;
            if (reason != nullptr)
            {
                disconnectMsg.Format("\r\nTelnet client disconnected (%s)\r\n", reason);
                resumeLocalAfterDisconnect = (strcmp(reason, "requested by client") == 0)
                                           || (strcmp(reason, "receive failed") == 0)
                                           || (strcmp(reason, "send failed") == 0);
            }
            else
            {
                disconnectMsg = "\r\nTelnet client disconnected\r\n";
            }
            kernel->HandleWlanHostRx(disconnectMsg.c_str(), disconnectMsg.GetLength());
            if (m_StopRequested)
            {
                kernel->MarkTelnetReady();
            }
            else if (resumeLocalAfterDisconnect)
            {
                kernel->MarkTelnetReady();
            }
            else
            {
                kernel->MarkTelnetWaiting();
            }
        }

    }

    if (!sendLocked)
    {
        m_SendLock.Release();
    }

    if (disconnected && m_pLogger)
    {
        if (reason != nullptr)
        {
            m_pLogger->Write(FromTerminal, LogNotice, "Client disconnected (%s)", reason);
        }
        else
        {
            m_pLogger->Write(FromTerminal, LogNotice, "Client disconnected");
        }

        if (m_pFallback != nullptr)
        {
            m_pLogger->Write(FromTerminal, LogNotice,
                             "WLAN logging: remote console closed – falling back to local output only");
        }
    }
}

void CTWlanLog::HandleIncomingData()
{
    CSocket *client = nullptr;
    m_ConnectionLock.Acquire();
    client = m_pClientSocket;
    m_ConnectionLock.Release();

    if (client == nullptr)
    {
        return;
    }

    char buffer[RxChunkSize];
    int received = client->Receive(buffer, sizeof buffer, 0);
    if (received <= 0)
    {
        if (m_pLogger)
        {
            m_pLogger->Write(FromTerminal, LogNotice, "Receive returned %d", received);
        }
        CloseClient("receive failed");
        return;
    }

    CString chunkLog;
    for (int i = 0; i < received; ++i)
    {
        u8 uch = static_cast<u8>(buffer[i]);
        if (uch >= 32 && uch <= 126)
        {
            chunkLog.Append(static_cast<char>(uch));
        }
        else
        {
            CString token;
            token.Format("<%02X>", uch);
            chunkLog += token;
        }
        HandleIncomingByte(uch);

        if (m_CloseRequested)
        {
            break;
        }
    }

    if (m_CloseRequested)
    {
        m_CloseRequested = false;
        CloseClient("requested by client");
        return;
    }

    if (m_pLogger)
    {
        m_pLogger->Write(FromTerminal, LogDebug, "RX chunk: %s", chunkLog.c_str());
    }
}

void CTWlanLog::HandleIncomingByte(u8 byte)
{
    if (m_CloseRequested)
    {
        return;
    }

    if (HandleTelnetByte(byte))
    {
        return;
    }

    if (m_HostModeActive)
    {
        if (byte == 0x03)
        {
            m_HostModeActive = false;
            m_HostDataPrimed = false;
            m_HostEscapeMatch = 0;
            SendLine("Host bridge mode disabled (Ctrl-C). Command/log mode active.");
            SendCommandPrompt();
            if (m_pLogger)
            {
                m_pLogger->Write(FromTerminal, LogNotice, "Host bridge mode disabled by Ctrl-C escape");
            }
            return;
        }

        const size_t escapeLength = sizeof(HostEscapeSequence) - 1;
        if (m_HostEscapeMatch > 0)
        {
            if (byte == static_cast<u8>(HostEscapeSequence[m_HostEscapeMatch]))
            {
                ++m_HostEscapeMatch;
                if (m_HostEscapeMatch >= escapeLength)
                {
                    m_HostModeActive = false;
                    m_HostDataPrimed = false;
                    m_HostEscapeMatch = 0;
                    SendLine("Host bridge mode disabled (+++). Command/log mode active.");
                    SendCommandPrompt();
                    if (m_pLogger)
                    {
                        m_pLogger->Write(FromTerminal, LogNotice, "Host bridge mode disabled by +++ escape");
                    }
                }
                return;
            }

            CKernel *kernel = CKernel::Get();
            if (kernel != nullptr)
            {
                kernel->HandleWlanHostRx(HostEscapeSequence, m_HostEscapeMatch);
            }

            if (byte == static_cast<u8>(HostEscapeSequence[0]))
            {
                m_HostEscapeMatch = 1;
                return;
            }

            m_HostEscapeMatch = 0;
        }

        if (byte == static_cast<u8>(HostEscapeSequence[0]))
        {
            m_HostEscapeMatch = 1;
            return;
        }

        if (!m_HostDataPrimed)
        {
            if (byte == 0x1B)
            {
                m_HostDataPrimed = true;
            }
            else if (byte >= 32 && byte <= 126)
            {
                m_HostDataPrimed = true;
            }
            else if (byte == '\r' || byte == '\n' || byte == '\t')
            {
                return;
            }
            else
            {
                return;
            }
        }

        CKernel *kernel = CKernel::Get();
        if (kernel != nullptr)
        {
            char ch = static_cast<char>(byte);
            kernel->HandleWlanHostRx(&ch, 1);
        }
        return;
    }

    HandleCommandChar(static_cast<char>(byte));
}

void CTWlanLog::HandleCommandChar(char ch)
{
    if (ch == '\0' && m_LastRxWasCR)
    {
        m_LastRxWasCR = false;
        return;
    }

    if (ch == '\r' || ch == '\n')
    {
        bool duplicateLF = (ch == '\n' && m_LastRxWasCR);
        m_LastRxWasCR = (ch == '\r');

        if (duplicateLF)
        {
            return;
        }

        if (m_RxLineBuffer.GetLength() > 0)
        {
            static const char NewLine[] = "\r\n";
            Send(NewLine, sizeof NewLine - 1);
            m_CommandPromptVisible = false;

            CString line = m_RxLineBuffer;
            m_RxLineBuffer = "";
            if (m_pLogger)
            {
                m_pLogger->Write(FromTerminal, LogDebug, "Received line: %s", line.c_str());
            }
            ProcessLine(line.c_str());
        }
        else
        {
            static const char NewLine[] = "\r\n";
            Send(NewLine, sizeof NewLine - 1);
            m_CommandPromptVisible = false;
        }

        if (!m_HostModeActive && IsClientConnected())
        {
            SendCommandPrompt();
        }
        return;
    }

    m_LastRxWasCR = false;

    if (ch == '\b' || ch == 0x7F)
    {
        unsigned length = m_RxLineBuffer.GetLength();
        if (length > 0)
        {
            CString truncated;
            const char *text = m_RxLineBuffer.c_str();
            for (unsigned i = 0; i + 1 < length; ++i)
            {
                truncated.Append(text[i]);
            }
            m_RxLineBuffer = truncated;
        }
        return;
    }

    if (ch >= 32 && ch <= 126)
    {
        if (m_RxLineBuffer.GetLength() < 200)
        {
            m_RxLineBuffer.Append(ch);
        }
    }
}

bool CTWlanLog::HandleTelnetByte(u8 byte)
{
    switch (m_TelnetRxState)
    {
    case TelnetStateData:
        if (byte == TelnetIAC)
        {
            m_TelnetRxState = TelnetStateIAC;
            return true;
        }
        return false;

    case TelnetStateIAC:
        if (byte == TelnetIAC)
        {
            m_TelnetRxState = TelnetStateData;
            return true;
        }

        if (byte == TelnetDO || byte == TelnetDONT || byte == TelnetWILL || byte == TelnetWONT)
        {
            m_TelnetCommand = byte;
            m_TelnetRxState = TelnetStateCommand;
            return true;
        }

        if (byte == TelnetSB)
        {
            m_TelnetRxState = TelnetStateSubnegotiation;
            return true;
        }

        m_TelnetRxState = TelnetStateData;
        return true;

    case TelnetStateCommand:
        if (m_TelnetCommand == TelnetDO)
        {
            if (byte == TelnetOptSuppressGoAhead || byte == TelnetOptEcho)
            {
                SendTelnetCommand(TelnetWILL, byte);
            }
            else
            {
                SendTelnetCommand(TelnetWONT, byte);
            }
        }
        else if (m_TelnetCommand == TelnetWILL)
        {
            if (byte == TelnetOptSuppressGoAhead)
            {
                SendTelnetCommand(TelnetDO, byte);
            }
            else
            {
                SendTelnetCommand(TelnetDONT, byte);
            }
        }

        m_TelnetRxState = TelnetStateData;
        return true;

    case TelnetStateSubnegotiation:
        if (byte == TelnetIAC)
        {
            m_TelnetRxState = TelnetStateSubnegotiationIAC;
        }
        return true;

    case TelnetStateSubnegotiationIAC:
        if (byte == TelnetSE)
        {
            m_TelnetRxState = TelnetStateData;
        }
        else
        {
            m_TelnetRxState = TelnetStateSubnegotiation;
        }
        return true;
    }

    m_TelnetRxState = TelnetStateData;
    return false;
}

void CTWlanLog::SendTelnetCommand(u8 verb, u8 option)
{
    char sequence[3];
    sequence[0] = static_cast<char>(TelnetIAC);
    sequence[1] = static_cast<char>(verb);
    sequence[2] = static_cast<char>(option);
    Send(sequence, sizeof sequence);
}

void CTWlanLog::SendTelnetNegotiation()
{
    if (m_TelnetNegotiated)
    {
        return;
    }

    SendTelnetCommand(TelnetWILL, TelnetOptSuppressGoAhead);
    SendTelnetCommand(TelnetDO, TelnetOptSuppressGoAhead);
    SendTelnetCommand(TelnetWILL, TelnetOptEcho);
    SendTelnetCommand(TelnetDONT, TelnetOptLineMode);

    m_TelnetNegotiated = true;
}

void CTWlanLog::AnnounceConnection(const CIPAddress &remoteIP, u16 remotePort)
{
    CString ipString;
    bool haveIP = TryFormatIPAddress(&remoteIP, ipString);

    if (m_pLogger)
    {
        if (haveIP)
        {
            m_pLogger->Write(FromTerminal, LogNotice,
                             "Client connected from %s:%u", (const char *)ipString, remotePort);
        }
        else
        {
            m_pLogger->Write(FromTerminal, LogNotice,
                             "Client connected (address pending): port %u", remotePort);
        }
    }

    SendLine("Welcome to the Circle WLAN logging console");
    SendLine("WLAN mode is active. Please wait while network connection is established.");
    SendLine("Log output is mirrored here once the system starts logging.");
    SendLine("Type 'help' for a list of commands.");
}

void CTWlanLog::ResetConnectionState()
{
    m_RxLineBuffer = "";
    m_HostModeActive = false;
    m_HostDataPrimed = false;
    m_HostEscapeMatch = 0;
    m_CommandPromptVisible = false;
    m_LogLastWasCR = false;
    m_CloseRequested = false;
    m_LastRxWasCR = false;
    m_TelnetNegotiated = false;
    m_TelnetRxState = TelnetStateData;
    m_TelnetCommand = 0;
}
