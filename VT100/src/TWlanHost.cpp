#include "TWlanHost.h"

#include "TConfig.h"
#include "kernel.h"
#include "TRenderer.h"
#include "hal.h"

#include <circle/logger.h>
#include <circle/net/error.h>
#include <circle/net/in.h>
#include <circle/sched/scheduler.h>
#include <circle/netdevice.h>
#include <stdlib.h>
#include <string.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

LOGMODULE("TWlanHost");

namespace
{
static const char FromTerminal[] = "wlan-host";
static const u16 DefaultRawPort = 2323;

static bool IsScreenOutputBlocked()
{
    CKernel *kernel = CKernel::Get();
    return kernel != nullptr && kernel->IsScreenOutputBlocked();
}

static void WriteRendererMessage(CTRenderer *pRenderer, const void *pBuffer, size_t nCount)
{
    if (pRenderer == nullptr || pBuffer == nullptr || nCount == 0 || IsScreenOutputBlocked())
    {
        return;
    }

    pRenderer->Write(pBuffer, nCount);
}
}

CTWlanHost *CTWlanHost::s_pThis = nullptr;

CTWlanHost *CTWlanHost::Get()
{
    if (s_pThis == nullptr)
    {
        s_pThis = new CTWlanHost();
    }
    return s_pThis;
}

CTWlanHost::CTWlanHost()
    : CTask()
    , m_pWlan(nullptr)
    , m_pNet(nullptr)
    , m_pSupplicant(nullptr)
    , m_pLogger(nullptr)
    , m_pRenderer(nullptr)
    , m_pHal(nullptr)
    , m_Initialized(false)
    , m_Enabled(false)
    , m_NetworkingReady(false)
    , m_StateLock()
    , m_InputActive(false)
    , m_PromptShown(false)
    , m_PromptField(ShellFieldDone)
    , m_InputBuffer()
    , m_HostToken()
    , m_pSocket(nullptr)
    , m_Connected(false)
    , m_ConnectInProgress(false)
    , m_ConnectRequested(false)
    , m_SshDetected(false)
    , m_RemoteIp()
    , m_RemotePort(0)
    , m_RetryBackoff(0U)
    , m_PromptPending(false)
    , m_PromptSkipReported(false)
{
    SetName("wlan-host");
    Suspend();
}

CTWlanHost::~CTWlanHost()
{
    CloseConnection("shutdown");
}

bool CTWlanHost::Initialize(CBcm4343Device &wlan,
                            CNetSubSystem &net,
                            CWPASupplicant &supplicant,
                            CLogger &logger,
                            CTRenderer *renderer,
                            CHAL *hal)
{
    m_pWlan = &wlan;
    m_pNet = &net;
    m_pSupplicant = &supplicant;
    m_pLogger = &logger;
    m_pRenderer = renderer;
    m_pHal = hal;

    if (!m_Initialized)
    {
        m_Initialized = true;
        Start();
    }

    return true;
}

void CTWlanHost::SetEnabled(bool enabled)
{
    if (enabled == m_Enabled)
    {
        return;
    }

    m_Enabled = enabled;

    if (!m_Enabled)
    {
        CloseConnection("disabled");
        m_NetworkingReady = false;
        m_InputActive = false;
        m_PromptShown = false;
        return;
    }

    ResetSessionState();

}

bool CTWlanHost::IsEnabled() const
{
    return m_Enabled;
}

void CTWlanHost::ResetSessionState()
{
    m_StateLock.Acquire();

    m_InputActive = false;
    m_PromptShown = false;
    m_PromptField = ShellFieldHost;

    m_InputBuffer = "";
    m_HostToken = "";

    m_Connected = false;
    m_ConnectInProgress = false;
    m_ConnectRequested = false;
    m_SshDetected = false;

    m_RemotePort = DefaultRawPort;
    m_RetryBackoff = 0U;
    m_PromptPending = true;
    m_PromptSkipReported = false;

    if (m_pSocket != nullptr)
    {
        delete m_pSocket;
        m_pSocket = nullptr;
    }

    m_StateLock.Release();
}

