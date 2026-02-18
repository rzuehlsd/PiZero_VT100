#include "profiler.h"

#include <circle/logger.h>
#include <circle/timer.h>
#include <string.h>

LOGMODULE("Profiler");

CProfiler::CProfiler()
: m_Slots{}
, m_SlotCount(0)
{
}

CProfiler &CProfiler::Get()
{
    static CProfiler instance;
    return instance;
}

int CProfiler::RegisterSlot(const char *label)
{
    if (label == nullptr)
    {
        return -1;
    }

    for (unsigned i = 0; i < m_SlotCount; ++i)
    {
        if (strncmp(m_Slots[i].Label, label, LabelLength) == 0)
        {
            return static_cast<int>(i);
        }
    }

    if (m_SlotCount >= MaxSlots)
    {
        return -1;
    }

    Slot &slot = m_Slots[m_SlotCount];
    strncpy(slot.Label, label, LabelLength);
    slot.Label[LabelLength] = '\0';
    slot.Count = 0;
    slot.TotalUs = 0;
    slot.MaxUs = 0;

    return static_cast<int>(m_SlotCount++);
}

void CProfiler::Record(int slotId, u64 durationUs)
{
    if (slotId < 0 || static_cast<unsigned>(slotId) >= m_SlotCount)
    {
        return;
    }

    Slot &slot = m_Slots[slotId];
    slot.TotalUs += durationUs;
    if (durationUs > slot.MaxUs)
    {
        slot.MaxUs = durationUs;
    }
    ++slot.Count;
}

void CProfiler::Reset()
{
    for (unsigned i = 0; i < m_SlotCount; ++i)
    {
        m_Slots[i].Count = 0;
        m_Slots[i].TotalUs = 0;
        m_Slots[i].MaxUs = 0;
    }
}

const CProfiler::Slot *CProfiler::GetSlots(unsigned &outCount) const
{
    outCount = m_SlotCount;
    return m_Slots;
}

void CProfiler::Dump() const
{
    for (unsigned i = 0; i < m_SlotCount; ++i)
    {
        const Slot &slot = m_Slots[i];
        u64 avg = slot.Count ? (slot.TotalUs / slot.Count) : 0;
        LOGNOTE("Profile[%s]: count=%u avg=%lluus max=%lluus total=%lluus",
                slot.Label,
                slot.Count,
                (unsigned long long) avg,
                (unsigned long long) slot.MaxUs,
                (unsigned long long) slot.TotalUs);
    }
}

void CProfiler::PeriodicDump(u64 intervalUs)
{
    static u64 lastDumpUs = 0;

    u64 nowUs = CTimer::GetClockTicks64();
    if (lastDumpUs == 0)
    {
        lastDumpUs = nowUs;
    }

    if (nowUs - lastDumpUs >= intervalUs)
    {
        Dump();
        Reset();
        lastDumpUs = nowUs;
    }
}

CScopeProfiler::CScopeProfiler(int slotId)
: m_SlotId(slotId)
, m_StartUs(0)
{
    if (m_SlotId >= 0)
    {
        m_StartUs = CTimer::GetClockTicks64();
    }
}

CScopeProfiler::~CScopeProfiler()
{
    if (m_SlotId >= 0)
    {
        u64 durationUs = CTimer::GetClockTicks64() - m_StartUs;
        CProfiler::Get().Record(m_SlotId, durationUs);
    }
}
