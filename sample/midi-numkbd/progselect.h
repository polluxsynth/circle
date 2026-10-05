//
// progselect.h
//
// Turns numpad key presses into MIDI program numbers. Header-only and free
// of Circle dependencies so it can be tested on a host machine.
//
// Behaviour:
//   0-9      type a program number (up to 3 digits)
//   ENTER    send the typed number now
//   BS       discard the typed number
//   +  / -   step the last program up/down by one (clamped to 0..127);
//            any half-typed number is discarded first
//
// A typed number is also sent automatically when
//   - it can't be extended into a valid number (e.g. "13" when the maximum
//     is 127: no digit could follow), or
//   - no key has been pressed for the timeout (call Tick() regularly).
//
// KeyPressed() and Tick() return the program (0..127) to send, or NoChange.
// The last program sent is remembered, and is the starting point for +/-.
//
#ifndef _progselect_h
#define _progselect_h

#include "rawkeys.h"

class CProgramSelector
{
public:
	enum { NoChange = -1 };
	enum { MaxProgram = 127 };
	enum { MaxDigits = 3 };
	enum { DefaultTimeoutMs = 1500 };

	// nFirstNumber is the number the user types for MIDI program 0:
	// 0 means typed number == wire value, 1 means entry is 1-based (as
	// most devices display presets) so typing 1..128 sends 0..127.
	// nInitialProgram is where +/- start from before anything was sent.
	CProgramSelector(int nFirstNumber = 0,
			 unsigned nTimeoutMs = DefaultTimeoutMs,
			 int nInitialProgram = 0)
	:	m_nFirstNumber(nFirstNumber),
		m_nTimeoutMs(nTimeoutMs),
		m_nLastProgram(Clamp(nInitialProgram)),
		m_nEntry(0),
		m_nDigits(0),
		m_nLastKeyMs(0)
	{
	}

	// nNowMs is any free-running millisecond count (wraparound is fine).
	int KeyPressed(unsigned nKey, unsigned nNowMs)
	{
		if (isNumeric(nKey))
			return Digit(keyVal(nKey), nNowMs);

		switch (nKey)
		{
		case KEYPAD_ENTER:	return Commit();
		case KEYPAD_BS:		Cancel(); return NoChange;
		case KEYPAD_PLUS:	Cancel(); return Step(+1);
		case KEYPAD_MINUS:	Cancel(); return Step(-1);
		default:		return NoChange;
		}
	}

	// Call regularly; commits a typed number after the idle timeout.
	int Tick(unsigned nNowMs)
	{
		if (m_nDigits > 0 && (unsigned) (nNowMs - m_nLastKeyMs) >= m_nTimeoutMs)
			return Commit();

		return NoChange;
	}

	// For display/logging
	int LastProgram(void) const	{ return m_nLastProgram; }
	unsigned EntryDigits(void) const { return m_nDigits; }
	int EntryNumber(void) const	{ return m_nEntry; }

private:
	int MaxNumber(void) const	{ return MaxProgram + m_nFirstNumber; }

	static int Clamp(int nProgram)
	{
		if (nProgram < 0) return 0;
		if (nProgram > MaxProgram) return MaxProgram;
		return nProgram;
	}

	int Digit(int nDigit, unsigned nNowMs)
	{
		m_nEntry = m_nEntry * 10 + nDigit;
		m_nDigits++;
		m_nLastKeyMs = nNowMs;

		// Done if full, or if no further digit could keep it valid
		if (m_nDigits >= MaxDigits || m_nEntry * 10 > MaxNumber())
			return Commit();

		return NoChange;
	}

	// Send the typed number if it is a valid program, else drop it.
	int Commit(void)
	{
		if (m_nDigits == 0)
			return NoChange;

		int nProgram = m_nEntry - m_nFirstNumber;
		Cancel();

		if (nProgram < 0 || nProgram > MaxProgram)
			return NoChange;	// out of range: rejected

		m_nLastProgram = nProgram;
		return nProgram;
	}

	void Cancel(void)
	{
		m_nEntry = 0;
		m_nDigits = 0;
	}

	// Always returns the new program, even if clamped at either end and
	// unchanged, so repeated presses re-send the value (harmless, and it
	// resyncs a device that has drifted).
	int Step(int nDelta)
	{
		m_nLastProgram = Clamp(m_nLastProgram + nDelta);
		return m_nLastProgram;
	}

private:
	int m_nFirstNumber;
	unsigned m_nTimeoutMs;
	int m_nLastProgram;
	int m_nEntry;
	unsigned m_nDigits;
	unsigned m_nLastKeyMs;
};

#endif