void CTWlanHost::RenderPrompt()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    switch (m_PromptField)
    {
    case ShellFieldHost:
        WriteRendererMessage(m_pRenderer, "Host: ", 6);
        break;
    case ShellFieldDone:
    default:
        break;
    }
}

void CTWlanHost::DisplayPromptIfReady()
{
    m_StateLock.Acquire();
    if (!m_Enabled || !m_PromptPending || m_InputActive || !m_NetworkingReady)
    {
        m_StateLock.Release();
        if (m_NetworkingReady && !m_PromptSkipReported)
        {
            LOGNOTE("Prompt skip");
            m_PromptSkipReported = true;
        }
        return;
    }

    m_InputActive = true;
    m_PromptShown = true;
    m_PromptField = ShellFieldHost;
    m_InputBuffer = "";
    m_HostToken = "";
    m_PromptPending = false;
    m_PromptSkipReported = false;
    m_StateLock.Release();

    // If a default host_id is configured, auto-start the shell-client connection
    // without prompting for an interactive host entry.
    {
        CTConfig *config = CTConfig::Get();
        const char *hostId = (config != nullptr) ? config->GetHostId() : nullptr;
        if (hostId != nullptr && hostId[0] != '\0')
        {
            m_StateLock.Acquire();
            m_HostToken = hostId;
            m_PromptField = ShellFieldDone;
            m_InputActive = false;
            m_ConnectInProgress = false;
            m_ConnectRequested = false;
            m_Connected = false;
            m_SshDetected = false;
            m_RetryBackoff = 0U;
            m_StateLock.Release();

            LOGNOTE("Shell target from host_id: %s", m_HostToken.c_str());

            if (m_pRenderer != nullptr)
            {
                CString summary;
                summary.Format("\r\nShell target from host_id: host=%s\r\n", (const char *)m_HostToken);
                WriteRendererMessage(m_pRenderer, summary.c_str(), summary.GetLength());
                WriteRendererMessage(m_pRenderer, "Starting outbound shell-client connection...\r\n", 45);
            }
            return;
        }
    }

    if (m_pRenderer != nullptr)
    {
        static const char Msg[] =
            "\r\nShell Client mode selected (wlan_host_autostart=2).\r\n"
            "Enter target host (IPv4[:port], default 2323).\r\n";
        WriteRendererMessage(m_pRenderer, Msg, sizeof Msg - 1);
        RenderPrompt();
    }
}

bool CTWlanHost::HandleKey(const char *pString)
{
    if (!m_Enabled || pString == nullptr || pString[0] == '\0')
    {
        return false;
    }

    if (!m_InputActive)
    {
        return false;
    }

    const size_t inputLen = strlen(pString);
    for (size_t i = 0; i < inputLen; ++i)
    {
        const char ch = pString[i];

        if (ch == '\r' || ch == '\n')
        {
            if (m_InputBuffer.GetLength() == 0)
            {
                if (m_pRenderer != nullptr)
                {
                    WriteRendererMessage(m_pRenderer, "\r\nInput required. Please retry.\r\n", 34);
                    RenderPrompt();
                }
                continue;
            }

            if (m_PromptField == ShellFieldHost)
            {
                m_HostToken = m_InputBuffer;
                m_PromptField = ShellFieldDone;
                m_InputActive = false;
                m_ConnectInProgress = false;
                m_ConnectRequested = false;
                m_Connected = false;
                m_SshDetected = false;
                m_RetryBackoff = 0U;
                LOGNOTE("Shell target submitted: %s", m_HostToken.c_str());
            }

            m_InputBuffer = "";

            if (m_pRenderer != nullptr)
            {
                WriteRendererMessage(m_pRenderer, "\r\n", 2);

                if (m_PromptField != ShellFieldDone)
                {
                    RenderPrompt();
                }
                else
                {
                    CString summary;
                    summary.Format("Shell target captured: host=%s\r\n", (const char *)m_HostToken);
                    WriteRendererMessage(m_pRenderer, summary.c_str(), summary.GetLength());
                    WriteRendererMessage(m_pRenderer, "Starting outbound shell-client connection...\r\n", 45);
                }
            }

            continue;
        }

        if (ch == '\b' || ch == 0x7F)
        {
            const size_t currentLen = m_InputBuffer.GetLength();
            if (currentLen > 0)
            {
                CString truncated;
                const char *current = m_InputBuffer.c_str();
                for (size_t pos = 0; pos < currentLen - 1; ++pos)
                {
                    truncated.Append(current[pos]);
                }
                m_InputBuffer = truncated.c_str();
                if (m_pRenderer != nullptr)
                {
                    WriteRendererMessage(m_pRenderer, "\b \b", 3);
                }
            }
            continue;
        }

        if (ch < 32 || ch > 126)
        {
            continue;
        }

        m_InputBuffer.Append(ch);

        if (m_pRenderer != nullptr)
        {
            WriteRendererMessage(m_pRenderer, &ch, 1);
        }
    }

    return true;
}

