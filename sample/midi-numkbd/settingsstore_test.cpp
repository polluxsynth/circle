//
// settingsstore_test.cpp
//
// Host test for settingsstore.h:
//   g++ -std=c++17 -Wall -fsanitize=address,undefined settingsstore_test.cpp -o t && ./t
//
#include "settingsstore.h"

#include <cstdio>
#include <cstring>

static int g_nFailed = 0;

#define CHECK(cond)	do { if (!(cond)) { \
				printf("FAIL line %d: %s\n", __LINE__, #cond); \
				g_nFailed++; } } while (0)

typedef CSettingsStore S;
typedef S::TSettings TSettings;
typedef S::u8 u8;

// An MBR with one partition per (type, start) given; the rest unused
static void MakeMbr(u8 Mbr[S::SectorSize], const unsigned *pStarts,
		    const u8 *pTypes, unsigned nEntries)
{
	memset(Mbr, 0, S::SectorSize);
	for (unsigned i = 0; i < nEntries; i++) {
		u8 *p = Mbr + 446 + 16 * i;
		p[0] = i == 0 ? 0x80 : 0x00;
		p[4] = pTypes[i];
		for (int b = 0; b < 4; b++) {
			p[8 + b] = (pStarts[i] >> (8 * b)) & 0xFF;
			p[12 + b] = (100000u >> (8 * b)) & 0xFF;	// size
		}
	}
	Mbr[510] = 0x55;
	Mbr[511] = 0xAA;
}

