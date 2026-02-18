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

#pragma once

// Include Circle core components
#include <circle/sched/task.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/devicenameservice.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <circle/timer.h>

/**
 * @file TKeyboard.h
 * @brief Declares the USB keyboard task and input processing helpers.
 * @details CTKeyboard wraps Circle's USB HID stack to deliver debounced key
 * sequences, auto-repeat handling, and LED synchronization for the VT100
 * terminal. The task acts as the central place where raw USB events are
 * translated into ANSI strings consumed by the renderer and kernel.
 */


// Forward declarations
class CKernel;


/**
 * @class CTKeyboard
 * @brief Cooperative task managing keyboard devices and event translation.
 * @details The keyboard task detects hot-plug events, surfaces processed key
 * strings via callbacks, and maintains raw status notifications for consumers
 * that need modifier state. It also implements configurable auto-repeat and LED
 * updates to emulate the VT100 hardware experience.
 */
class CTKeyboard : public CTask
{
public:
    using TKeyPressedHandler = void (*)(const char *pString);
    using TKeyStatusHandlerRaw = void (*)(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    /// \brief Access the singleton keyboard task instance.
    static CTKeyboard *Get(void);

    /// \brief Construct the singleton keyboard task; dependencies are hooked via Configure().
    CTKeyboard(void);
    /// \brief Configure callbacks and optional host controller.
    /// \param pKeyPressedHandler Callback invoked for processed key strings.
    /// \param pKeyStatusHandlerRaw Optional callback receiving raw key status updates.
    /// \param pUSBHost Optional USB host controller used for plug-and-play updates.
    /// \param keyRepeatDelayMs Auto-repeat activation delay in milliseconds.
    /// \param keyRepeatRateCps Auto-repeat rate in characters per second.
    void Configure(TKeyPressedHandler pKeyPressedHandler,
                   TKeyStatusHandlerRaw pKeyStatusHandlerRaw = nullptr,
                   CUSBHCIDevice *pUSBHost = nullptr,
                   unsigned keyRepeatDelayMs = KeyRepeatDelayDefaultMs,
                   unsigned keyRepeatRateCps = KeyRepeatRateDefaultCps);
    /// \brief Cleanup the keyboard task on shutdown.
    ~CTKeyboard(void);

    /// \brief Initialize keyboard devices and start the task.
    /// \return TRUE if initialization succeeded, FALSE otherwise.
    boolean Initialize(void);

    /// \brief Check for and handle keyboard plug and play events.
    /// \param bDevicesUpdated TRUE if USB devices were updated.
    /// \return TRUE if a keyboard is connected after processing.
    boolean UpdateKeyboard(boolean bDevicesUpdated);

    /// \brief Update keyboard LED state (must be called from main loop).
    void UpdateLEDs(void);

    /// \brief Check if keyboard is connected.
    /// \return TRUE if a keyboard device is available.
    boolean IsKeyboardConnected(void) const;

    /// \brief Notify keyboard that configuration changed.
    void OnConfigUpdated();

    /// \brief Periodic update hook called from the scheduler loop.
    void Update();

    /// \brief Entry point of the keyboard task.
    void Run(void) override;

    /// \brief Replace the key pressed handler.
    void SetKeyPressedHandler(TKeyPressedHandler handler);
    /// \brief Replace the raw key status handler.
    void SetKeyStatusHandlerRaw(TKeyStatusHandlerRaw handler);
    /// \brief Get current key pressed handler.
    TKeyPressedHandler GetKeyPressedHandler() const;
    /// \brief Get current raw key status handler.
    TKeyStatusHandlerRaw GetKeyStatusHandlerRaw() const;

private:
    // Static callback handlers
    /// \brief Invoked when the keyboard device is unplugged.
    static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);
    /// \brief Trampoline forwarding key strings to the instance handler.
    static void KeyPressedTrampoline(const char *pString);
    /// \brief Trampoline forwarding raw key data to the instance handler.
    static void KeyStatusTrampoline(unsigned char ucModifiers, const unsigned char RawKeys[6]);

    // Internal methods
    /// \brief Handle state clean-up when the keyboard disconnects.
    void OnKeyboardRemoved(void);
    /// \brief Dispatch a processed key string to the registered consumer.
    /// \param pString Processed key string.
    /// \param fromAutoRepeat TRUE if invoked from auto-repeat.
    void HandleKeyPressed(const char *pString, boolean fromAutoRepeat);
    /// \brief Process raw key matrix data and track modifier state.
    /// \param ucModifiers Modifier bitmask.
    /// \param RawKeys Raw key matrix values.
    void HandleRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    /// \brief Maintain auto-repeat scheduling while the task runs.
    void ServiceAutoRepeat();
    /// \brief Cancel any pending auto-repeat state.
    void StopAutoRepeat();
    /// \brief Attempt to activate auto-repeat for the latest key.
    void TryActivateAutoRepeat();
    /// \brief Store a key sequence for future auto-repeat emission.
    /// \param pString Key sequence to queue.
    void QueueAutoRepeat(const char *pString);
    /// \brief Decide if a key string qualifies for auto-repeat.
    /// \param pString Key sequence to examine.
    /// \return TRUE if the key sequence should auto-repeat.
    boolean ShouldQueueAutoRepeat(const char *pString) const;

private:
    static constexpr unsigned KeyRepeatDelayDefaultMs = 500;
    static constexpr unsigned KeyRepeatRateDefaultCps = 20;
    CUSBKeyboardDevice *volatile m_pKeyboardDevice{nullptr};
    CUSBHCIDevice *m_pUSBHost;

    static constexpr size_t AutoRepeatMaxSequence = 8;
    struct AutoRepeatState
    {
        boolean active;
        boolean pendingStart;
        unsigned char rawKeyCode;
        char sequence[AutoRepeatMaxSequence + 1];
        unsigned sequenceLength;
        u64 pressStartUs;
        u64 nextRepeatUs;
        u64 delayUs;
        u64 intervalUs;
    } m_AutoRepeat;
    unsigned char m_PreviousRawKeys[6];
    unsigned char m_PendingAutoRepeatRawKey;
    unsigned m_KeyRepeatDelayMs;
    unsigned m_KeyRepeatRateCps;

    TKeyPressedHandler m_pKeyPressedHandler;
    TKeyStatusHandlerRaw m_pKeyStatusHandlerRaw;
};
