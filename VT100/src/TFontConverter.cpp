//------------------------------------------------------------------------------
// Module:        CTFontConverter
// Description:   Loads and caches VT100 font assets for renderer consumption.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-18
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-18     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

// Include class header
#include "TFontConverter.h"

// Include module headers
#include "VT100_FontConverter.h"

// Full class definitions for classes used in this module
// Include Circle core components
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/spinlock.h>



// Include application components



LOGMODULE("TFontConverter");

// Singleton instance creation and access
// teardown handled by runtime
// CAUTION: Only possible if constructor does not need parameters
static CTFontConverter *s_pThis = 0;
CTFontConverter *CTFontConverter::Get(void)
{
    if(s_pThis == 0)
    {
        s_pThis = new CTFontConverter();
    }
    return s_pThis;
}



// Simple constructor   just sets Name for the task
CTFontConverter::CTFontConverter()
    : CTask()
{
    SetName("FontConverter");
    Suspend();
}

// Destructor should be defaulted as special cleanup may be risky in a task context 
CTFontConverter::~CTFontConverter()
{

}

bool CTFontConverter::Initialize()
{
    if (!m_Initialized)
    {
        ConvertVT100Font();
        m_Initialized = true;
    }

    LOGNOTE("FontConverter initialized");
    Start();
    return true;
}

void CTFontConverter::Run()
{
    while (!IsSuspended())
    {
        CScheduler::Get()->MsSleep(100);
    }
}

const TFont &CTFontConverter::GetFont(EFontSelection font)
{
    return GetVT100Font(font);
}