int main(void)
{
	// CRC-32 check value, same as zlib's
	CHECK(S::CRC32((const u8 *) "123456789", 9) == 0xCBF43926u);

	// ---- round trip
	for (unsigned nCh = 0; nCh <= 16; nCh++) {
		u8 Sector[S::SectorSize];
		TSettings In, Out;
		In.MidiChannel = nCh;
		CHECK(S::Encode(In, Sector));
		Out.MidiChannel = 99;
		CHECK(S::Decode(Sector, Out) == S::ResultOk);
		CHECK(Out.MidiChannel == nCh);
	}

	// ---- fixed layout, so that old cards stay readable
	{
		u8 Sector[S::SectorSize];
		TSettings In;
		In.MidiChannel = 12;
		CHECK(S::Encode(In, Sector));
		CHECK(memcmp(Sector, "NKBD", 4) == 0);
		CHECK(Sector[4] == 1 && Sector[5] == 0);
		CHECK(Sector[6] == 1 && Sector[7] == 0);
		CHECK(Sector[8] == 12);
		unsigned nCRC = S::CRC32(Sector, 9);
		CHECK(Sector[9] == (nCRC & 0xFF) && Sector[12] == (nCRC >> 24));
		for (unsigned i = 13; i < S::SectorSize; i++)
			CHECK(Sector[i] == 0);
	}

	// ---- out of range values are refused, and Sector is left alone
	{
		u8 Sector[S::SectorSize];
		memset(Sector, 0xA5, sizeof Sector);
		TSettings In;
		In.MidiChannel = 17;
		CHECK(!S::Encode(In, Sector));
		CHECK(Sector[0] == 0xA5 && Sector[511] == 0xA5);
	}

	// ---- bad sectors: blank, and damage anywhere in the used part
	{
		u8 Sector[S::SectorSize];
		TSettings Out;
		Out.MidiChannel = 7;

		memset(Sector, 0, sizeof Sector);
		CHECK(S::Decode(Sector, Out) == S::ResultBlank);
		memset(Sector, 0xFF, sizeof Sector);
		CHECK(S::Decode(Sector, Out) == S::ResultBlank);
		CHECK(Out.MidiChannel == 7);		// untouched

		TSettings In;
		In.MidiChannel = 5;
		S::Encode(In, Sector);

		// Every single bit flip in the used 13 bytes is detected
		for (unsigned i = 0; i < 13; i++)
			for (int bit = 0; bit < 8; bit++) {
				u8 Bad[S::SectorSize];
				memcpy(Bad, Sector, sizeof Bad);
				Bad[i] ^= (u8) (1 << bit);
				CHECK(S::Decode(Bad, Out) != S::ResultOk);
				CHECK(Out.MidiChannel == 7);
			}

		// Bytes after the CRC are ignored
		Sector[200] = 0x55;
		CHECK(S::Decode(Sector, Out) == S::ResultOk && Out.MidiChannel == 5);
		Sector[200] = 0;

		// A future version is not guessed at
		Sector[4] = 2;
		CHECK(S::Decode(Sector, Out) == S::ResultBadVersion);
		Sector[4] = 1;

		// Wrong length, even with a matching CRC
		Sector[6] = 2;
		CHECK(S::Decode(Sector, Out) == S::ResultBadLength);
		Sector[6] = 1;

		// Intact but out of range (valid CRC computed over the bad value)
		Sector[8] = 99;
		Out.MidiChannel = 7;
		unsigned nCRC = S::CRC32(Sector, 9);
		Sector[9] = nCRC & 0xFF;
		Sector[10] = (nCRC >> 8) & 0xFF;
		Sector[11] = (nCRC >> 16) & 0xFF;
		Sector[12] = (nCRC >> 24) & 0xFF;
		CHECK(S::Decode(Sector, Out) == S::ResultBadValue);
		CHECK(Out.MidiChannel == 7);
	}

	// ---- the partition table check
	{
		u8 Mbr[S::SectorSize];
		const u8 Fat = 0x0C, Linux = 0x83, Gpt = 0xEE;

		unsigned Pi[] = {8192, 532480};
		u8 PiT[] = {Fat, Linux};
		MakeMbr(Mbr, Pi, PiT, 2);
		CHECK(S::CheckSector(Mbr, S::DefaultSector) == S::GuardOk);
		CHECK(S::CheckSector(Mbr, 8191) == S::GuardOk);
		CHECK(S::CheckSector(Mbr, 8192) == S::GuardOverlap);	// first sector of it
		CHECK(S::CheckSector(Mbr, 9000) == S::GuardOverlap);
		CHECK(S::CheckSector(Mbr, 0) == S::GuardReserved);

		// Entries aren't necessarily in order: any one may be the first
		unsigned Rev[] = {532480, 2048};
		MakeMbr(Mbr, Rev, PiT, 2);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardOk);
		CHECK(S::CheckSector(Mbr, 2048) == S::GuardOverlap);

		// Old-style start at sector 63
		unsigned Old[] = {63};
		MakeMbr(Mbr, Old, PiT, 1);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardOverlap);

		// A partition with start 0 is nonsense: refuse
		unsigned Zero[] = {0};
		MakeMbr(Mbr, Zero, PiT, 1);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardOverlap);

		// An unused entry (type 0) doesn't count, whatever its start
		unsigned Unused[] = {10, 8192};
		u8 UnusedT[] = {0x00, Fat};
		MakeMbr(Mbr, Unused, UnusedT, 2);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardOk);

		// GPT
		unsigned G[] = {1};
		u8 GT[] = {Gpt};
		MakeMbr(Mbr, G, GT, 1);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardGPT);

		// No signature; blank card; boot code instead of a table
		MakeMbr(Mbr, Pi, PiT, 2);
		Mbr[511] = 0;
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardNoMBR);
		memset(Mbr, 0, sizeof Mbr);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardNoMBR);
		MakeMbr(Mbr, Pi, PiT, 2);
		Mbr[446 + 16 * 3] = 0x42;		// not a boot flag
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardNoMBR);

		// Signed but with no partitions at all
		MakeMbr(Mbr, Pi, PiT, 0);
		CHECK(S::CheckSector(Mbr, 1024) == S::GuardOk);
	}

	if (g_nFailed == 0)
		printf("settingsstore: all tests passed\n");
	else
		printf("settingsstore: %d check(s) FAILED\n", g_nFailed);
	return g_nFailed == 0 ? 0 : 1;
}
