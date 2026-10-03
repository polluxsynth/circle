//
// settingsmode_test.cpp
//
// Host test for settingsmode.h and blinkseq.h:
//   g++ -std=c++17 -Wall -fsanitize=address,undefined settingsmode_test.cpp -o t && ./t
//
#include "settingsmode.h"
#include "blinkseq.h"

#include <cstdio>

static int g_nFailed = 0;

#define CHECK(cond)	do { if (!(cond)) { \
				printf("FAIL line %d: %s\n", __LINE__, #cond); \
				g_nFailed++; } } while (0)

typedef CSettingsMode::TAction TAction;

static unsigned g_nNow = 1000;

static TAction Press(CSettingsMode &Mode, unsigned nKey)
{
	g_nNow += 100;
	return Mode.KeyPressed(nKey, g_nNow);
}

static unsigned DigitKey(int nDigit)
{
	return nDigit == 0 ? KEYPAD_0 : KEYPAD_1 + (nDigit - 1);
}

// Types a number's digits, returning the action from the last one
static TAction TypeNumber(CSettingsMode &Mode, const char *pDigits)
{
	TAction Last = {TAction::ActNone, 0, 0};
	for (; *pDigits != '\0'; pDigits++)
		Last = Press(Mode, DigitKey(*pDigits - '0'));
	return Last;
}

