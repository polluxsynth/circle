//
// keyreports.h
//
// Turns the USB keyboard's raw reports into key press events. Header-only
// and free of Circle dependencies so it can be tested on a host machine.
//
// In raw mode the keyboard reports the set of keys currently held (up to 6
// usage codes), not presses, so each report is compared with the previous
// one to find the keys that have just gone down.
//
// * is a shift key: it never produces an event of its own. Instead, each
// key that goes down while * is held is marked as shifted. For that, * must
// have gone down before the key: it has to be in the previous report as well
// as the current one. So a key pressed together with * (in the same
// report), or before it, is not shifted, nor is one pressed as * is let go.
//
#ifndef _keyreports_h
#define _keyreports_h

#include "rawkeys.h"

class CKeyReportDecoder
{
public:
	CKeyReportDecoder(void)
	{
		Reset();
	}

	// Forgets the keys that were held, e.g. when a keyboard is attached
	void Reset(void)
	{
		for (unsigned i = 0; i < 6; i++)
			m_Prev[i] = 0;
	}

	// Calls Push(unsigned char ucKey, bool bShift) for each key that has
	// gone down since the previous report. It is called from the USB
	// interrupt path, so Push must only do something quick.
	template <typename TPush>
	void Process(const unsigned char RawKeys[6], TPush Push)
	{
		// Rollover/error report (usage 1): ignore it entirely and keep
		// the previous state, so keys aren't reported as released and
		// pressed again.
		for (unsigned i = 0; i < 6; i++)
			if (RawKeys[i] == 1)
				return;

		// Shifted only if * was already down, and still is
		bool bStarNow = false;
		bool bStarBefore = false;
		for (unsigned i = 0; i < 6; i++)
		{
			if (RawKeys[i] == KEYPAD_STAR)
				bStarNow = true;
			if (m_Prev[i] == KEYPAD_STAR)
				bStarBefore = true;
		}
		bool bShift = bStarNow && bStarBefore;

		for (unsigned i = 0; i < 6; i++)
		{
			unsigned char key = RawKeys[i];
			if (key < 4) continue;			// 0 = none, 2..3 = error codes
			if (key == KEYPAD_STAR) continue;	// the shift key

			bool bNew = true;
			for (unsigned j = 0; j < 6; j++)
				if (m_Prev[j] == key)
					bNew = false;

			if (bNew)
				Push(key, bShift);
		}

		for (unsigned i = 0; i < 6; i++)
			m_Prev[i] = RawKeys[i];
	}

private:
	unsigned char m_Prev[6];
};

#endif
