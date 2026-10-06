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

static const char From[] = "kernel";

CKernel *CKernel::s_pThis = 0;
// Number the user types for MIDI program 0: 0 = typed number is the wire
// value (0..127); 1 = entry is 1-based (1..128), as most devices display it.
static const int FirstProgramNumber = 1;

// MIDI channel, 0..15 (0 is "channel 1")
static const unsigned MidiChannel = 0;

// LED patterns. Each starts with its dark phase, so a change of mode is
// visible at once.
//
// While a number is being typed: 4 Hz, 50% duty (125 ms off, then 125 ms on)
static const unsigned EntryBlinkPeriodMs = 250;

// In bank lock mode: 85% on / 15% off, once a second (150 ms off, 850 ms on)
static const unsigned BankLockPeriodMs = 1000;
static const unsigned BankLockOffMs = 150;

CKernel::CKernel(void): m_Timer(&m_Interrupt),
			//m_Serial(&m_Interrupt),
			m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
			m_pKeyboard(0),
			m_Logger(m_Options.GetLogLevel(), &m_Timer),
			m_Reboot(false),
			m_PrevKeys{0},
			m_nLastDropped(0),
			m_Selector(FirstProgramNumber),
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

		if (bUpdated && m_pKeyboard == 0)
			AttachKeyboard();

		// Drain key events queued by the USB callback. All the slow
		// work (logging, and later MIDI output) happens here.
		TKeyEvent Event;
		while (m_KeyQueue.Pop(Event))
			HandleKey(Event);

		// Commit a half-typed number once the idle timeout expires
		int nProgram = m_Selector.Tick(NowMs());
		if (nProgram != CProgramSelector::NoChange)
			SendProgramChange(nProgram);

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

	pKeyboard->RegisterRemovedHandler(DeviceRemovedHandler, this);
	pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, this);
	pKeyboard->SetLEDs(LED_NUM_LOCK);
	m_bLEDOn = true;
	m_LEDMode = LEDSteady;

	m_pKeyboard = pKeyboard;
}

void CKernel::DeviceRemovedHandler(CDevice *pDevice, void *pContext)
{
	CKernel *pThis = static_cast<CKernel *>(pContext);

	assert(pThis != 0);
	pThis->m_pKeyboard = 0;
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

	int nProgram = m_Selector.KeyPressed(Event.ucKey, NowMs());
	if (nProgram != CProgramSelector::NoChange)
		SendProgramChange(nProgram);
	else if (m_Selector.EntryDigits() > 0)
		LOGDBG("Entry so far: %d", m_Selector.EntryNumber());
}

// TODO: send a real MIDI message; for now just report what would be sent.
void CKernel::SendProgramChange(unsigned nProgram)
{
	LOGNOTE("Program change: channel %u program %u",
		MidiChannel + 1, nProgram);
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

	TLEDMode Mode = LEDSteady;
	if (m_Selector.EntryDigits() > 0)
		Mode = LEDEntry;

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
	switch (Mode) {
	case LEDEntry:
		bOn = nPhaseMs % EntryBlinkPeriodMs >= EntryBlinkPeriodMs / 2;
		break;

	default:
		break;
	}

	if (bOn != m_bLEDOn) {
		pKeyboard->SetLEDs(bOn ? LED_NUM_LOCK : 0);
		m_bLEDOn = bOn;
	}
}
