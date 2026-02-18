#pragma once

#include <circle/types.h>
#include <stddef.h>

// Lightweight scope-based timing profiler for VT100.
// This file is intentionally NOT integrated into the build by default.
//
// Basic usage (after manual integration):
//   PROFILE_SCOPE("RenderLine");
//   ... code to measure ...
//
// Optional periodic dump (e.g., from main loop/task):
//   PROFILE_DUMP(10000000ULL); // every 10 seconds

class CProfiler
{
public:
    static constexpr unsigned MaxSlots = 32;
    static constexpr unsigned LabelLength = 31;

    struct Slot
    {
        char Label[LabelLength + 1];
        unsigned Count;
        u64 TotalUs;
        u64 MaxUs;
    };

    static CProfiler &Get();

    int RegisterSlot(const char *label);
    void Record(int slotId, u64 durationUs);
    void Reset();
    void Dump() const;
    void PeriodicDump(u64 intervalUs);
    const Slot *GetSlots(unsigned &outCount) const;

private:
    CProfiler();

    Slot m_Slots[MaxSlots];
    unsigned m_SlotCount;
};

class CScopeProfiler
{
public:
    explicit CScopeProfiler(int slotId);
    ~CScopeProfiler();

private:
    int m_SlotId;
    u64 m_StartUs;
};

#define PROFILE_SCOPE(label) \
    static int s_profile_slot_##__LINE__ = CProfiler::Get().RegisterSlot(label); \
    CScopeProfiler profile_scope_##__LINE__(s_profile_slot_##__LINE__)

#define PROFILE_DUMP(intervalUs) \
    CProfiler::Get().PeriodicDump(intervalUs)
