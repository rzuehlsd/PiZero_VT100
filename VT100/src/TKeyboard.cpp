//------------------------------------------------------------------------------
// Module:        CTKeyboard
// Description:   Handles USB keyboard input and manages keyboard state.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-21
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-21     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

// Include class header
#include "TKeyboard.h"

// Full class definitions for classes used in this module
// Include Circle core components
#include "kernel.h"
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/sched/scheduler.h>
#include <circle/spinlock.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <assert.h>
#include <string.h>
#include "hal.h"
#include "TConfig.h"


LOGMODULE("TKeyboard");

// Singleton instance creation and access
// teardown handled by runtime
// CAUTION: Only possible if constructor does not need parameters
static CTKeyboard *s_pThis = nullptr;
CTKeyboard *CTKeyboard::Get(void)
{
    if(s_pThis == nullptr)
    {
        s_pThis = new CTKeyboard();
    }
    return s_pThis;
}


namespace
{
const char *ApplyConfiguredLineEndings(const char *input, CString &scratch)
{
	// Converts newline characters based on the configured line ending mode.
	// Mode 0: pass-through, Mode 1: ensure CRLF, Mode 2: convert LF to CR.
	CTConfig *config = CTConfig::Get();
	unsigned int mode = (config != nullptr) ? config->GetLineEndingMode() : 0U;
	if (input == nullptr || mode == 0U)
	{
		return input;
	}

	// Single pass conversion with a flag to avoid allocating a new string
	// when no changes are required.
	bool converted = false;
	scratch = "";
	for (const char *p = input; *p != '\0'; ++p)
	{
		char ch = *p;
		if (ch == '\r')
		{
			if (mode == 1U)
			{
				// CRLF mode: keep an existing CRLF pair as-is, otherwise append LF.
				scratch.Append('\r');
				if (*(p + 1) == '\n')
				{
					scratch.Append('\n');
					++p;
				}
				else
				{
					scratch.Append('\n');
					converted = true;
				}
			}
			else
			{
				// CR-only mode: keep carriage returns.
				scratch.Append('\r');
			}
		}
		else if (ch == '\n')
		{
			// If CRLF is requested, only add CR when it is not already present.
			if (mode == 1U)
			{
				if (p == input || *(p - 1) != '\r')
				{
					scratch.Append('\r');
					converted = true;
				}
				scratch.Append('\n');
			}
			else if (mode == 2U)
			{
				// CR-only mode: drop LF and insert CR.
				scratch.Append('\r');
				converted = true;
			}
			else
			{
				scratch.Append('\n');
			}
		}
		else
		{
			scratch.Append(ch);
		}
	}

	return converted ? scratch.c_str() : input;
}
}

CTKeyboard::CTKeyboard(void)
	: CTask(),
	  m_pUSBHost(nullptr),
	  m_AutoRepeat{},
	  m_PreviousRawKeys{0},
	  m_PendingAutoRepeatRawKey(0),
	  m_KeyRepeatDelayMs(KeyRepeatDelayDefaultMs),
	  m_KeyRepeatRateCps(KeyRepeatRateDefaultCps),
	  m_pKeyPressedHandler(nullptr),
	  m_pKeyStatusHandlerRaw(nullptr)
{
	SetName("Keyboard");
	Suspend();

	m_AutoRepeat.active = FALSE;
	m_AutoRepeat.pendingStart = FALSE;
	m_AutoRepeat.rawKeyCode = 0;
	m_AutoRepeat.sequence[0] = '\0';
	m_AutoRepeat.sequenceLength = 0;
	m_AutoRepeat.pressStartUs = 0;
	m_AutoRepeat.nextRepeatUs = 0;
	m_AutoRepeat.delayUs = 0;
	m_AutoRepeat.intervalUs = 0;

	LOGNOTE("CTKeyboard constructed - task suspended");
}

