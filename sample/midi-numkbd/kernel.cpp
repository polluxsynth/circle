//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2016  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "kernel.h"
#include "rawkeys.h"
#include <circle/string.h>

static const char From[] = "kernel";

CKernel *CKernel::s_pThis = 0;
// The user-configurable settings (program/bank numbering, bank select CC,
// MIDI channel) are read from cmdline.txt: see settings.h.

// How many "umidiN" device names to try when looking for a MIDI device
static const unsigned MaxMIDIDevices = 4;

// LED patterns. Each starts with its dark phase, so a change of mode is
// visible at once.
//
// While a number is being typed: 4 Hz, 50% duty (125 ms off, then 125 ms on)
static const unsigned EntryBlinkPeriodMs = 250;

// In bank lock mode: 85% on / 15% off, once a second (150 ms off, 850 ms on)
static const unsigned BankLockPeriodMs = 1000;
static const unsigned BankLockOffMs = 150;

// In the settings mode, waiting for a setting number: two blips off, each
// 100 ms long and 100 ms apart, once a second.
static const unsigned SettingsPeriodMs = 1000;
static const unsigned SettingsBlipMs = 100;

// How long * must be held down to enter the settings mode
static const unsigned StarHoldMs = 2000;

CKernel::CKernel(void): m_Settings(m_Options),
			m_Timer(&m_Interrupt),
			//m_Serial(&m_Interrupt),
			m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
			m_pKeyboard(0),
			m_pMIDI(0),
			m_Logger(m_Options.GetLogLevel(), &m_Timer),
			m_Reboot(false),
			m_PrevKeys{0},
			m_bStarHeld(false),
			m_nLastDropped(0),
			m_Selector(m_Settings.FirstProgramNumber,
				   CProgramSelector::DefaultTimeoutMs,
				   0, m_Settings.FirstBankNumber),
			m_SettingsMode(),
			m_bStarTiming(false),
			m_nStarDownMs(0),
			m_Blink(),
			m_bLEDOn(true),
			m_LEDMode(LEDSteady),
			m_nPatternStartMs(0)
{
	s_pThis = this;
}

CKernel::~CKernel(void)
{
}

boolean CKernel::Initialize(void)
{
	bool bOK = TRUE;
	
	if (bOK)
		bOK = m_Serial.Initialize(115200);
	if (bOK)
		bOK = m_Logger.Initialize(&m_Serial);
	if (bOK)
		m_Settings.Report();
	if (bOK)
		bOK = m_Interrupt.Initialize();
	if (bOK)
		bOK = m_Timer.Initialize();
	if (bOK)
		bOK = m_USBHCI.Initialize();

	m_ActLED.Blink(2);

	return bOK;
}

TShutdownMode CKernel::Run(void)
{
	LOGDBG("MIDI Numkbd starting up\n");
	m_ActLED.On();

	while (!m_Reboot) {
		boolean bUpdated = m_USBHCI.UpdatePlugAndPlay();

		if (bUpdated) {
			if (m_pKeyboard == 0)
				AttachKeyboard();
			if (m_pMIDI == 0)
				AttachMIDI();
		}

		// Drain key events queued by the USB callback. All the slow
		// work (logging, and later MIDI output) happens here.
		TKeyEvent Event;
		while (m_KeyQueue.Pop(Event))
			HandleKey(Event);

		// Commit a half-typed number once the idle timeout expires
		DoAction(m_Selector.Tick(NowMs()));

		// Settings mode: a long press of * gets in, and it times out
		// by itself when left alone.
		CheckStarHold();
		DoSettingsAction(m_SettingsMode.Tick(NowMs()));

		UpdateLED();

		unsigned nDropped = m_KeyQueue.Dropped();
		if (nDropped != m_nLastDropped) {
			LOGWARN("Key queue overflow, %u events dropped in total", nDropped);
			m_nLastDropped = nDropped;
		}

		m_Timer.MsDelay(1);
	}
	LOGDBG("Rebooting\n");
	return ShutdownReboot;
}

