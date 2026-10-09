//
// kernel.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014  R. Stange <rsta2@o2online.de>
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
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/types.h>

#include <circle/koptions.h>   // CKernelOptions
#include <circle/devicenameservice.h>   // CDeviceNameService
#include <circle/exceptionhandler.h>  // CExceptionHandler (needed by CInterruptSystem's constructor)
#include <circle/interrupt.h>  // CInterruptSystem (needed by CSerialDevice's constructor)
#include <circle/timer.h>      // CTimer (needed by CLogger's constructor)
#include <circle/serial.h>     // CSerialDevice
#include <circle/logger.h>     // CLogger, and the LOGNOTICE/LOGDBG/etc. macros
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/usb/usbmidi.h>
#include <assert.h>

#include "spscqueue.h"
#include "progselect.h"
#include "keyreports.h"
#include "settings.h"
#include "settingsmode.h"
#include "blinkseq.h"

// A key press, queued from the USB interrupt path for the main loop
struct TKeyEvent
{
	unsigned char ucKey;		// raw USB HID usage code
	unsigned char ucModifiers;	// modifier bits at the time of the press
	bool bShift;			// * was already held down when the key went down
};

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel(void);
	~CKernel(void);

	boolean Initialize(void);

	TShutdownMode Run(void);

private:
	CActLED m_ActLED;
	CKernelOptions m_Options;
	CSettings m_Settings;                 // from m_Options; declare before m_Selector
	CDeviceNameService m_DeviceNameService;
	CExceptionHandler m_Exception;
	CInterruptSystem m_Interrupt;
	CTimer m_Timer;
	CUSBHCIDevice m_USBHCI;               // declare after m_Timer
	CUSBKeyboardDevice *volatile m_pKeyboard;
	CUSBMIDIDevice *volatile m_pMIDI;     // first USB MIDI device found, or 0
	CSerialDevice m_Serial;
	CLogger m_Logger;

	bool m_Reboot; // Set (from the main loop only) to make Run() exit

	// Turns raw reports into key presses (and handles the * shift key).
	// Touched only by the USB callback (IRQ context) once the handler
	// is registered.
	CKeyReportDecoder m_KeyDecoder;
	// Fake a 'this' pointer for static callbacks, in the case they don't
	// have a context pointer where 'this' can be passed.
	// We can probably remove this eventually.
	static CKernel *s_pThis;

	// Key presses: produced in the USB callback, consumed in Run().
	CSpscQueue<TKeyEvent, 32> m_KeyQueue;
	unsigned m_nLastDropped;

	// Turns key presses into program numbers; remembers the last one.
	// Only used from the main loop.
	CProgramSelector m_Selector;

	// Settings mode: entered by pressing a digit while * is held down
	// (see settingsmode.h). Main loop only.
	CSettingsMode m_SettingsMode;

	// One-shot LED sequence (acknowledgement, value readback), shown in
	// place of the mode pattern while it plays.
	CBlinkSequence m_Blink;

	// NumLock LED feedback, main loop only. The pattern shows the mode:
	//   steady on        normal
	//   4 Hz blink       a number is being typed, or a bank select (/)
	//                    is waiting for its digit, or a setting value is
	//                    being typed in the settings mode
	//   mostly on, with  bank lock is active
	//   a brief blip off
	// On top of those, a one-shot sequence (m_Blink) can play: see blinkseq.h.
	enum TLEDMode { LEDSteady, LEDEntry, LEDBankLock };
	bool m_bLEDOn;			// state we last sent to the keyboard
	TLEDMode m_LEDMode;		// pattern currently shown
	unsigned m_nPatternStartMs;	// when that pattern started

	void AttachKeyboard(void);
	void AttachMIDI(void);
	void HandleKey(const TKeyEvent &Event);
	void DoAction(const CProgramSelector::TAction &Action);
	void SendMIDI(const u8 *pMessage, unsigned nLength);
	void SendProgramChange(unsigned nProgram);
	void SendBankSelect(unsigned nBank);
	unsigned NowMs(void);
	void UpdateLED(void);
	void DoSettingsAction(const CSettingsMode::TAction &Action);
	bool ApplySetting(int nSetting, int nValue);
	int SettingValue(int nSetting);

	// USB callbacks. Static because Circle takes plain function pointers.
	// KeyStatusHandlerRaw may run in interrupt context: keep it short,
	// no logging, no blocking, just enqueue.
	static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6], void *pContext);
	static void DeviceRemovedHandler(CDevice *pDevice, void *pContext);
	static void MIDIRemovedHandler(CDevice *pDevice, void *pContext);
};

#endif
