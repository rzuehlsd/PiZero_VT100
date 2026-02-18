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

#pragma once

#include <circle/device.h>
#include <circle/string.h>
#include <circle/types.h>
#include <fatfs/ff.h>

/**
 * @file TFileLog.h
 * @brief Declares the SD-card backed logging device.
 * @details CTFileLog mirrors Circle logger output into a rotating file stored on
 * the SD card while optionally forwarding the same data to an alternate device.
 * It hides buffering, periodic flushing, and fallback handling so higher-level
 * code only needs to attach the logger once and choose a file path.
 */

class CLogger;

/**
 * @class CTFileLog
 * @brief Provides file-based persistence for Circle logger messages.
 * @details The singleton integrates with CLogger, writes each message to the
 * mounted filesystem using FatFs, and mirrors output to a fallback CDevice
 * whenever the SD card is missing. It manages buffering thresholds and emits a
 * startup banner to aid post-mortem analysis.
 */
class CTFileLog : public CDevice
{
public:
    /// \brief Access the singleton file log device.
    /// \return Pointer to the file log instance.
    static CTFileLog *Get();

    /// \brief Prepare the log target with logger, file path, and fallback device.
    /// \param logger Logger whose output should be mirrored.
    /// \param fileName Path to the log file on the SD card.
    /// \param fallbackTarget Device used when file logging is unavailable.
    /// \return TRUE on success, FALSE on failure.
    bool Initialize(CLogger &logger, const char *fileName, CDevice *fallbackTarget);
    /// \brief Change the device used when file logging is unavailable.
    /// \param fallbackTarget Device to receive mirrored log output.
    void SetFallback(CDevice *fallbackTarget);
    /// \brief Attach to the logger and begin capturing output.
    /// \return TRUE if logging started, FALSE otherwise.
    bool Start();
    /// \brief Detach from the logger and flush pending output.
    void Stop();

    /// \brief Write a chunk of log data and mirror to the fallback if needed.
    /// \param buffer Pointer to bytes to write.
    /// \param count Number of bytes in buffer.
    /// \return Number of bytes consumed.
    int Write(const void *buffer, size_t count) override;

private:
    /// \brief Construct the file logging device (singleton use only).
    CTFileLog();
    /// \brief Ensure graceful shutdown of file logging.
    ~CTFileLog();

    /// \brief Open or create the backing log file.
    /// \param fileName Path of the file to open.
    /// \return TRUE if the file is open and ready.
    bool OpenFile(const char *fileName);
    /// \brief Close the backing log file safely.
    void CloseFile();
    /// \brief Emit an initial header banner into the log file.
    void WriteHeader();
    /// \brief Flush buffered log data to the SD card.
    void Flush();

private:
    static CTFileLog *s_pInstance;

    CLogger *m_pLogger;
    CDevice *m_pFallback;
    FIL m_File;
    bool m_FileOpen;
    bool m_Initialized;
    bool m_Active;
    CString m_FilePath;
    unsigned m_PendingFlushBytes;
    unsigned m_PendingFlushLines;

    static constexpr unsigned FlushByteThreshold = 1024;
    static constexpr unsigned FlushLineThreshold = 8;
};