void CKernel::AttachKeyboard(void)
{
	LOGDBG("PnP updated, looking for keyboard");

	CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *)
		m_DeviceNameService.GetDevice("ukbd1", FALSE);
	if (pKeyboard == 0)
		return;

	LOGDBG("Numpad connected");

	// Reset the report history before registering the callback, 
	// to avoid race with the callback.
	for (int i = 0; i < 6; i++)
		m_PrevKeys[i] = 0;
	m_bStarHeld = false;
	m_bStarTiming = false;

	pKeyboard->RegisterRemovedHandler(DeviceRemovedHandler, this);
	pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, this);
	pKeyboard->SetLEDs(LED_NUM_LOCK);
	m_bLEDOn = true;
	m_LEDMode = LEDSteady;

	m_pKeyboard = pKeyboard;
}

// Look for a USB MIDI device. Circle registers each one as "umidi1",
// "umidi2", ... (numbers are recycled), and we use the first one present.
// Only output is needed, so no packet handler is registered: any incoming
// MIDI data is read by the driver and discarded.
void CKernel::AttachMIDI(void)
{
	for (unsigned i = 1; i <= MaxMIDIDevices; i++) {
		CString Name;
		Name.Format("umidi%u", i);

		CUSBMIDIDevice *pMIDI = (CUSBMIDIDevice *)
			m_DeviceNameService.GetDevice(Name, FALSE);
		if (pMIDI == 0)
			continue;

		LOGNOTE("MIDI device %s connected", (const char *) Name);

		pMIDI->RegisterRemovedHandler(MIDIRemovedHandler, this);
		m_pMIDI = pMIDI;
		return;
	}
}

// Called from the USB plug-and-play code when the MIDI device goes away,
// i.e. from within UpdatePlugAndPlay() in Run(). That is the same context
// that sends MIDI, so SendProgramChange() cannot be using the pointer
// while it is cleared.
void CKernel::MIDIRemovedHandler(CDevice *pDevice, void *pContext)
{
	CKernel *pThis = static_cast<CKernel *>(pContext);

	assert(pThis != 0);
	pThis->m_pMIDI = 0;
	LOGNOTE("MIDI device removed");
}

void CKernel::DeviceRemovedHandler(CDevice *pDevice, void *pContext)
{
	CKernel *pThis = static_cast<CKernel *>(pContext);

	assert(pThis != 0);
	pThis->m_pKeyboard = 0;

	// A key held down when the numpad is unplugged is never released
	pThis->m_bStarHeld = false;
	pThis->m_bStarTiming = false;
	LOGDBG("Numpad removed");
}

// May be called in interrupt context. Only compare, enqueue and return.
void CKernel::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6], void *pContext)
{
	CKernel *pThis = static_cast<CKernel *>(pContext);

	assert(pThis != 0);

	// Raw mode reports the set of keys currently held, not press events,
	// so compare against the previous report to find new presses.

	// Rollover/error report (usage 1): ignore it entirely and keep the
	// previous state, so keys aren't reported as released and re-pressed.
	for (unsigned i = 0; i < 6; i++)
		if (RawKeys[i] == 1)
			return;

	// Is * down? The main loop times a long press of it. Set before the
	// press event is queued below, so the main loop never sees the event
	// without the flag.
	bool bStar = false;
	for (unsigned i = 0; i < 6; i++)
		if (RawKeys[i] == KEYPAD_STAR)
			bStar = true;
	pThis->m_bStarHeld = bStar;

	for (unsigned i = 0; i < 6; i++)
	{
		unsigned char key = RawKeys[i];
		if (key < 4) continue; // 0 = none, 2..3 = error codes

		bool bNew = true;
		for (unsigned j = 0; j < 6; j++)
			if (pThis->m_PrevKeys[j] == key) bNew = false;

		if (bNew)
			pThis->m_KeyQueue.Push(TKeyEvent{key, ucModifiers});
	}

	for (unsigned i = 0; i < 6; i++)
		pThis->m_PrevKeys[i] = RawKeys[i];
}