void CTWlanHost::Tick()
{
    if (!m_Enabled)
    {
        return;
    }

    if (!m_NetworkingReady)
    {
        // Allow kernel to stay responsive even if WLAN bring-up fails.
        return;
    }

    if (m_pNet != nullptr)
    {
        m_pNet->Process();
    }

    RequestConnectIfReady();

    // Non-blocking RX while connected.
    m_StateLock.Acquire();
    const bool connected = m_Connected;
    CSocket *sock = m_pSocket;
    m_StateLock.Release();

    if (connected && sock != nullptr)
    {
        u8 rx[FRAME_BUFFER_SIZE];
        const unsigned int maxIterations = 4U;
        unsigned int iteration = 0U;
        while (iteration < maxIterations)
        {
            const int received = sock->Receive(rx, sizeof(rx), MSG_DONTWAIT);
            if (received > 0)
            {
                if (!m_SshDetected && received >= 4 && memcmp(rx, "SSH-", 4) == 0)
                {
                    m_SshDetected = true;
                    if (m_pRenderer != nullptr)
                    {
                        static const char Msg[] =
                            "\r\nSSH server detected. Full SSH transport/auth is not implemented in firmware yet.\r\n"
                            "Connection is closed to avoid unusable encrypted session.\r\n";
                        m_pRenderer->Write(Msg, sizeof Msg - 1);
                    }
                    CloseConnection("ssh-not-implemented");
                    return;
                }

                if (m_pRenderer != nullptr)
                {
                    m_pRenderer->Write(rx, static_cast<size_t>(received));
                }
                ++iteration;
                continue;
            }

            if (received == 0)
            {
                // Circle TCP semantics: MSG_DONTWAIT returns 0 when no data is available.
                break;
            }

            if (received == -NET_ERROR_WOULD_BLOCK)
            {
                break;
            }

            if (received == -NET_ERROR_CONNECTION_RESET)
            {
                CloseConnection("remote-closed");
                break;
            }

            LOGNOTE("Shell-client receive failed rc=%d", received);
            CloseConnection("receive-error");
            break;
        }
    }
}

void CTWlanHost::RequestConnectIfReady()
{
    if (m_InputActive || m_Connected || m_ConnectInProgress)
    {
        return;
    }

    if (m_RetryBackoff > 0U)
    {
        --m_RetryBackoff;
        return;
    }

    if (m_pNet == nullptr || !m_pNet->IsRunning())
    {
        m_RetryBackoff = 50U;
        return;
    }

    CIPAddress ip;
    u16 port = DefaultRawPort;
    if (!ParseHostAndPort(m_HostToken.c_str(), ip, port))
    {
        if (m_pRenderer != nullptr)
        {
            static const char Msg[] = "\r\nShell-client requires numeric IPv4 host (e.g. 192.168.2.10[:2323]).\r\n";
            WriteRendererMessage(m_pRenderer, Msg, sizeof Msg - 1);
            RenderPrompt();
        }
        m_RetryBackoff = 200U;
        return;
    }

    {
        CString target;
        ip.Format(&target);
        LOGNOTE("Shell-client request queued for %s:%u", target.c_str(), port);
    }

    m_StateLock.Acquire();
    m_RemoteIp = ip;
    m_RemotePort = port;
    m_ConnectRequested = true;
    m_ConnectInProgress = true;
    m_StateLock.Release();
}

