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

	void Blink(unsigned nCount);

	boolean Initialize(void);

	TShutdownMode Run(void);

private:
	CActLED m_ActLED;
	CKernelOptions m_Options;
	CDeviceNameService m_DeviceNameService;
	CExceptionHandler m_Exception;
	CInterruptSystem m_Interrupt;
	CTimer m_Timer;
	CSerialDevice m_Serial;
	CLogger m_Logger;
	
};

#endif
