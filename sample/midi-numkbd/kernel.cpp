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

static const char From[] = "kernel";

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel(void): m_Timer(&m_Interrupt),
			//m_Serial(&m_Interrupt),
			m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
			m_pKeyboard(0),
			m_Logger(m_Options.GetLogLevel(), &m_Timer),
			m_PrevKeys{0}
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

	m_ActLED.Blink(5);
	CTimer::SimpleMsDelay(600);

	return bOK;
}

TShutdownMode CKernel::Run(void)
{
	LOGDBG("MIDI Numkbd starting up\n");

	for (;;) {
		boolean bUpdated = m_USBHCI.UpdatePlugAndPlay();

		if (bUpdated && m_pKeyboard == 0) {
			LOGDBG("PnP updated, registering kbd");
			m_pKeyboard = (CUSBKeyboardDevice *)
				m_DeviceNameService.GetDevice("ukbd1", FALSE);
			if (m_pKeyboard != 0) {
				LOGDBG("Numpad connected");
				for (int i = 0; i < 6; i++)
					m_PrevKeys[i] = 0;
				m_pKeyboard->RegisterRemovedHandler(DeviceRemovedHandler, this);
				m_pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, this);
				m_pKeyboard->SetLEDs(0x01); // Turn on NumLock
			}
		}

		m_Timer.MsDelay(1);
	}
	LOGDBG("Rebooting\n");
	return ShutdownReboot;
}

void CKernel::DeviceRemovedHandler(CDevice *pDevice, void *pContext)
{
	CKernel *pThis = static_cast<CKernel *>(pContext);

	assert(pThis != 0);
	pThis->m_pKeyboard = 0;
	LOGDBG("Numpad removed");
}

void CKernel::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6], void *pContext)
{
	CKernel *pThis = static_cast<CKernel *>(pContext);

	assert(pThis != 0);

	// Raw mode reports the set of keys currently held, not press events,
	// so compare against the previous report to find new presses.
	LOGDBG("Raw keys: 0x%02X 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
		RawKeys[0], RawKeys[1], RawKeys[2], RawKeys[3], RawKeys[4], RawKeys[5]);
	for (unsigned i = 0; i < 6; i++)
	{
		unsigned char key = RawKeys[i];
		if (key == 1) return; // Rollover: bail out, skip copy to prev
		if (key < 4) continue; // 0 = none, 1..3 = error/rollover
		boolean bNew = TRUE;
		for (unsigned j = 0; j < 6; j++)
			if (pThis->m_PrevKeys[j] == key) bNew = FALSE;
		if (bNew) pThis->OnKeyDown(ucModifiers, key);
	}
	for (unsigned i = 0; i < 6; i++)
		pThis->m_PrevKeys[i] = RawKeys[i];
}

void CKernel::OnKeyDown(unsigned char ucModifiers, unsigned char ucKey)
{
	LOGDBG("Key down: Modifier 0x%02X Code 0x%02X", (unsigned) ucModifiers, (unsigned) ucKey);
}