bool CTWlanHost::SendHostData(const char *pData, size_t nLength)
{
    if (!m_Enabled || pData == nullptr || nLength == 0)
    {
        return false;
    }

    m_StateLock.Acquire();
    const bool connected = m_Connected;
    CSocket *sock = m_pSocket;
    m_StateLock.Release();

    if (!connected || sock == nullptr)
    {
        return true; // swallow in shell-client mode
    }

    const int sent = sock->Send(pData, static_cast<unsigned>(nLength), MSG_DONTWAIT);
    if (sent != static_cast<int>(nLength))
    {
        CloseConnection("send-error");
    }

    return true;
}

void CTWlanHost::CloseConnection(const char *reason)
{
    m_StateLock.Acquire();
    if (m_pSocket != nullptr)
    {
        delete m_pSocket;
        m_pSocket = nullptr;
    }
    const bool wasConnected = m_Connected;
    m_Connected = false;
    m_ConnectInProgress = false;
    m_ConnectRequested = false;
    m_RetryBackoff = 200U;
    m_StateLock.Release();

    const char *logReason = (reason != nullptr) ? reason : "unspecified";
    LOGNOTE("Shell-client connection closing (%s)", logReason);

    if (wasConnected && m_pRenderer != nullptr)
    {
        CString msg;
        if (reason != nullptr)
        {
            msg.Format("\r\nShell-client connection closed (%s).\r\n", reason);
        }
        else
        {
            msg = "\r\nShell-client connection closed.\r\n";
        }
        m_pRenderer->Write(msg.c_str(), msg.GetLength());
    }
}

bool CTWlanHost::ParseIPv4Address(const char *text, CIPAddress &outIp)
{
    if (text == nullptr || text[0] == '\0')
    {
        return false;
    }

    unsigned long octets[4] = {0, 0, 0, 0};
    const char *cursor = text;

    for (unsigned i = 0; i < 4; ++i)
    {
        if (*cursor == '\0')
        {
            return false;
        }

        char *endPtr = nullptr;
        const unsigned long value = strtoul(cursor, &endPtr, 10);
        if (endPtr == cursor || value > 255UL)
        {
            return false;
        }

        octets[i] = value;

        if (i < 3)
        {
            if (*endPtr != '.')
            {
                return false;
            }
            cursor = endPtr + 1;
        }
        else
        {
            if (*endPtr != '\0')
            {
                return false;
            }
        }
    }

    const u32 ip = (static_cast<u32>(octets[3]) << 24)
                 | (static_cast<u32>(octets[2]) << 16)
                 | (static_cast<u32>(octets[1]) << 8)
                 | static_cast<u32>(octets[0]);
    outIp.Set(ip);
    return true;
}

bool CTWlanHost::ParseHostAndPort(const char *hostToken, CIPAddress &outIp, u16 &outPort)
{
    if (hostToken == nullptr || hostToken[0] == '\0')
    {
        return false;
    }

    CString token = hostToken;
    u16 port = outPort;

    const char *text = token.c_str();
    const char *colon = strchr(text, ':');
    if (colon != nullptr)
    {
        CString hostOnly;
        for (const char *it = text; it < colon; ++it)
        {
            hostOnly.Append(*it);
        }
        token = hostOnly;

        const char *portText = colon + 1;
        if (*portText != '\0')
        {
            const unsigned long portValue = strtoul(portText, nullptr, 10);
            if (portValue > 0UL && portValue <= 65535UL)
            {
                port = static_cast<u16>(portValue);
            }
        }
    }

    if (!ParseIPv4Address(token.c_str(), outIp))
    {
        return false;
    }

    outPort = port;
    return true;
}

