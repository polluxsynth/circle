//
// settingsmode.h
//
// The settings mode: a way to change settings from the numpad. Header-only
// and free of Circle dependencies so it can be tested on a host machine.
//
// * is a shift key. Holding it, and then pressing a digit, starts the mode
// for the setting with that number (the caller sees the shifted digit and
// calls Begin()). * must go down first; a digit pressed together with it, or
// before it, or after it has been let go, is just a digit. Then * can be
// let go, and:
//
//   <value> ENTER    sets the setting; the mode then ends
//   BS               cancels; the mode ends
//
// For example, * held and 1, then 1 2 ENTER, sets setting 1 (the MIDI
// channel) to 12. Holding * all the way through works too.
//
// All other keys are ignored, and nothing is sent to the MIDI device
// while the mode is on. The mode also ends by itself after the timeout
// with no key pressed.
//
// A value that is out of range is rejected as soon as it can no longer be
// valid (so 1 7 for the channel is refused at the 7), or at ENTER if it is
// too small, or if no value has been typed at all. After a rejection the
// mode stays on, with the typed digits cleared, ready for the value to be
// typed again. A chord for a setting
// that doesn't exist is rejected and doesn't start the mode.
//
// Settings:
//   1   MIDI channel, 1..16 (as numbered on the device)
//
// Begin(), KeyPressed() and Tick() return a TAction telling the caller what
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
			ActExit		// the mode has ended without doing anything
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
		m_bActive(false),
		m_nLastKeyMs(0),
		m_nSetting(0),
		m_nValue(0),
		m_nDigits(0)
	{
	}

	// Starts the mode for a setting, in response to the * + digit chord.
	// nNowMs is any free-running millisecond count (wraparound is fine).
	// If there is no such setting, this is rejected and the mode is not
	// started (and if it was on, it is left as it was).
	TAction Begin(int nSetting, unsigned nNowMs)
	{
		int nMin, nMax;
		if (!Range(nSetting, &nMin, &nMax))
			return Action(TAction::ActRejected, nSetting, 0);

		m_bActive = true;
		m_nLastKeyMs = nNowMs;
		m_nSetting = nSetting;
		ClearDigits();
		return None();
	}

	bool Active(void) const		{ return m_bActive; }

	TAction KeyPressed(unsigned nKey, unsigned nNowMs)
	{
		if (!m_bActive)
			return None();

		m_nLastKeyMs = nNowMs;

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
				int nValue = m_nValue;
				ClearDigits();
				return Action(TAction::ActRejected, m_nSetting, nValue);
			}

			return None();
		}

		if (nKey == KEYPAD_ENTER)
		{
			int nSetting = m_nSetting;
			int nValue = m_nValue;
			unsigned nDigits = m_nDigits;

			// No digits typed is no value
			int nMin, nMax;
			Range(nSetting, &nMin, &nMax);
			if (nDigits == 0 || nValue < nMin || nValue > nMax)
			{
				ClearDigits();
				return Action(TAction::ActRejected, nSetting, nValue);
			}

			Finish();
			return Action(TAction::ActSet, nSetting, nValue);
		}

		if (nKey == KEYPAD_BS)
		{
			Finish();
			return Action(TAction::ActExit, 0, 0);
		}

		return None();
	}

	// Call regularly; ends the mode after the idle timeout.
	TAction Tick(unsigned nNowMs)
	{
		if (   m_bActive
		    && (unsigned) (nNowMs - m_nLastKeyMs) >= m_nTimeoutMs)
		{
			Finish();
			return Action(TAction::ActExit, 0, 0);
		}

		return None();
	}

	// For display/logging
	int Setting(void) const		{ return m_nSetting; }
	unsigned EntryDigits(void) const { return m_nDigits; }

private:
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

	void Finish(void)
	{
		m_bActive = false;
		m_nSetting = 0;
		ClearDigits();
	}

	void ClearDigits(void)
	{
		m_nValue = 0;
		m_nDigits = 0;
	}

private:
	unsigned m_nTimeoutMs;
	bool m_bActive;
	unsigned m_nLastKeyMs;
	int m_nSetting;
	int m_nValue;
	unsigned m_nDigits;
};

#endif