int main(void)
{
	// ---- the settings mode

	{
		CSettingsMode Mode;
		CHECK(!Mode.Active());
		CHECK(Press(Mode, KEYPAD_1).Type == TAction::ActNone);	// ignored when off
		CHECK(Press(Mode, KEYPAD_ENTER).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_BS).Type == TAction::ActNone);
		CHECK(!Mode.Active());

		// The example: chord *+1, then 1 2 ENTER, sets setting 1 to 12
		CHECK(Mode.Begin(1, g_nNow).Type == TAction::ActNone);
		CHECK(Mode.Active() && Mode.Setting() == 1 && Mode.EntryDigits() == 0);
		CHECK(TypeNumber(Mode, "12").Type == TAction::ActNone);
		CHECK(Mode.EntryDigits() == 2);

		TAction A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Setting == 1 && A.Value == 12);
		CHECK(!Mode.Active());					// done

		// Limits of the range
		Mode.Begin(1, g_nNow); TypeNumber(Mode, "1");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Value == 1 && !Mode.Active());

		Mode.Begin(1, g_nNow); TypeNumber(Mode, "16");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Value == 16 && !Mode.Active());

		// Leading zeros are fine
		Mode.Begin(1, g_nNow); TypeNumber(Mode, "007");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Value == 7);

		// 0 is below the range: rejected at ENTER. The mode stays on with
		// the digits cleared, so the value can just be typed again.
		Mode.Begin(1, g_nNow); TypeNumber(Mode, "0");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActRejected && A.Setting == 1);
		CHECK(Mode.Active() && Mode.EntryDigits() == 0);
		TypeNumber(Mode, "5");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Value == 5 && !Mode.Active());

		// 17 is above it: rejected at the 7, no ENTER needed; same again
		Mode.Begin(1, g_nNow);
		CHECK(Press(Mode, KEYPAD_1).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_7).Type == TAction::ActRejected);
		CHECK(Mode.Active() && Mode.EntryDigits() == 0);
		TypeNumber(Mode, "9");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Value == 9 && !Mode.Active());

		// Too many leading zeros
		Mode.Begin(1, g_nNow);
		CHECK(TypeNumber(Mode, "000").Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_0).Type == TAction::ActRejected);
		CHECK(Mode.Active() && Mode.EntryDigits() == 0);
		Press(Mode, KEYPAD_BS);

		// Chords for settings that don't exist: rejected, mode not started
		CHECK(Mode.Begin(9, g_nNow).Type == TAction::ActRejected);
		CHECK(!Mode.Active());
		CHECK(Mode.Begin(5, g_nNow).Type == TAction::ActRejected);
		CHECK(!Mode.Active());
		// ... and if it was on already, it is left as it was
		Mode.Begin(1, g_nNow); TypeNumber(Mode, "1");
		CHECK(Mode.Begin(5, g_nNow).Type == TAction::ActRejected);
		CHECK(Mode.Active() && Mode.Setting() == 1 && Mode.EntryDigits() == 1);
		Press(Mode, KEYPAD_BS);

		// ENTER with no value typed is rejected, and the mode stays on
		Mode.Begin(1, g_nNow);
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActRejected && A.Setting == 1);
		CHECK(Mode.Active() && Mode.EntryDigits() == 0);
		A = Press(Mode, KEYPAD_ENTER);			// and again
		CHECK(A.Type == TAction::ActRejected && Mode.Active());
		TypeNumber(Mode, "4");
		A = Press(Mode, KEYPAD_ENTER);
		CHECK(A.Type == TAction::ActSet && A.Value == 4 && !Mode.Active());

		// +, -, /, NumLock and * mean nothing in the mode
		Mode.Begin(1, g_nNow); TypeNumber(Mode, "1");
		CHECK(Press(Mode, KEYPAD_PLUS).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_MINUS).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_SLASH).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_NUMLOCK).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_STAR).Type == TAction::ActNone);
		CHECK(Mode.Active() && Mode.EntryDigits() == 1);

		// BS cancels, and nothing is set
		CHECK(Press(Mode, KEYPAD_BS).Type == TAction::ActExit);
		CHECK(!Mode.Active());
		CHECK(Press(Mode, KEYPAD_ENTER).Type == TAction::ActNone);

		// A fresh start is clean: no digits left over from a cancelled one
		Mode.Begin(1, g_nNow); TypeNumber(Mode, "12");
		Press(Mode, KEYPAD_BS);
		Mode.Begin(1, g_nNow);
		CHECK(Mode.EntryDigits() == 0);
		CHECK(Press(Mode, KEYPAD_ENTER).Type == TAction::ActRejected);
		CHECK(Mode.Active());
	}

	{
		// The reboot, setting 0: armed by the chord, done by DEL
		CSettingsMode Mode;

		// DEL means nothing when the mode is off
		CHECK(Press(Mode, KEYPAD_DEL).Type == TAction::ActNone);

		CHECK(Mode.Begin(0, g_nNow).Type == TAction::ActNone);
		CHECK(Mode.Active() && Mode.Setting() == 0);

		// Armed, but nothing else does it: not ENTER, digits or the other keys
		CHECK(Press(Mode, KEYPAD_ENTER).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_0).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_1).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_PLUS).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_MINUS).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_SLASH).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_NUMLOCK).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_STAR).Type == TAction::ActNone);
		CHECK(Press(Mode, KEYPAD_TAB).Type == TAction::ActNone);
		CHECK(Mode.Active() && Mode.EntryDigits() == 0);

		TAction A = Press(Mode, KEYPAD_DEL);
		CHECK(A.Type == TAction::ActReboot && A.Setting == 0);
		CHECK(!Mode.Active());
		CHECK(Press(Mode, KEYPAD_DEL).Type == TAction::ActNone);	// only once

		// BS backs out of it
		Mode.Begin(0, g_nNow);
		CHECK(Press(Mode, KEYPAD_BS).Type == TAction::ActExit);
		CHECK(!Mode.Active());
		CHECK(Press(Mode, KEYPAD_DEL).Type == TAction::ActNone);

		// DEL must never reboot from a value entry
		Mode.Begin(1, g_nNow);
		CHECK(Press(Mode, KEYPAD_DEL).Type == TAction::ActNone);
		TypeNumber(Mode, "1");
		CHECK(Press(Mode, KEYPAD_DEL).Type == TAction::ActNone);
		CHECK(Mode.Active() && Mode.Setting() == 1 && Mode.EntryDigits() == 1);
		Press(Mode, KEYPAD_BS);

		// It leaves no trace in the next entry
		Mode.Begin(1, g_nNow);
		CHECK(Mode.EntryDigits() == 0);
		TypeNumber(Mode, "3");
		TAction S = Press(Mode, KEYPAD_ENTER);
		CHECK(S.Type == TAction::ActSet && S.Setting == 1 && S.Value == 3);

		// Armed and left alone, it times out and does nothing
		CSettingsMode Timed(10000);
		Timed.Begin(0, 5000);
		CHECK(Timed.Tick(14999).Type == TAction::ActNone);
		CHECK(Timed.Tick(15000).Type == TAction::ActExit);
		CHECK(!Timed.Active());
		CHECK(Timed.KeyPressed(KEYPAD_DEL, 15001).Type == TAction::ActNone);

		// Chord for another setting while armed is just rejected, state kept
		Mode.Begin(0, g_nNow);
		CHECK(Mode.Begin(5, g_nNow).Type == TAction::ActRejected);
		CHECK(Mode.Active() && Mode.Setting() == 0);
		CHECK(Press(Mode, KEYPAD_DEL).Type == TAction::ActReboot);
	}

	{
		// Timeout: counted from the last key, and wraparound is fine
		CSettingsMode Mode(10000);
		unsigned nStart = 0xFFFFF000u;
		Mode.Begin(1, nStart);
		CHECK(Mode.Tick(nStart + 9999).Type == TAction::ActNone);
		CHECK(Mode.Active());

		Mode.KeyPressed(KEYPAD_1, nStart + 9000);		// restarts the clock
		CHECK(Mode.Tick(nStart + 18999).Type == TAction::ActNone);
		CHECK(Mode.Tick(nStart + 19000).Type == TAction::ActExit);
		CHECK(!Mode.Active());
		CHECK(Mode.Tick(nStart + 99999).Type == TAction::ActNone);	// only once

		// A value half typed when the time runs out is dropped
		Mode.Begin(1, nStart);
		Mode.KeyPressed(KEYPAD_1, nStart + 1);
		Mode.KeyPressed(KEYPAD_1, nStart + 2);
		CHECK(Mode.Tick(nStart + 20000).Type == TAction::ActExit);
		Mode.Begin(1, nStart + 30000);
		CHECK(Mode.EntryDigits() == 0);
	}

	// ---- the blink sequences

	{
		CBlinkSequence B;
		CHECK(!B.Active(0));				// nothing built

		B.BuildAccepted();
		B.Start(5000);
		CHECK(B.TotalMs() == 480);
		CHECK(B.Active(5000) && B.Active(5479) && !B.Active(5480));
		CHECK(!B.IsOn(5000) && !B.IsOn(5059));		// starts dark
		CHECK(B.IsOn(5060) && B.IsOn(5119));
		CHECK(!B.IsOn(5120));
		CHECK(B.IsOn(5420) && B.IsOn(5479));		// ends lit

		B.BuildRejected();
		B.Start(0);
		CHECK(B.TotalMs() == 1200);
		CHECK(B.Active(1199) && !B.Active(1200));
		CHECK(!B.IsOn(0) && !B.IsOn(1199));

		// Both sequences fit with room to spare, and start dark
		B.BuildAccepted();
		B.Start(0xFFFFFF00u);				// wraps in the middle
		CHECK(B.TotalMs() == 480);
		CHECK(B.Active(0xFFFFFF00u + 479) && !B.Active(0xFFFFFF00u + 480));
		CHECK(!B.IsOn(0xFFFFFF00u) && B.IsOn(0xFFFFFF00u + 60));

		B.Clear();
		CHECK(!B.Active(0));
	}

	printf(g_nFailed == 0 ? "all tests passed\n" : "%d FAILED\n", g_nFailed);
	return g_nFailed != 0;
}
