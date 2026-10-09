//
// settingsmode.h
//
// The settings mode: a way to change settings from the numpad. Header-only
// and free of Circle dependencies so it can be tested on a host machine.
//
// Getting in is the caller's business (the kernel enters the mode when *
// has been held down for a couple of seconds); Enter() switches it on.
// Then:
//
//   <setting> <value> ENTER    set a setting, e.g.  1 1 2 ENTER  sets
//                              setting 1 (the MIDI channel) to 12
//   <setting> ENTER            read it back (the kernel blinks the value)
//   BS                         while typing a value: back to choosing a
//                              setting; when choosing: leave the mode
//   *                          leave the mode
//
// All other keys are ignored, and nothing is sent to the MIDI device
// while the mode is on. The mode also ends by itself after the timeout
// with no key pressed.
//
// A value that is out of range is rejected as soon as it can no longer be
// valid (so 1 7 for the channel is refused at the 7), or at ENTER if it is
// too small. After a rejection, an accepted value or a read back, the mode
// is ready for the next setting.
//
// Settings:
//   1   MIDI channel, 1..16 (as numbered on the device)
//
// KeyPressed() and Tick() return a TAction telling the caller what
// happened, so it can apply the change and give feedback.
//
#ifndef _settingsmode_h
#define _settingsmode_h

#include "rawkeys.h"

class CSettingsMode
{
public:
	// What the caller should do after a key press or a tick
	struct TAction
	{
		enum TType
		{
			ActNone,
			ActSet,		// set Setting to Value (already range-checked)
			ActRejected,	// bad setting number or value
			ActReadBack,	// show the current value of Setting
			ActExit		// the mode has ended
		};

		TType Type;
		int Setting;
		int Value;
	};

	// Setting numbers
	enum { SettingMidiChannel = 1 };

	enum { MaxDigits = 3 };
	enum { DefaultTimeoutMs = 10000 };

	CSettingsMode(unsigned nTimeoutMs = DefaultTimeoutMs)
	:	m_nTimeoutMs(nTimeoutMs),
		m_State(StateOff),
		m_nLastKeyMs(0),
		m_nSetting(0),
		m_nValue(0),
		m_nDigits(0)
	{
	}

	// Switches the mode on. nNowMs is any free-running millisecond count
	// (wraparound is fine).
	void Enter(unsigned nNowMs)
	{
		m_State = StateChoose;
		m_nLastKeyMs = nNowMs;
		ResetEntry();
	}

	bool Active(void) const		{ return m_State != StateOff; }

	// True once a setting has been chosen and its value is awaited
	bool ValueEntry(void) const	{ return m_State == StateValue; }

	TAction KeyPressed(unsigned nKey, unsigned nNowMs)
	{
		if (!Active())
			return None();

		m_nLastKeyMs = nNowMs;

		if (nKey == KEYPAD_STAR)
			return Exit();

		if (m_State == StateChoose)
		{
			if (isNumeric(nKey))
			{
				int nSetting = keyVal(nKey);

				int nMin, nMax;
				if (!Range(nSetting, &nMin, &nMax))
					return Action(TAction::ActRejected, nSetting, 0);

				m_nSetting = nSetting;
				m_nValue = 0;
				m_nDigits = 0;
				m_State = StateValue;
				return None();
			}

			if (nKey == KEYPAD_BS)
				return Exit();

			return None();
		}

		// Waiting for the value
		if (isNumeric(nKey))
		{
			int nMin, nMax;
			Range(m_nSetting, &nMin, &nMax);

			m_nValue = m_nValue * 10 + keyVal(nKey);
			m_nDigits++;

			// Digits only ever make a number bigger, so too big now
			// means too big for good.
			if (m_nDigits > MaxDigits || m_nValue > nMax)
			{
				int nSetting = m_nSetting;
				int nValue = m_nValue;
				BackToChoose();
				return Action(TAction::ActRejected, nSetting, nValue);
			}

			return None();
		}

		if (nKey == KEYPAD_ENTER)
		{
			int nSetting = m_nSetting;
			int nValue = m_nValue;
			unsigned nDigits = m_nDigits;
			BackToChoose();

			if (nDigits == 0)
				return Action(TAction::ActReadBack, nSetting, 0);

			int nMin, nMax;
			Range(nSetting, &nMin, &nMax);
			if (nValue < nMin || nValue > nMax)
				return Action(TAction::ActRejected, nSetting, nValue);

			return Action(TAction::ActSet, nSetting, nValue);
		}

		if (nKey == KEYPAD_BS)
		{
			BackToChoose();
			return None();
		}

		return None();
	}

	// Call regularly; ends the mode after the idle timeout.
	TAction Tick(unsigned nNowMs)
	{
		if (   Active()
		    && (unsigned) (nNowMs - m_nLastKeyMs) >= m_nTimeoutMs)
			return Exit();

		return None();
	}

	// For display/logging
	int Setting(void) const		{ return m_nSetting; }
	unsigned EntryDigits(void) const { return m_nDigits; }

private:
	enum TState { StateOff, StateChoose, StateValue };

	// Range of valid values of a setting; false if there is no such
	// setting. The MIDI channel range must match CSettings.
	static bool Range(int nSetting, int *pMin, int *pMax)
	{
		switch (nSetting)
		{
		case SettingMidiChannel:
			*pMin = 1;
			*pMax = 16;
			return true;

		default:
			return false;
		}
	}

	static TAction Action(TAction::TType Type, int nSetting, int nValue)
	{
		TAction Result = { Type, nSetting, nValue };
		return Result;
	}

	static TAction None(void)
	{
		return Action(TAction::ActNone, 0, 0);
	}

	TAction Exit(void)
	{
		m_State = StateOff;
		ResetEntry();
		return Action(TAction::ActExit, 0, 0);
	}

	void BackToChoose(void)
	{
		m_State = StateChoose;
		ResetEntry();
	}

	void ResetEntry(void)
	{
		m_nSetting = 0;
		m_nValue = 0;
		m_nDigits = 0;
	}

private:
	unsigned m_nTimeoutMs;
	TState m_State;
	unsigned m_nLastKeyMs;
	int m_nSetting;
	int m_nValue;
	unsigned m_nDigits;
};

#endif