void CTKeyboard::Configure(TKeyPressedHandler pKeyPressedHandler,
				   TKeyStatusHandlerRaw pKeyStatusHandlerRaw,
				   CUSBHCIDevice *pUSBHost,
				   unsigned keyRepeatDelayMs,
				   unsigned keyRepeatRateCps)
{
	m_pKeyPressedHandler = pKeyPressedHandler;
	m_pKeyStatusHandlerRaw = pKeyStatusHandlerRaw;
	m_pUSBHost = pUSBHost;
	m_KeyRepeatDelayMs = keyRepeatDelayMs == 0U ? KeyRepeatDelayDefaultMs : keyRepeatDelayMs;
	m_KeyRepeatRateCps = keyRepeatRateCps == 0U ? KeyRepeatRateDefaultCps : keyRepeatRateCps;
}

void CTKeyboard::SetKeyPressedHandler(TKeyPressedHandler handler)
{
	m_pKeyPressedHandler = handler;
}

void CTKeyboard::SetKeyStatusHandlerRaw(TKeyStatusHandlerRaw handler)
{
	m_pKeyStatusHandlerRaw = handler;
}

CTKeyboard::TKeyPressedHandler CTKeyboard::GetKeyPressedHandler() const
{
	return m_pKeyPressedHandler;
}

CTKeyboard::TKeyStatusHandlerRaw CTKeyboard::GetKeyStatusHandlerRaw() const
{
	return m_pKeyStatusHandlerRaw;
}

CTKeyboard::~CTKeyboard (void)
{
		StopAutoRepeat();
}

boolean CTKeyboard::Initialize (void)
{
    boolean bOK = TRUE;

	if (m_pUSBHost != nullptr)
	{
		m_pUSBHost->UpdatePlugAndPlay();
	}
	if (m_pKeyboardDevice == nullptr)
	{
		bOK = UpdateKeyboard (TRUE);
	}
    Start();
	LOGNOTE("Keyboard subsystem initialized");
	return bOK;
}

bool CTKeyboard::UpdateKeyboard (boolean bDevicesUpdated)
{

	boolean connected = (m_pKeyboardDevice != nullptr);

	if (bDevicesUpdated && !connected)
	{
		m_pKeyboardDevice = (CUSBKeyboardDevice *) CDeviceNameService::Get()->GetDevice ("ukbd1", FALSE);
		if (m_pKeyboardDevice != nullptr)
		{
			m_pKeyboardDevice->RegisterRemovedHandler (KeyboardRemovedHandler);
			m_pKeyboardDevice->RegisterKeyPressedHandler (KeyPressedTrampoline);
			m_pKeyboardDevice->RegisterKeyStatusHandlerRaw (KeyStatusTrampoline, TRUE);
			LOGNOTE("Keyboard connected - Just type something!");
			connected = TRUE;
		}
	}

	if (connected)
	{
		ServiceAutoRepeat();
	}
	else
	{
		StopAutoRepeat();
	}

	return connected;
}


void CTKeyboard::Run()
{
    while (!IsSuspended())
    {
		boolean devicesUpdated = FALSE;
		if (m_pUSBHost != nullptr)
		{
			devicesUpdated = m_pUSBHost->UpdatePlugAndPlay();
		}

		UpdateKeyboard(devicesUpdated);
        UpdateLEDs();

        CScheduler::Get()->MsSleep(20);
    }
}

void CTKeyboard::UpdateLEDs (void)
{
	if (m_pKeyboardDevice != 0)
	{
		// CUSBKeyboardDevice::UpdateLEDs() must not be called in interrupt context,
		// that's why this must be done here. This does nothing in raw mode.
		m_pKeyboardDevice->UpdateLEDs ();
	}
}

boolean CTKeyboard::IsKeyboardConnected (void) const
{
	return m_pKeyboardDevice != 0;
}


void CTKeyboard::OnConfigUpdated()
{
	StopAutoRepeat();
}



void CTKeyboard::KeyboardRemovedHandler (CDevice *pDevice, void *pContext)
{
	CTKeyboard::Get()->OnKeyboardRemoved ();
}



void CTKeyboard::OnKeyboardRemoved (void)
{
	LOGNOTE("Keyboard removed");
	m_pKeyboardDevice = 0;
	StopAutoRepeat();
	memset(m_PreviousRawKeys, 0, sizeof(m_PreviousRawKeys));
	m_PendingAutoRepeatRawKey = 0;
}

