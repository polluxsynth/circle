//
// blinkseq_test.cpp
//
// Host test for blinkseq.h (settingsmode_test.cpp has a few more checks of
// the two sequences, made together with the settings mode):
//   g++ -std=c++17 -Wall -fsanitize=address,undefined blinkseq_test.cpp -o t && ./t
//
#include "blinkseq.h"

#include <cstdio>

static int g_nFailed = 0;

#define CHECK(cond)	do { if (!(cond)) { \
				printf("FAIL line %d: %s\n", __LINE__, #cond); \
				g_nFailed++; } } while (0)

// What "accepted" should look like at t ms after the start: 4 times
// 60 ms dark then 60 ms lit.
static bool AcceptedOn(unsigned t)
{
	return t % 120 >= 60;
}

int main(void)
{
	// ---- nothing built

	{
		CBlinkSequence B;
		CHECK(B.TotalMs() == 0);
		CHECK(!B.Active(0) && !B.Active(1000));

		B.Start(500);				// starting an empty sequence plays nothing
		CHECK(!B.Active(500) && !B.Active(501));
	}

	// ---- accepted: every millisecond

	{
		CBlinkSequence B;
		B.BuildAccepted();
		B.Start(10000);
		CHECK(B.TotalMs() == 480);

		bool bOk = true;
		for (unsigned t = 0; t < 480; t++)
		{
			if (!B.Active(10000 + t))
				bOk = false;
			if (B.IsOn(10000 + t) != AcceptedOn(t))
				bOk = false;
		}
		CHECK(bOk);
		CHECK(!B.Active(10000 + 480));
		CHECK(!B.Active(10000 + 100000));
	}

	// ---- rejected: dark throughout

	{
		CBlinkSequence B;
		B.BuildRejected();
		B.Start(0);
		CHECK(B.TotalMs() == 1200);

		bool bOk = true;
		for (unsigned t = 0; t < 1200; t++)
			if (!B.Active(t) || B.IsOn(t))
				bOk = false;
		CHECK(bOk);
		CHECK(!B.Active(1200));
	}

	// ---- before the start time it is not active

	{
		CBlinkSequence B;
		B.BuildAccepted();
		B.Start(5000);
		CHECK(!B.Active(4999));
		CHECK(!B.Active(0));
		CHECK(B.Active(5000));
	}

	// ---- the same pattern whatever the start time, including across wraparound

	{
		static const unsigned Starts[] =
		{
			0u, 1u, 123456u, 0x7FFFFFF0u, 0xFFFFFE00u, 0xFFFFFFC4u, 0xFFFFFFFFu
		};

		bool bOk = true;
		for (unsigned s = 0; s < sizeof Starts / sizeof Starts[0]; s++)
		{
			CBlinkSequence B;
			B.BuildAccepted();
			B.Start(Starts[s]);

			for (unsigned t = 0; t < 500; t++)
			{
				unsigned nNow = Starts[s] + t;		// wraps by design

				if (B.Active(nNow) != (t < 480))
					bOk = false;
				if (t < 480 && B.IsOn(nNow) != AcceptedOn(t))
					bOk = false;
			}
		}
		CHECK(bOk);
	}

	// ---- restarting, rebuilding and clearing

	{
		CBlinkSequence B;
		B.BuildAccepted();
		B.Start(1000);
		CHECK(!B.Active(1480));				// finished
		B.Start(2000);					// played again
		CHECK(B.Active(2000) && B.Active(2479) && !B.Active(2480));
		CHECK(!B.IsOn(2000) && B.IsOn(2060));

		// Restart in the middle of playing begins from the top
		B.Start(2100);
		CHECK(!B.IsOn(2100) && B.IsOn(2160));

		// Building again replaces the sequence; it doesn't append
		B.BuildAccepted();
		CHECK(B.TotalMs() == 480);
		B.BuildRejected();
		CHECK(B.TotalMs() == 1200);
		B.BuildAccepted();
		CHECK(B.TotalMs() == 480);

		// Clear stops it at once, and it stays stopped
		B.Start(3000);
		CHECK(B.Active(3100));
		B.Clear();
		CHECK(!B.Active(3100) && !B.Active(3000));
		CHECK(B.TotalMs() == 0);
	}

	// ---- two sequences don't share state

	{
		CBlinkSequence A, B;
		A.BuildAccepted();
		A.Start(0);
		B.BuildRejected();
		B.Start(0);
		CHECK(A.TotalMs() == 480 && B.TotalMs() == 1200);
		CHECK(A.IsOn(60) && !B.IsOn(60));
	}

	printf(g_nFailed == 0 ? "all tests passed\n" : "%d FAILED\n", g_nFailed);
	return g_nFailed != 0;
}