void CTWlanHost::Run()
{
    // This task isolates blocking socket connect operations.
    // The kernel loop stays responsive and continues calling HAL.Update().
    while (true)
    {
        if (!m_Enabled)
        {
            CScheduler::Get()->MsSleep(50);
            continue;
        }

        if (!m_NetworkingReady)
        {
            // Bring-up WLAN + net + supplicant once while enabled.
            if (m_pWlan != nullptr && m_pNet != nullptr && m_pSupplicant != nullptr)
            {
                if (!m_pWlan->Initialize())
                {
                    if (m_pLogger)
                        m_pLogger->Write(FromTerminal, LogError, "Shell client: WLAN firmware load failed");
                }
                else if (!m_pNet->Initialize(FALSE))
                {
                    if (m_pLogger)
                        m_pLogger->Write(FromTerminal, LogError, "Shell client: network stack initialization failed");
                }
                else if (!m_pSupplicant->Initialize())
                {
                    if (m_pLogger)
                        m_pLogger->Write(FromTerminal, LogError, "Shell client: WPA supplicant initialization failed");
                }
                else
                {
                    m_NetworkingReady = true;
                    if (m_pLogger)
                        m_pLogger->Write(FromTerminal, LogNotice, "Shell client: WLAN + net stack ready");
                    LOGNOTE("Networking ready, prompt pending=%u", m_PromptPending ? 1U : 0U);
                }
            }

            CScheduler::Get()->MsSleep(50);
            continue;
        }

        DisplayPromptIfReady();

        // Consume connect requests.
        m_StateLock.Acquire();
        const bool requested = m_ConnectRequested;
        u16 port = m_RemotePort;
        CIPAddress ip;
        if (requested)
        {
            ip = m_RemoteIp;
            m_ConnectRequested = false;
        }
        m_StateLock.Release();

        if (!requested)
        {
            CScheduler::Get()->MsSleep(20);
            continue;
        }

        if (m_pHal != nullptr)
        {
            m_pHal->StopBuzzer();
        }

        // Replace any prior socket.
        m_StateLock.Acquire();
        if (m_pSocket != nullptr)
        {
            delete m_pSocket;
            m_pSocket = nullptr;
        }
        m_StateLock.Release();

        CSocket *sock = new CSocket(m_pNet, IPPROTO_TCP);
        if (sock == nullptr)
        {
            CloseConnection("alloc-failed");
            continue;
        }

        CString ipString;
        ip.Format(&ipString);
        LOGNOTE("Shell-client connecting to %s:%u", ipString.c_str(), port);

        // Blocking connect (Circle waits on TCP state transition event).
        const int rc = sock->Connect(ip, port);
        if (rc < 0)
        {
            delete sock;
            m_StateLock.Acquire();
            m_ConnectInProgress = false;
            m_StateLock.Release();
            m_RetryBackoff = 200U;
            LOGNOTE("Shell-client connect failed (%s:%u) rc=%d", ipString.c_str(), port, rc);
            continue;
        }

        m_StateLock.Acquire();
        m_pSocket = sock;
        m_Connected = true;
        m_ConnectInProgress = false;
        m_SshDetected = false;
        m_StateLock.Release();

        LOGNOTE("Shell-client connected to %s:%u", ipString.c_str(), port);

        if (m_pRenderer != nullptr)
        {
            CString msg;
            CString ipString;
            ip.Format(&ipString);
            msg.Format("\r\nShell-client connected to %s:%u\r\n",
                       (const char *)ipString, port);
            m_pRenderer->Write(msg.c_str(), msg.GetLength());
        }
    }
}