void CTKeyboard::KeyPressedTrampoline(const char *pString)
{
	CTKeyboard::Get()->HandleKeyPressed(pString, FALSE);
}

void CTKeyboard::KeyStatusTrampoline(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	CTKeyboard::Get()->HandleRawKeyStatus(ucModifiers, RawKeys);
}

void CTKeyboard::HandleKeyPressed(const char *pString, boolean fromAutoRepeat)
{
	if (pString == 0)
	{
		if (!fromAutoRepeat)
		{
			StopAutoRepeat();
		}
		if (m_pKeyPressedHandler != 0)
		{
			m_pKeyPressedHandler(pString);
		}
        
		return;
	}

	if (!fromAutoRepeat)
	{
		if (m_AutoRepeat.active || m_AutoRepeat.pendingStart)
		{
			StopAutoRepeat();
		}
	}

	boolean queueRepeat = FALSE;
	if (!fromAutoRepeat)
	{
		queueRepeat = ShouldQueueAutoRepeat(pString);
	}

	CString convertedLine;
	const char *lineToSend = ApplyConfiguredLineEndings(pString, convertedLine);
	if (lineToSend == nullptr)
	{
		lineToSend = pString;
	}

	if (m_pKeyPressedHandler != 0)
	{
        if(CTConfig::Get()->GetKeyClick() == 1)
        {
            CHAL::Get()->Click();
        }
		m_pKeyPressedHandler(lineToSend);
	}
	// No handler configured

	if (!fromAutoRepeat && queueRepeat)
	{
		QueueAutoRepeat(pString);
	}
}

boolean CTKeyboard::ShouldQueueAutoRepeat(const char *pString) const
{
	CTConfig *config = CTConfig::Get();
	if (config != nullptr && !config->GetKeyAutoRepeatEnabled())
	{
		return FALSE;
	}

	if (pString == 0)
	{
		return FALSE;
	}

	size_t length = strlen(pString);
	if (length == 0 || length > AutoRepeatMaxSequence)
	{
		return FALSE;
	}

	if (length == 1)
	{
		unsigned char ch = static_cast<unsigned char>(pString[0]);
		if (ch == '\n' || ch == '\r' || ch == '\b' || ch == 0x7F)
		{
			return TRUE;
		}
		return ch >= 0x20 && ch < 0x7F;
	}

	if (length == 3 && pString[0] == '\x1B' && pString[1] == '[')
	{
		char third = pString[2];
		if (third == 'A' || third == 'B' || third == 'C' || third == 'D')
		{
			return TRUE; // Arrow keys
		}
		return FALSE; // Do not repeat other short CSI sequences (e.g. Home/End)
	}

	if (length == 4 && pString[0] == '\x1B' && pString[1] == '[' && pString[2] == '3' && pString[3] == '~')
	{
		return TRUE; // Delete key
	}

	return FALSE;
}

void CTKeyboard::QueueAutoRepeat(const char *pString)
{
	if (pString == 0)
	{
		return;
	}

	size_t length = strlen(pString);
	if (length == 0 || length > AutoRepeatMaxSequence)
	{
		return;
	}

	for (size_t i = 0; i < length; ++i)
	{
		m_AutoRepeat.sequence[i] = pString[i];
	}
	m_AutoRepeat.sequence[length] = '\0';
	m_AutoRepeat.sequenceLength = static_cast<unsigned>(length);
	m_AutoRepeat.pendingStart = TRUE;
	m_AutoRepeat.active = FALSE;
	m_AutoRepeat.rawKeyCode = 0;
	m_AutoRepeat.pressStartUs = CTimer::GetClockTicks64();

	
	unsigned int delayMs = (m_KeyRepeatDelayMs == 0U) ? KeyRepeatDelayDefaultMs : m_KeyRepeatDelayMs;
	m_AutoRepeat.delayUs = static_cast<u64>(delayMs) * 1000ULL;

	unsigned int rateCps = (m_KeyRepeatRateCps == 0U) ? KeyRepeatRateDefaultCps : m_KeyRepeatRateCps;
	u64 intervalUs = (rateCps > 0U) ? (1000000ULL / static_cast<u64>(rateCps)) : 0ULL;
	if (intervalUs == 0U)
	{
		intervalUs = 1000000ULL / static_cast<u64>(KeyRepeatRateDefaultCps);
	}
	m_AutoRepeat.intervalUs = intervalUs;
	m_AutoRepeat.nextRepeatUs = m_AutoRepeat.pressStartUs + m_AutoRepeat.delayUs;
	TryActivateAutoRepeat();
}

