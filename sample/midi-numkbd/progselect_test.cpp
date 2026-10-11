//
// progselect_test.cpp
//
// Host test for progselect.h:
//   g++ -std=c++17 -Wall -fsanitize=address,undefined progselect_test.cpp -o t && ./t
//
#include "progselect.h"

#include <cstdio>

static int g_nFailed = 0;

#define CHECK(cond)	do { if (!(cond)) { \
				printf("FAIL line %d: %s\n", __LINE__, #cond); \
				g_nFailed++; } } while (0)

typedef CProgramSelector::TAction TAction;

static unsigned g_nNow = 1000;

// Presses a key a little after the previous one
static TAction Press(CProgramSelector &Sel, unsigned nKey)
{
	g_nNow += 10;
	return Sel.KeyPressed(nKey, g_nNow);
}

static unsigned DigitKey(int nDigit)
{
	return nDigit == 0 ? KEYPAD_0 : KEYPAD_1 + (nDigit - 1);
}

// Types the digits, returning the action from the last one
static TAction Type(CProgramSelector &Sel, const char *pDigits)
{
	TAction Last = {TAction::ActNone, 0};
	for (; *pDigits != '\0'; pDigits++)
		Last = Press(Sel, DigitKey(*pDigits - '0'));
	return Last;
}

static bool IsProgram(const TAction &A, int nValue)
{
	return A.Type == TAction::ActProgram && A.Value == nValue;
}

static bool IsBank(const TAction &A, int nValue)
{
	return A.Type == TAction::ActBank && A.Value == nValue;
}