// Main-loop context: safe to log, block briefly, send MIDI, etc.
void CKernel::HandleKey(const TKeyEvent &Event)
{
	LOGDBG("Key down: Modifier 0x%02X Code 0x%02X",
		(unsigned) Event.ucModifiers, (unsigned) Event.ucKey);

	if (Event.ucKey == KEYPAD_TAB) {
		m_Reboot = true;
		return;
	}

	// In the settings mode the keys are the mode's, not the selector's
	if (m_SettingsMode.Active()) {
		m_Blink.Clear();	// any key cuts short a sequence still playing
		DoSettingsAction(m_SettingsMode.KeyPressed(Event.ucKey, NowMs()));
		return;
	}

	// * does nothing by itself; held for StarHoldMs it enters the
	// settings mode (see CheckStarHold).
	if (Event.ucKey == KEYPAD_STAR) {
		m_nStarDownMs = NowMs();
		m_bStarTiming = true;
		return;
	}

	bool bBankLock = m_Selector.BankLock();
	bool bBankPending = m_Selector.BankSelectPending();

	CProgramSelector::TAction Action = m_Selector.KeyPressed(Event.ucKey, NowMs());
	DoAction(Action);
	if (Action.IsNone() && m_Selector.EntryDigits() > 0)
		LOGDBG("Entry so far: %d", m_Selector.EntryNumber());

	if (m_Selector.BankLock() != bBankLock)
		LOGNOTE("Bank lock %s", m_Selector.BankLock() ? "on" : "off");
	if (m_Selector.BankSelectPending() != bBankPending)
		LOGDBG("Bank select %s", m_Selector.BankSelectPending() ? "waiting for digit" : "cancelled");
}

void CKernel::DoAction(const CProgramSelector::TAction &Action)
{
	switch (Action.Type) {
	case CProgramSelector::TAction::ActProgram:
		SendProgramChange(Action.Value);
		break;

	case CProgramSelector::TAction::ActBank:
		SendBankSelect(Action.Value);
		break;

	default:
		break;
	}
}

// Send a channel message to the MIDI device, if there is one.
// Main loop only: the USB bulk transfer behind SendPlainMIDI() blocks
// (typically around a millisecond) so it must never be called from IRQ
// context.
void CKernel::SendMIDI(const u8 *pMessage, unsigned nLength)
{
	CUSBMIDIDevice *pMIDI = m_pMIDI;
	if (pMIDI == 0) {
		LOGWARN("No MIDI device connected, message not sent");
		return;
	}

	// Cable number 0; the driver wraps the message in a USB-MIDI event packet.
	if (!pMIDI->SendPlainMIDI(0, pMessage, nLength))
		LOGWARN("Sending MIDI message failed");
}

// MIDI Program Change: status 0xCn, one data byte.
void CKernel::SendProgramChange(unsigned nProgram)
{
	LOGNOTE("Program change: channel %u program %u",
		m_Settings.MidiChannel + 1, nProgram);

	assert(nProgram <= 127);
	const u8 Message[] = {
		(u8) (0xC0 | (m_Settings.MidiChannel & 0x0F)),
		(u8) (nProgram & 0x7F)
	};
	SendMIDI(Message, sizeof Message);
}

// MIDI Control Change used for bank select (32, Bank Select LSB, by default,
// which is what the Prophet Rev2 uses): status 0xBn, controller number, value.
void CKernel::SendBankSelect(unsigned nBank)
{
	LOGNOTE("Bank select: channel %u bank %d (CC %u value %u)",
		m_Settings.MidiChannel + 1, (int) nBank + m_Settings.FirstBankNumber,
		(unsigned) m_Settings.BankSelectCC, nBank);

	assert(nBank <= 127);
	const u8 Message[] = {
		(u8) (0xB0 | (m_Settings.MidiChannel & 0x0F)),
		m_Settings.BankSelectCC,
		(u8) (nBank & 0x7F)
	};
	SendMIDI(Message, sizeof Message);
}

// Millisecond count for the selector's timeout. GetTicks() runs at HZ
// ticks per second and takes over a year to wrap.
unsigned CKernel::NowMs(void)
{
	return m_Timer.GetTicks() * (1000 / HZ);
}

