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
//   NUMLOCK  toggle bank lock (see below); discards any half-typed number
//   /        bank select: the next digit key sends that bank as a bank
//            select (by default CC 32). The wire value is the digit minus the first
//            bank number, so with a first bank of 1, keys 1-9 send 0-8
//            and key 0 is ignored. Pressing / again, BS, +, -, NUMLOCK, or
//            waiting for the timeout cancels it. Any half-typed number is
//            discarded first. The program number is not touched.
//
// In bank lock mode a digit key replaces just the units digit of the current
// program number and sends it at once: with program 36 selected, pressing 8
// selects 38. The tens (the "bank") stay put. A digit that would give a
// program outside 0..127 (e.g. 8 while on 125) is ignored. ENTER and BS have
// nothing to act on in this mode; +/- work as usual and move between banks.
// Numbers are the ones the user types, so with a first number of 1 the
// "units digit" is that of the 1-based number, not of the wire value.
//
// A typed number is also sent automatically when
//   - it can't be extended into a valid number (e.g. "13" when the maximum
//     is 127: no digit could follow), or
//   - no key has been pressed for the timeout (call Tick() regularly).
//
// KeyPressed() and Tick() return a TAction: a program (0..127) to send, a
// bank (wire value) to send, or nothing. The last program sent is remembered, and
// is the starting point for +/-. Bank selects don't change it.
//
#ifndef _progselect_h
#define _progselect_h

#include "rawkeys.h"

class CProgramSelector
{
public:
	// What the caller should do after a key press or a tick
	struct TAction
	{
		enum TType { ActNone, ActProgram, ActBank };

		TType Type;
		int Value;	// wire value to send; unused for ActNone

		bool IsNone(void) const	{ return Type == ActNone; }
	};

	enum { MaxProgram = 127 };
	enum { MaxDigits = 3 };
	enum { DefaultTimeoutMs = 1500 };

	// nFirstNumber is the number the user types for MIDI program 0:
	// 0 means typed number == wire value, 1 means entry is 1-based (as
	// most devices display presets) so typing 1..128 sends 0..127.
	// nInitialProgram is where +/- start from before anything was sent.
	// nFirstBank is the same for banks: the digit the user presses for
	// bank wire value 0 (0 or 1).
	CProgramSelector(int nFirstNumber = 0,
			 unsigned nTimeoutMs = DefaultTimeoutMs,
			 int nInitialProgram = 0,
			 int nFirstBank = 0)
	:	m_nFirstNumber(nFirstNumber),
		m_nFirstBank(nFirstBank),
		m_nTimeoutMs(nTimeoutMs),
		m_nLastProgram(Clamp(nInitialProgram)),
		m_nEntry(0),
		m_nDigits(0),
		m_nLastKeyMs(0),
		m_bBankLock(false),
		m_bBankPending(false)
	{
	}

	// nNowMs is any free-running millisecond count (wraparound is fine).
	TAction KeyPressed(unsigned nKey, unsigned nNowMs)
	{
		if (isNumeric(nKey))
		{
			// A pending bank select takes the next digit, whatever
			// the mode, and is then finished.
			if (m_bBankPending)
			{
				m_bBankPending = false;

				int nBank = keyVal(nKey) - m_nFirstBank;
				if (nBank < 0)
					return None();	// no such bank: ignored
				return Action(TAction::ActBank, nBank);
			}

			if (m_bBankLock)
				return ProgramAction(BankDigit(keyVal(nKey)));

			return ProgramAction(Digit(keyVal(nKey), nNowMs));
		}

		switch (nKey)
		{
		case KEYPAD_SLASH:
		{
			// Arms bank select; pressed again, it cancels it.
			bool bWasPending = m_bBankPending;
			Cancel();
			m_bBankPending = !bWasPending;
			m_nLastKeyMs = nNowMs;
			return None();
		}
		case KEYPAD_NUMLOCK:	Cancel(); m_bBankLock = !m_bBankLock; return None();
		case KEYPAD_ENTER:	return ProgramAction(Commit());
		case KEYPAD_BS:		Cancel(); return None();
		case KEYPAD_PLUS:	Cancel(); return ProgramAction(Step(+1));
		case KEYPAD_MINUS:	Cancel(); return ProgramAction(Step(-1));
		default:		return None();
		}
	}

	// Call regularly; commits a typed number, or drops a pending bank
	// select, after the idle timeout.
	TAction Tick(unsigned nNowMs)
	{
		if ((m_nDigits > 0 || m_bBankPending)
		    && (unsigned) (nNowMs - m_nLastKeyMs) >= m_nTimeoutMs)
		{
			if (m_bBankPending)
			{
				m_bBankPending = false;
				return None();
			}

			return ProgramAction(Commit());
		}

		return None();
	}

	// For display/logging
	int LastProgram(void) const	{ return m_nLastProgram; }
	bool BankLock(void) const	{ return m_bBankLock; }
	bool BankSelectPending(void) const { return m_bBankPending; }
	unsigned EntryDigits(void) const { return m_nDigits; }
	int EntryNumber(void) const	{ return m_nEntry; }

private:
	enum { NoChange = -1 };		// internal "no program" result

	static TAction None(void)
	{
		return Action(TAction::ActNone, 0);
	}

	static TAction Action(TAction::TType Type, int nValue)
	{
		TAction Result = { Type, nValue };
		return Result;
	}

	static TAction ProgramAction(int nProgram)
	{
		return nProgram == NoChange ? None()
					    : Action(TAction::ActProgram, nProgram);
	}

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

	// Bank lock: replace the units digit of the current number and send it.
	int BankDigit(int nDigit)
	{
		int nNumber = m_nLastProgram + m_nFirstNumber;
		nNumber = nNumber / 10 * 10 + nDigit;

		int nProgram = nNumber - m_nFirstNumber;
		if (nProgram < 0 || nProgram > MaxProgram)
			return NoChange;	// no such program: ignored

		m_nLastProgram = nProgram;
		return nProgram;
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

	// Abandons both a half-typed number and a pending bank select
	void Cancel(void)
	{
		m_nEntry = 0;
		m_nDigits = 0;
		m_bBankPending = false;
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
	int m_nFirstBank;
	unsigned m_nTimeoutMs;
	int m_nLastProgram;
	int m_nEntry;
	unsigned m_nDigits;
	unsigned m_nLastKeyMs;
	bool m_bBankLock;
	bool m_bBankPending;	// "/" pressed, waiting for the bank digit
};

#endif