void CTKeyboard::TryActivateAutoRepeat()
{
	if (!m_AutoRepeat.pendingStart)
	{
		return;
	}

	if (m_PendingAutoRepeatRawKey == 0)
	{
		return;
	}

	m_AutoRepeat.rawKeyCode = m_PendingAutoRepeatRawKey;
	m_AutoRepeat.active = TRUE;
	m_AutoRepeat.pendingStart = FALSE;
	u64 now = CTimer::GetClockTicks64();
	if (now > m_AutoRepeat.nextRepeatUs)
	{
		m_AutoRepeat.nextRepeatUs = now;
	}
	m_PendingAutoRepeatRawKey = 0;
}

void CTKeyboard::StopAutoRepeat()
{
	m_AutoRepeat.active = FALSE;
	m_AutoRepeat.pendingStart = FALSE;
	m_AutoRepeat.rawKeyCode = 0;
	m_AutoRepeat.sequence[0] = '\0';
	m_AutoRepeat.sequenceLength = 0;
	m_AutoRepeat.pressStartUs = 0;
	m_AutoRepeat.nextRepeatUs = 0;
	m_AutoRepeat.delayUs = 0;
	m_AutoRepeat.intervalUs = 0;
	m_PendingAutoRepeatRawKey = 0;
}

void CTKeyboard::ServiceAutoRepeat()
{
	if (m_AutoRepeat.pendingStart)
	{
		TryActivateAutoRepeat();
	}

	if (!m_AutoRepeat.active)
	{
		return;
	}

	if (m_AutoRepeat.rawKeyCode == 0)
	{
		StopAutoRepeat();
		return;
	}

	u64 now = CTimer::GetClockTicks64();
	if (now < m_AutoRepeat.nextRepeatUs)
	{
		return;
	}

		if (m_AutoRepeat.intervalUs == 0U)
		{
			StopAutoRepeat();
			return;
		}

		m_AutoRepeat.nextRepeatUs = now + m_AutoRepeat.intervalUs;
	HandleKeyPressed(m_AutoRepeat.sequence, TRUE);
}

void CTKeyboard::HandleRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	if (m_pKeyStatusHandlerRaw != 0)
	{
		m_pKeyStatusHandlerRaw(ucModifiers, RawKeys);
	}

	if (m_AutoRepeat.active && m_AutoRepeat.rawKeyCode != 0)
	{
		boolean stillDown = FALSE;
		for (unsigned i = 0; i < 6; ++i)
		{
			if (RawKeys[i] == m_AutoRepeat.rawKeyCode)
			{
				stillDown = TRUE;
				break;
			}
		}
		if (!stillDown)
		{
			StopAutoRepeat();
		}
	}

	unsigned char newKey = 0;
	for (unsigned i = 0; i < 6; ++i)
	{
		unsigned char code = RawKeys[i];
		if (code == 0)
		{
			continue;
		}

		boolean wasPresent = FALSE;
		for (unsigned j = 0; j < 6; ++j)
		{
			if (m_PreviousRawKeys[j] == code)
			{
				wasPresent = TRUE;
				break;
			}
		}

		if (!wasPresent)
		{
			newKey = code;
			break;
		}
	}

	if (newKey != 0)
	{
		m_PendingAutoRepeatRawKey = newKey;
	}
	else
	{
		boolean allReleased = TRUE;
		for (unsigned i = 0; i < 6; ++i)
		{
			if (RawKeys[i] != 0)
			{
				allReleased = FALSE;
				break;
			}
		}
		if (allReleased)
		{
			m_PendingAutoRepeatRawKey = 0;
			StopAutoRepeat();
		}
	}

	memcpy(m_PreviousRawKeys, RawKeys, sizeof(m_PreviousRawKeys));
	TryActivateAutoRepeat();
}
