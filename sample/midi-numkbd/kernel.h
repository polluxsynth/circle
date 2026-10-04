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
	CDeviceNameService m_DeviceNameService;
	CExceptionHandler m_Exception;
	CInterruptSystem m_Interrupt;
	CTimer m_Timer;
	CUSBHCIDevice m_USBHCI;               // declare after m_Timer
	CUSBKeyboardDevice *volatile m_pKeyboard;
	CSerialDevice m_Serial;
	CLogger m_Logger;

	unsigned char m_PrevKeys[6];
	// Fake a 'this' pointer for static callbacks, in the case they don't
	// have a context pointer where 'this' can be passed.
	// We can probably remove this eventually.
	static CKernel *s_pThis;

	// raw-key callback (static, because Circle takes plain function pointers)
	static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6], void *pContext);
	static void DeviceRemovedHandler(CDevice *pDevice, void *pContext);
	void OnKeyDown(unsigned char ucModifiers, unsigned char ucUsage);
};

#endif