int main(void)
{
	// ---- typing numbers (first number 0)

	{
		CProgramSelector Sel;
		CHECK(Press(Sel, KEYPAD_5).IsNone());
		CHECK(Sel.EntryDigits() == 1 && Sel.EntryNumber() == 5);
		CHECK(IsProgram(Press(Sel, KEYPAD_ENTER), 5));
		CHECK(Sel.LastProgram() == 5 && Sel.EntryDigits() == 0);

		CHECK(Type(Sel, "12").IsNone());			// 120 still valid: waits
		CHECK(Sel.EntryDigits() == 2 && Sel.EntryNumber() == 12);
		CHECK(IsProgram(Press(Sel, KEYPAD_ENTER), 12));

		CHECK(Press(Sel, KEYPAD_ENTER).IsNone());		// nothing to send
	}

	// Sent as soon as no digit could extend it
	{
		CProgramSelector Sel;
		CHECK(IsProgram(Type(Sel, "13"), 13));			// 130 > 127
		CHECK(IsProgram(Type(Sel, "20"), 20));
		CHECK(IsProgram(Type(Sel, "99"), 99));
		CHECK(Type(Sel, "10").IsNone());			// 100 is valid: waits
		CHECK(IsProgram(Press(Sel, KEYPAD_0), 100));		// third digit: full
		CHECK(IsProgram(Type(Sel, "127"), 127));
		CHECK(Type(Sel, "12").IsNone());
		CHECK(Press(Sel, KEYPAD_8).IsNone());			// 128: rejected
		CHECK(Sel.EntryDigits() == 0);
		CHECK(Sel.LastProgram() == 127);			// unchanged by the rejection
	}

	// Leading zeros
	{
		CProgramSelector Sel;
		CHECK(Type(Sel, "05").IsNone());
		CHECK(IsProgram(Press(Sel, KEYPAD_ENTER), 5));
		CHECK(IsProgram(Type(Sel, "007"), 7));			// full after 3 digits
		CHECK(Press(Sel, KEYPAD_0).IsNone());
		CHECK(IsProgram(Press(Sel, KEYPAD_ENTER), 0));		// 0 is a program
	}

	// BS discards the entry
	{
		CProgramSelector Sel;
		Type(Sel, "12");
		CHECK(Press(Sel, KEYPAD_BS).IsNone());
		CHECK(Sel.EntryDigits() == 0 && Sel.EntryNumber() == 0);
		CHECK(Press(Sel, KEYPAD_ENTER).IsNone());
		CHECK(Sel.LastProgram() == 0);
	}

	// ---- timeout

	{
		CProgramSelector Sel;			// 1500 ms
		Sel.KeyPressed(KEYPAD_5, 1000);
		CHECK(Sel.Tick(1000).IsNone());
		CHECK(Sel.Tick(2499).IsNone());
		CHECK(IsProgram(Sel.Tick(2500), 5));
		CHECK(Sel.Tick(9000).IsNone());				// only once
		CHECK(Sel.EntryDigits() == 0);
		CHECK(Sel.Tick(0).IsNone());				// nothing pending
	}

	// Measured from the last digit
	{
		CProgramSelector Sel;
		Sel.KeyPressed(KEYPAD_1, 1000);
		Sel.KeyPressed(KEYPAD_2, 2000);
		CHECK(Sel.Tick(3499).IsNone());
		CHECK(IsProgram(Sel.Tick(3500), 12));
	}

	// Wraparound of the millisecond count
	{
		CProgramSelector Sel;
		unsigned nStart = 0xFFFFFF00u;
		Sel.KeyPressed(KEYPAD_5, nStart);
		CHECK(Sel.Tick(nStart + 1499).IsNone());
		CHECK(IsProgram(Sel.Tick(nStart + 1500), 5));
	}

	// Custom timeout, and a timed-out number outside the range is dropped
	{
		CProgramSelector Sel(0, 500);
		Sel.KeyPressed(KEYPAD_7, 0);
		CHECK(Sel.Tick(499).IsNone());
		CHECK(IsProgram(Sel.Tick(500), 7));

		CProgramSelector Sel1(1, 500);				// 1-based: 0 is no program
		Sel1.KeyPressed(KEYPAD_0, 0);
		CHECK(Sel1.Tick(500).IsNone());
		CHECK(Sel1.EntryDigits() == 0);
	}

	// ---- + and -

	{
		CProgramSelector Sel;
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 1));
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 2));
		CHECK(IsProgram(Press(Sel, KEYPAD_MINUS), 1));
		CHECK(IsProgram(Press(Sel, KEYPAD_MINUS), 0));
		CHECK(IsProgram(Press(Sel, KEYPAD_MINUS), 0));		// clamped, re-sent
		CHECK(Sel.LastProgram() == 0);
	}

	{
		CProgramSelector Sel(0, 1500, 127);
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 127));		// clamped, re-sent
		CHECK(IsProgram(Press(Sel, KEYPAD_MINUS), 126));
	}

	// Initial program is clamped
	{
		CProgramSelector High(0, 1500, 200);
		CHECK(High.LastProgram() == 127);
		CProgramSelector Low(0, 1500, -5);
		CHECK(Low.LastProgram() == 0);
	}

	// They start from the last program sent, and drop a half-typed number
	{
		CProgramSelector Sel;
		CHECK(IsProgram(Type(Sel, "50"), 50));
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 51));

		Press(Sel, KEYPAD_1);					// half-typed
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 52));
		CHECK(Sel.EntryDigits() == 0);
		CHECK(Sel.Tick(g_nNow + 10000).IsNone());		// the 1 is really gone
	}

	// ---- first number 1 (entry is 1-based)

	{
		CProgramSelector Sel(1, 1500, 0, 1);
		CHECK(Press(Sel, KEYPAD_1).IsNone());
		CHECK(IsProgram(Press(Sel, KEYPAD_ENTER), 0));		// typed 1 -> wire 0
		CHECK(IsProgram(Type(Sel, "128"), 127));		// typed 128 -> wire 127
		CHECK(Type(Sel, "129").IsNone());			// rejected
		CHECK(Sel.LastProgram() == 127);

		CHECK(Press(Sel, KEYPAD_0).IsNone());
		CHECK(Press(Sel, KEYPAD_ENTER).IsNone());		// 0 is no program here
		CHECK(Sel.EntryDigits() == 0 && Sel.LastProgram() == 127);

		CHECK(IsProgram(Type(Sel, "13"), 12));			// 130 > 128: sent at once
		CHECK(Type(Sel, "12").IsNone());			// 120 <= 128: waits
		CHECK(IsProgram(Press(Sel, KEYPAD_ENTER), 11));

		Press(Sel, KEYPAD_5);
		CHECK(IsProgram(Sel.Tick(g_nNow + 1500), 4));
	}

	// ---- bank lock

	{
		CProgramSelector Sel;
		CHECK(!Sel.BankLock());
		Press(Sel, KEYPAD_1);
		CHECK(Press(Sel, KEYPAD_NUMLOCK).IsNone());
		CHECK(Sel.BankLock() && Sel.EntryDigits() == 0);	// entry discarded
		CHECK(Press(Sel, KEYPAD_NUMLOCK).IsNone());
		CHECK(!Sel.BankLock());
	}

	// A digit replaces the units digit and is sent at once
	{
		CProgramSelector Sel;
		CHECK(IsProgram(Type(Sel, "36"), 36));
		Press(Sel, KEYPAD_NUMLOCK);
		CHECK(IsProgram(Press(Sel, KEYPAD_8), 38));
		CHECK(Sel.LastProgram() == 38 && Sel.EntryDigits() == 0);
		CHECK(IsProgram(Press(Sel, KEYPAD_9), 39));
		CHECK(IsProgram(Press(Sel, KEYPAD_0), 30));		// the tens stay put
		CHECK(Sel.Tick(g_nNow + 100000).IsNone());		// no timeouts in this mode

		// ENTER and BS have nothing to act on; +/- move between banks
		CHECK(Press(Sel, KEYPAD_ENTER).IsNone());
		CHECK(Press(Sel, KEYPAD_BS).IsNone());
		CHECK(Sel.LastProgram() == 30);
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 31));
		CHECK(IsProgram(Press(Sel, KEYPAD_MINUS), 30));
	}

	// A digit that would leave 0..127 is ignored
	{
		CProgramSelector Sel;
		Type(Sel, "125");
		Press(Sel, KEYPAD_NUMLOCK);
		CHECK(Press(Sel, KEYPAD_8).IsNone());			// 128
		CHECK(Sel.LastProgram() == 125);
		CHECK(IsProgram(Press(Sel, KEYPAD_7), 127));
		CHECK(IsProgram(Press(Sel, KEYPAD_0), 120));
	}

	// With a first number of 1, the units digit is that of the typed number
	{
		CProgramSelector Sel(1, 1500, 0, 1);
		CHECK(IsProgram(Type(Sel, "36"), 35));			// typed 36
		Press(Sel, KEYPAD_NUMLOCK);
		CHECK(IsProgram(Press(Sel, KEYPAD_8), 37));		// typed 38
		CHECK(IsProgram(Press(Sel, KEYPAD_0), 29));		// typed 30

		CProgramSelector Low(1, 1500, 0, 1);			// at typed 1
		Press(Low, KEYPAD_NUMLOCK);
		CHECK(Low.LastProgram() == 0);
		CHECK(Press(Low, KEYPAD_0).IsNone());			// typed 0: no such program
		CHECK(IsProgram(Press(Low, KEYPAD_5), 4));		// typed 5
	}

	// ---- bank select (/)

	{
		CProgramSelector Sel;
		CHECK(Press(Sel, KEYPAD_SLASH).IsNone());
		CHECK(Sel.BankSelectPending());
		CHECK(IsBank(Press(Sel, KEYPAD_5), 5));
		CHECK(!Sel.BankSelectPending());
		CHECK(Sel.LastProgram() == 0);				// programs untouched

		CHECK(Press(Sel, KEYPAD_5).IsNone());			// back to normal entry
		CHECK(Sel.EntryDigits() == 1);
		Press(Sel, KEYPAD_BS);

		Press(Sel, KEYPAD_SLASH);
		CHECK(IsBank(Press(Sel, KEYPAD_0), 0));			// first bank 0: 0 is bank 0
		Press(Sel, KEYPAD_SLASH);
		CHECK(IsBank(Press(Sel, KEYPAD_9), 9));
	}

	// First bank 1: keys 1..9 are banks 0..8, key 0 is ignored
	{
		CProgramSelector Sel(0, 1500, 0, 1);
		Press(Sel, KEYPAD_SLASH);
		CHECK(IsBank(Press(Sel, KEYPAD_1), 0));
		Press(Sel, KEYPAD_SLASH);
		CHECK(IsBank(Press(Sel, KEYPAD_9), 8));
		Press(Sel, KEYPAD_SLASH);
		CHECK(Press(Sel, KEYPAD_0).IsNone());
		CHECK(!Sel.BankSelectPending());			// the digit was consumed
		CHECK(Sel.EntryDigits() == 0);
	}

	// The digit goes to the bank, not to program entry
	{
		CProgramSelector Sel;
		Press(Sel, KEYPAD_SLASH);
		Press(Sel, KEYPAD_3);
		CHECK(Sel.EntryDigits() == 0);
	}

	// Works in bank lock mode too
	{
		CProgramSelector Sel;
		Press(Sel, KEYPAD_NUMLOCK);
		Press(Sel, KEYPAD_SLASH);
		CHECK(IsBank(Press(Sel, KEYPAD_3), 3));
		CHECK(Sel.BankLock() && Sel.LastProgram() == 0);
	}

	// Pressed again it cancels; it also discards a half-typed number
	{
		CProgramSelector Sel;
		Press(Sel, KEYPAD_SLASH);
		Press(Sel, KEYPAD_SLASH);
		CHECK(!Sel.BankSelectPending());
		CHECK(Press(Sel, KEYPAD_4).IsNone());
		CHECK(Sel.EntryDigits() == 1);				// a normal digit again
		Press(Sel, KEYPAD_BS);

		Press(Sel, KEYPAD_1);
		Press(Sel, KEYPAD_SLASH);
		CHECK(Sel.EntryDigits() == 0 && Sel.BankSelectPending());
	}

	// BS, +, -, NUMLOCK cancel it
	{
		CProgramSelector Sel;
		Press(Sel, KEYPAD_SLASH);
		CHECK(Press(Sel, KEYPAD_BS).IsNone() && !Sel.BankSelectPending());

		Press(Sel, KEYPAD_SLASH);
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 1) && !Sel.BankSelectPending());

		Press(Sel, KEYPAD_SLASH);
		CHECK(IsProgram(Press(Sel, KEYPAD_MINUS), 0) && !Sel.BankSelectPending());

		Press(Sel, KEYPAD_SLASH);
		CHECK(Press(Sel, KEYPAD_NUMLOCK).IsNone());
		CHECK(!Sel.BankSelectPending() && Sel.BankLock());
	}

	// ENTER is not among the documented cancels: it leaves it pending
	{
		CProgramSelector Sel;
		Press(Sel, KEYPAD_SLASH);
		CHECK(Press(Sel, KEYPAD_ENTER).IsNone());
		CHECK(Sel.BankSelectPending());
	}

	// Timeout drops it without sending anything
	{
		CProgramSelector Sel;
		Sel.KeyPressed(KEYPAD_SLASH, 1000);
		CHECK(Sel.Tick(2499).IsNone() && Sel.BankSelectPending());
		CHECK(Sel.Tick(2500).IsNone() && !Sel.BankSelectPending());
		CHECK(Sel.KeyPressed(KEYPAD_5, 2600).IsNone());		// normal entry now
		CHECK(Sel.EntryDigits() == 1);
	}

	// ---- keys that mean nothing here

	{
		CProgramSelector Sel;
		Press(Sel, KEYPAD_1);
		CHECK(Press(Sel, KEYPAD_TAB).IsNone());
		CHECK(Press(Sel, KEYPAD_DEL).IsNone());
		CHECK(Press(Sel, KEYPAD_STAR).IsNone());
		CHECK(Press(Sel, 0x04).IsNone());
		CHECK(Sel.EntryDigits() == 1 && Sel.EntryNumber() == 1);	// undisturbed

		Press(Sel, KEYPAD_SLASH);
		Press(Sel, KEYPAD_TAB);
		CHECK(Sel.BankSelectPending());
	}

	// ---- Abort: for when another mode takes over the keys

	{
		// A half-typed number is dropped, not sent by a later Tick
		CProgramSelector Sel;
		Press(Sel, KEYPAD_1);
		Sel.Abort();
		CHECK(Sel.EntryDigits() == 0 && Sel.EntryNumber() == 0);
		CHECK(Sel.Tick(g_nNow + 100000).IsNone());
		CHECK(Press(Sel, KEYPAD_ENTER).IsNone());

		// A pending bank select is dropped
		Press(Sel, KEYPAD_SLASH);
		Sel.Abort();
		CHECK(!Sel.BankSelectPending());
		CHECK(Sel.Tick(g_nNow + 100000).IsNone());
		CHECK(Press(Sel, KEYPAD_3).IsNone() && Sel.EntryDigits() == 1);
		Sel.Abort();

		// Last program and bank lock are left alone
		CHECK(IsProgram(Type(Sel, "42"), 42));
		Press(Sel, KEYPAD_NUMLOCK);
		Sel.Abort();
		CHECK(Sel.BankLock() && Sel.LastProgram() == 42);
		CHECK(IsProgram(Press(Sel, KEYPAD_PLUS), 43));

		// Nothing to abort is fine
		Sel.Abort();
		Sel.Abort();
		CHECK(Sel.LastProgram() == 43);
	}

	// ---- invariants over a long pseudo-random key stream

	for (int nFirstNumber = 0; nFirstNumber <= 1; nFirstNumber++)
	for (int nFirstBank = 0; nFirstBank <= 1; nFirstBank++)
	{
		static const unsigned Keys[] =
		{
			KEYPAD_0, KEYPAD_1, KEYPAD_2, KEYPAD_3, KEYPAD_4, KEYPAD_5, KEYPAD_6,
			KEYPAD_7, KEYPAD_8, KEYPAD_9, KEYPAD_ENTER, KEYPAD_BS, KEYPAD_PLUS,
			KEYPAD_MINUS, KEYPAD_NUMLOCK, KEYPAD_SLASH, KEYPAD_TAB, KEYPAD_STAR
		};
		const unsigned nKeys = sizeof Keys / sizeof Keys[0];

		CProgramSelector Sel(nFirstNumber, 1500, 0, nFirstBank);

		unsigned nSeed = 12345 + nFirstNumber * 7 + nFirstBank * 13;
		unsigned nNow = 0xFFFF0000u;		// also crosses the wraparound
		bool bOk = true;

		for (int i = 0; i < 200000; i++)
		{
			nSeed = nSeed * 1664525u + 1013904223u;
			unsigned nRand = nSeed >> 8;
			nNow += nRand % 400;

			TAction A;
			if (nRand % 5 == 0)
				A = Sel.Tick(nNow);
			else
				A = Sel.KeyPressed(Keys[(nRand >> 4) % nKeys], nNow);

			if (A.Type == TAction::ActProgram && (A.Value < 0 || A.Value > 127))
				bOk = false;
			if (A.Type == TAction::ActBank && (A.Value < 0 || A.Value > 9 - nFirstBank))
				bOk = false;
			if (Sel.LastProgram() < 0 || Sel.LastProgram() > 127)
				bOk = false;
			if (Sel.EntryDigits() >= (unsigned) CProgramSelector::MaxDigits)
				bOk = false;		// a full entry is always sent at once
			if (Sel.EntryDigits() > 0 && Sel.BankSelectPending())
				bOk = false;		// never both at once
			if (Sel.EntryDigits() > 0 && Sel.BankLock())
				bOk = false;		// no typed entry in bank lock mode
		}

		CHECK(bOk);
	}

	printf(g_nFailed == 0 ? "all tests passed\n" : "%d FAILED\n", g_nFailed);
	return g_nFailed != 0;
}
