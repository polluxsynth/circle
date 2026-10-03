//
// keyreports_test.cpp
//
// Host test for keyreports.h:
//   g++ -std=c++17 -Wall -fsanitize=address,undefined keyreports_test.cpp -o t && ./t
//
#include "keyreports.h"

#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

static int g_nFailed = 0;

#define CHECK(cond)	do { if (!(cond)) { \
				printf("FAIL line %d: %s\n", __LINE__, #cond); \
				g_nFailed++; } } while (0)

typedef std::vector<std::pair<unsigned, bool>> TEvents;	// (key, shifted)

// Feeds one report (unused slots are 0) and returns the events it produced
static TEvents Feed(CKeyReportDecoder &Dec, std::initializer_list<unsigned char> Keys)
{
	unsigned char Report[6] = {0, 0, 0, 0, 0, 0};
	unsigned i = 0;
	for (unsigned char Key : Keys)
		Report[i++] = Key;

	TEvents Events;
	Dec.Process(Report, [&Events](unsigned char ucKey, bool bShift)
	{
		Events.push_back(std::make_pair((unsigned) ucKey, bShift));
	});
	return Events;
}

int main(void)
{
	// ---- plain presses

	{
		CKeyReportDecoder Dec;
		CHECK(Feed(Dec, {}).empty());				// nothing held

		TEvents E = Feed(Dec, {KEYPAD_5});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_5 && !E[0].second);

		CHECK(Feed(Dec, {KEYPAD_5}).empty());			// still held: no repeat
		CHECK(Feed(Dec, {KEYPAD_5}).empty());
		CHECK(Feed(Dec, {}).empty());				// release: no event

		E = Feed(Dec, {KEYPAD_5});				// pressed again
		CHECK(E.size() == 1 && E[0].first == KEYPAD_5);
	}

	// Two keys down in one report: both reported, in report order
	{
		CKeyReportDecoder Dec;
		TEvents E = Feed(Dec, {KEYPAD_3, KEYPAD_7});
		CHECK(E.size() == 2);
		CHECK(E[0].first == KEYPAD_3 && E[1].first == KEYPAD_7);
	}

	// A second key while the first is held: only the new one is reported
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_1});
		TEvents E = Feed(Dec, {KEYPAD_1, KEYPAD_2});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_2);

		// The first is released, the second stays: nothing new
		CHECK(Feed(Dec, {KEYPAD_2}).empty());
	}

	// Keys may move between slots in the report without looking new
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_1, KEYPAD_2});
		CHECK(Feed(Dec, {KEYPAD_2, KEYPAD_1}).empty());		// swapped
		CHECK(Feed(Dec, {0, 0, KEYPAD_1, KEYPAD_2}).empty());	// moved
		TEvents E = Feed(Dec, {KEYPAD_9, 0, KEYPAD_1, KEYPAD_2});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_9);
	}

	// A full report of new keys
	{
		CKeyReportDecoder Dec;
		TEvents E = Feed(Dec, {0x04, 0x05, 0x06, 0x07, 0x08, 0x09});
		CHECK(E.size() == 6);
		CHECK(E[0].first == 0x04 && E[5].first == 0x09);	// not only keypad codes
	}

	// ---- * as shift

	// * alone gives no event
	{
		CKeyReportDecoder Dec;
		CHECK(Feed(Dec, {KEYPAD_STAR}).empty());
		CHECK(Feed(Dec, {KEYPAD_STAR}).empty());
		CHECK(Feed(Dec, {}).empty());
	}

	// * down first, then a key: shifted
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_STAR});
		TEvents E = Feed(Dec, {KEYPAD_STAR, KEYPAD_1});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_1 && E[0].second);

		// Release the key, press another with * still held: shifted too
		Feed(Dec, {KEYPAD_STAR});
		E = Feed(Dec, {KEYPAD_STAR, KEYPAD_2});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_2 && E[0].second);

		// * released: the next key is plain again
		Feed(Dec, {});
		E = Feed(Dec, {KEYPAD_3});
		CHECK(E.size() == 1 && !E[0].second);
	}

	// Slot order within the report doesn't matter
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_STAR});
		TEvents E = Feed(Dec, {KEYPAD_4, KEYPAD_STAR});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_4 && E[0].second);
	}

	// * pressed together with a key (same report): not shifted
	{
		CKeyReportDecoder Dec;
		TEvents E = Feed(Dec, {KEYPAD_STAR, KEYPAD_1});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_1 && !E[0].second);
	}

	// Key first, then *: no new event, and the key is not retroactively shifted
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_1});
		CHECK(Feed(Dec, {KEYPAD_1, KEYPAD_STAR}).empty());
		CHECK(Feed(Dec, {KEYPAD_1, KEYPAD_STAR}).empty());

		// Releasing and re-pressing the key with * held is shifted
		Feed(Dec, {KEYPAD_STAR});
		TEvents E = Feed(Dec, {KEYPAD_STAR, KEYPAD_1});
		CHECK(E.size() == 1 && E[0].second);
	}

	// A key pressed in the report where * is let go: not shifted
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_STAR});
		TEvents E = Feed(Dec, {KEYPAD_2});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_2 && !E[0].second);
	}

	// Two keys in the report after *: both shifted
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_STAR});
		TEvents E = Feed(Dec, {KEYPAD_STAR, KEYPAD_1, KEYPAD_2});
		CHECK(E.size() == 2 && E[0].second && E[1].second);
	}

	// * itself never appears as an event, however it is pressed
	{
		CKeyReportDecoder Dec;
		CHECK(Feed(Dec, {KEYPAD_STAR, KEYPAD_STAR}).empty());	// odd, but safe
		CHECK(Feed(Dec, {}).empty());
		TEvents E = Feed(Dec, {KEYPAD_1, KEYPAD_STAR});
		for (unsigned i = 0; i < E.size(); i++)
			CHECK(E[i].first != KEYPAD_STAR);
	}

	// ---- error and unused codes

	// 0 = none, 2 and 3 = error codes: skipped, others still found
	{
		CKeyReportDecoder Dec;
		TEvents E = Feed(Dec, {0, 2, 3, KEYPAD_5});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_5);
	}

	// Rollover error (usage 1): the whole report is ignored...
	{
		CKeyReportDecoder Dec;
		CHECK(Feed(Dec, {1, 1, 1, 1, 1, 1}).empty());
		CHECK(Feed(Dec, {KEYPAD_5, 1}).empty());		// even with a real key in it
	}

	// ...and doesn't disturb the held-key state
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_5});
		CHECK(Feed(Dec, {1, 1, 1, 1, 1, 1}).empty());
		CHECK(Feed(Dec, {KEYPAD_5}).empty());			// 5 is not "pressed again"

		Feed(Dec, {KEYPAD_5});
		Feed(Dec, {1, 1, 1, 1, 1, 1});
		TEvents E = Feed(Dec, {KEYPAD_5, KEYPAD_6});
		CHECK(E.size() == 1 && E[0].first == KEYPAD_6);	// only the new key
	}

	// * held across a rollover error is still remembered
	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_STAR});
		Feed(Dec, {1, 1, 1, 1, 1, 1});
		TEvents E = Feed(Dec, {KEYPAD_STAR, KEYPAD_1});
		CHECK(E.size() == 1 && E[0].second);
	}

	// ---- Reset

	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_5});
		Dec.Reset();
		TEvents E = Feed(Dec, {KEYPAD_5});			// seen as new again
		CHECK(E.size() == 1 && E[0].first == KEYPAD_5);
	}

	{
		CKeyReportDecoder Dec;
		Feed(Dec, {KEYPAD_STAR});
		Dec.Reset();						// a new keyboard
		TEvents E = Feed(Dec, {KEYPAD_STAR, KEYPAD_1});
		CHECK(E.size() == 1 && !E[0].second);			// * counts as just pressed
	}

	printf(g_nFailed == 0 ? "all tests passed\n" : "%d FAILED\n", g_nFailed);
	return g_nFailed != 0;
}