// Show the mode on the NumLock LED (see the TLEDMode comment in kernel.h).
// Runs in the main loop only, because SetLEDs() is a blocking USB control
// transfer; it is only called when the LED actually needs to change.
void CKernel::UpdateLED(void)
{
	CUSBKeyboardDevice *pKeyboard = m_pKeyboard;
	if (pKeyboard == 0)
		return;

	// Waiting for the user to finish something (digits of a number, or
	// the digit after /) takes precedence over the bank lock pattern.
	TLEDMode Mode = LEDSteady;
	if (m_SettingsMode.Active())
		Mode = m_SettingsMode.ValueEntry() ? LEDEntry : LEDSettings;
	else if (m_Selector.EntryDigits() > 0 || m_Selector.BankSelectPending())
		Mode = LEDEntry;
	else if (m_Selector.BankLock())
		Mode = LEDBankLock;

	unsigned nNowMs = NowMs();
	if (Mode != m_LEDMode) {
		m_LEDMode = Mode;
		m_nPatternStartMs = nNowMs;
	}

	// Where are we in the pattern? Computed from the start time rather
	// than by counting toggles, so timer granularity and loop jitter
	// can't accumulate into drift.
	unsigned nPhaseMs = nNowMs - m_nPatternStartMs;

	bool bOn = true;
	if (m_Blink.Active(nNowMs)) {
		// An acknowledgement or a readback is playing: it takes the
		// LED over from the mode pattern until it is done.
		bOn = m_Blink.IsOn(nNowMs);
	} else {
		switch (Mode) {
		case LEDEntry:
			bOn = nPhaseMs % EntryBlinkPeriodMs >= EntryBlinkPeriodMs / 2;
			break;

		case LEDBankLock:
			bOn = nPhaseMs % BankLockPeriodMs >= BankLockOffMs;
			break;

		case LEDSettings: {
			unsigned nPhase = nPhaseMs % SettingsPeriodMs;
			bOn = !(   nPhase < SettingsBlipMs
				|| (nPhase >= 2 * SettingsBlipMs && nPhase < 3 * SettingsBlipMs));
			}
			break;

		default:
			break;
		}
	}

	if (bOn != m_bLEDOn) {
		pKeyboard->SetLEDs(bOn ? LED_NUM_LOCK : 0);
		m_bLEDOn = bOn;
	}
}

// Times a long press of *. The press event arrives through the key queue
// (HandleKey starts the timing); whether * is still down comes from the
// flag the USB callback maintains. Releasing early makes it a plain tap,
// which does nothing.
void CKernel::CheckStarHold(void)
{
	if (!m_bStarTiming)
		return;

	if (!m_bStarHeld) {
		m_bStarTiming = false;
		return;
	}

	if (NowMs() - m_nStarDownMs < StarHoldMs)
		return;

	// Enter the mode. The LED changing to its pattern tells the user to
	// let go; that release is not an event, so it is not mistaken for the
	// tap that leaves the mode.
	m_bStarTiming = false;
	m_Blink.Clear();
	m_SettingsMode.Enter(NowMs());
	LOGNOTE("Settings mode on");
}

// Carries out what the settings mode decided, and gives the feedback
// (there is no display; the NumLock LED is all there is).
void CKernel::DoSettingsAction(const CSettingsMode::TAction &Action)
{
	switch (Action.Type) {
	case CSettingsMode::TAction::ActSet:
		if (ApplySetting(Action.Setting, Action.Value))
			m_Blink.BuildAccepted();
		else
			m_Blink.BuildRejected();
		m_Blink.Start(NowMs());
		break;

	case CSettingsMode::TAction::ActRejected:
		LOGNOTE("Settings: rejected");
		m_Blink.BuildRejected();
		m_Blink.Start(NowMs());
		break;

	case CSettingsMode::TAction::ActReadBack: {
		int nValue = SettingValue(Action.Setting);
		LOGNOTE("Settings: setting %d is %d", Action.Setting, nValue);
		m_Blink.BuildNumber(nValue);
		m_Blink.Start(NowMs());
		} break;

	case CSettingsMode::TAction::ActExit:
		LOGNOTE("Settings mode off");
		m_Blink.Clear();
		break;

	default:
		break;
	}
}

// Changes a setting while running; false if there is no such setting or
// the value is not valid for it.
bool CKernel::ApplySetting(int nSetting, int nValue)
{
	switch (nSetting) {
	case CSettingsMode::SettingMidiChannel:
		if (!m_Settings.SetMidiChannel(nValue))
			return false;
		LOGNOTE("MIDI channel is now %u", m_Settings.MidiChannelNumber());
		return true;

	default:
		return false;
	}
}

// The current value of a setting, as the user numbers it
int CKernel::SettingValue(int nSetting)
{
	switch (nSetting) {
	case CSettingsMode::SettingMidiChannel:
		return (int) m_Settings.MidiChannelNumber();

	default:
		return 0;
	}
}
