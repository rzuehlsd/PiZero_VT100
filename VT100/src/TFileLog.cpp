//------------------------------------------------------------------------------
// Module:        CTFileLog
// Description:   Mirrors Circle log output into an SD-card backed log file.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-24
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-24     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#include "TFileLog.h"

#include <circle/logger.h>
#include <circle/util.h>
#include <fatfs/ff.h>

static const char FromFileLog[] = "FileLog";

CTFileLog *CTFileLog::s_pInstance = nullptr;

CTFileLog *CTFileLog::Get()
{
    if (s_pInstance == nullptr)
    {
        s_pInstance = new CTFileLog();
    }
    return s_pInstance;
}

CTFileLog::CTFileLog()
    : CDevice()
    , m_pLogger(nullptr)
    , m_pFallback(nullptr)
    , m_FileOpen(false)
    , m_Initialized(false)
    , m_Active(false)
    , m_PendingFlushBytes(0)
    , m_PendingFlushLines(0)
{
}

CTFileLog::~CTFileLog()
{
    Stop();
    CloseFile();
}

bool CTFileLog::Initialize(CLogger &logger, const char *fileName, CDevice *fallbackTarget)
{
    m_pLogger = &logger;
    m_pFallback = fallbackTarget;
    m_FilePath = "SD:/";
    m_Initialized = false;
    m_Active = false;

    if (fileName != nullptr && *fileName != '\0')
    {
        m_FilePath.Append(fileName);
    }
    else
    {
        m_FilePath.Append("VT100.log");
    }

    CloseFile();

    if (!OpenFile((const char *)m_FilePath))
    {
        return false;
    }

    WriteHeader();
    m_Initialized = true;
    return true;
}

void CTFileLog::SetFallback(CDevice *fallbackTarget)
{
    m_pFallback = fallbackTarget;
}

bool CTFileLog::Start()
{
    if (!m_Initialized || m_pLogger == nullptr)
    {
        return false;
    }

    if (!m_Active)
    {
        m_pLogger->SetNewTarget(this);
        m_Active = true;
    }
    return true;
}

void CTFileLog::Stop()
{
    if (!m_Active || m_pLogger == nullptr)
    {
        return;
    }

    Flush();

    if (m_pFallback != nullptr)
    {
        m_pLogger->SetNewTarget(m_pFallback);
    }
    m_Active = false;
}

int CTFileLog::Write(const void *buffer, size_t count)
{
    if (buffer == nullptr || count == 0)
    {
        return 0;
    }

    if (m_FileOpen)
    {
        UINT written = 0;
        FRESULT result = f_write(&m_File, buffer, static_cast<UINT>(count), &written);
        if (result == FR_OK)
        {
            m_PendingFlushBytes += written;

            const char *ch = static_cast<const char *>(buffer);
            for (UINT i = 0; i < written; ++i)
            {
                if (ch[i] == '\n')
                {
                    ++m_PendingFlushLines;
                }
            }

            if (   m_PendingFlushBytes >= FlushByteThreshold
                || m_PendingFlushLines >= FlushLineThreshold)
            {
                Flush();
            }
        }
        else
        {
            m_FileOpen = false;
        }
    }

    if (m_pFallback != nullptr)
    {
        m_pFallback->Write(buffer, count);
    }

    return static_cast<int>(count);
}

bool CTFileLog::OpenFile(const char *fileName)
{
    if (fileName == nullptr)
    {
        return false;
    }

    FRESULT res = f_open(&m_File, fileName, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK)
    {
        return false;
    }

    m_FileOpen = true;
    return true;
}

void CTFileLog::CloseFile()
{
    if (m_FileOpen)
    {
        Flush();
        f_close(&m_File);
        m_FileOpen = false;
    }
}

void CTFileLog::WriteHeader()
{
    if (!m_FileOpen)
    {
        return;
    }

    static const char HeaderPrefix[] = "[INFO] VT100 Terminal Emulator Log Started\r\n";

    UINT written = 0;
    f_write(&m_File, HeaderPrefix, sizeof(HeaderPrefix) - 1, &written);

    CString compileLine;
    compileLine.Format("[INFO] Compiled: %s %s\r\n", __DATE__, __TIME__);
    f_write(&m_File, (const char *)compileLine, compileLine.GetLength(), &written);

    static const char Divider[] = "[INFO] ================================\r\n";
    f_write(&m_File, Divider, sizeof(Divider) - 1, &written);
    f_sync(&m_File);
    m_PendingFlushBytes = 0;
    m_PendingFlushLines = 0;
}

void CTFileLog::Flush()
{
    if (!m_FileOpen)
    {
        return;
    }

    if (m_PendingFlushBytes == 0)
    {
        return;
    }

    f_sync(&m_File);
    m_PendingFlushBytes = 0;
    m_PendingFlushLines = 0;
}
