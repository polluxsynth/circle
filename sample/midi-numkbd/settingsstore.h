//
// settingsstore.h
//
// The on-disk format of the stored settings, and nothing else: no Circle
// dependencies, so the very same header is used by the firmware, by the
// host tool (nkstore.cpp) and by the host test (settingsstore_test.cpp).
//
// The settings live in one raw 512-byte sector of the SD card, in the gap
// between the partition table and the first partition. It is written in
// place: there is no protection against a write torn by loss of power (a
// torn write fails the CRC and the defaults are used instead).
//
// Layout (all multi-byte values little-endian):
//
//   offset  size  contents
//        0     4  magic, "NKBD"
//        4     1  format version (1)
//        5     1  reserved, 0
//        6     2  payload length N (1 for version 1)
//        8     N  payload (version 1: see below)
//      8+N     4  CRC-32 (as zlib's crc32) of bytes 0 .. 8+N-1
//    12+N  ...    unused, written as zero, ignored when reading
//
// Version 1 payload:
//        0     1  MIDI channel as the user numbers it, 1..16
//                 (0 = not stored: the cmdline.txt/default value is used)
//
// A new setting means a new version number; a reader that doesn't know
// the version treats the sector as "no stored settings".
//
// The sector number is a compile-time default (override with
// -DNKSTORE_SECTOR=n). CheckSector() looks at the MBR and says whether
// that sector is safe to use, i.e. lies before the first partition.
//
#ifndef _settingsstore_h
#define _settingsstore_h

#ifndef NKSTORE_SECTOR
#define NKSTORE_SECTOR	1024	// 512 KiB; Raspberry Pi images start
				// the first partition at 8192.
				// By default, Linux mkfs.vfat seems
				// to put the first partition at
				// 2048.
#endif

// Plain types, so that no system header is needed (Circle's u8/u32 and
// the host's <cstdint> are both avoided).
static_assert(sizeof(unsigned) == 4, "unsigned must be 32 bits");

class CSettingsStore
{
public:
	typedef unsigned char u8;
	typedef unsigned u32;

	enum {
		SectorSize = 512,
		DefaultSector = NKSTORE_SECTOR,
	};

	enum { FormatVersion = 1 };
	enum { PayloadLengthV1 = 1 };

	enum {
		OffMagic = 0,
		OffVersion = 4,
		OffLength = 6,
		OffPayload = 8,
	};

	enum { NotStored = 0 };

	// The settings that can be stored. Each one that is NotStored is
	// left as it was found (cmdline.txt or default).
	struct TSettings
	{
		unsigned MidiChannel;	// 1..16, or NotStored

		TSettings(void) : MidiChannel(NotStored) {}
	};

	enum TResult {
		ResultOk,		// valid, Out is filled in
		ResultBlank,		// no magic: never written (or not by us)
		ResultBadVersion,	// a version we don't know
		ResultBadLength,	// payload length impossible for the version
		ResultBadCRC,		// damaged (or a write torn by power loss)
		ResultBadValue,		// intact, but a value is out of range
	};

	enum TGuard {
		GuardOk,		// the sector is before every partition
		GuardNoMBR,		// no (plausible) MBR: layout unknown
		GuardGPT,		// GPT disk: not handled, refused
		GuardOverlap,		// a partition starts at or before it
		GuardReserved,		// sector 0 is the MBR itself
	};

	// Writes the settings into Sector. False, with Sector untouched, if
	// a value is out of range (NotStored is fine).
	static bool Encode(const TSettings &Settings, u8 Sector[SectorSize])
	{
		if (Settings.MidiChannel != NotStored && !ValidChannel(Settings.MidiChannel))
			return false;

		for (unsigned i = 0; i < SectorSize; i++)
			Sector[i] = 0;

		Sector[OffMagic + 0] = 'N';
		Sector[OffMagic + 1] = 'K';
		Sector[OffMagic + 2] = 'B';
		Sector[OffMagic + 3] = 'D';
		Sector[OffVersion] = FormatVersion;
		Sector[OffLength] = PayloadLengthV1 & 0xFF;
		Sector[OffLength + 1] = PayloadLengthV1 >> 8;
		Sector[OffPayload] = (u8) Settings.MidiChannel;

		unsigned nEnd = OffPayload + PayloadLengthV1;
		PutLE32(Sector + nEnd, CRC32(Sector, nEnd));
		return true;
	}

	static TResult Decode(const u8 Sector[SectorSize], TSettings &Out)
	{
		if (   Sector[OffMagic + 0] != 'N' || Sector[OffMagic + 1] != 'K'
		    || Sector[OffMagic + 2] != 'B' || Sector[OffMagic + 3] != 'D')
			return ResultBlank;

		if (Sector[OffVersion] != FormatVersion)
			return ResultBadVersion;

		unsigned nLength = Sector[OffLength] | (Sector[OffLength + 1] << 8);
		if (nLength != PayloadLengthV1)
			return ResultBadLength;

		unsigned nEnd = OffPayload + nLength;
		if (GetLE32(Sector + nEnd) != CRC32(Sector, nEnd))
			return ResultBadCRC;

		unsigned nChannel = Sector[OffPayload];
		if (nChannel != NotStored && !ValidChannel(nChannel))
			return ResultBadValue;

		Out.MidiChannel = nChannel;
		return ResultOk;
	}

	// Is nSector safe to use, judging by the MBR (sector 0)?
	static TGuard CheckSector(const u8 Mbr[SectorSize], unsigned nSector)
	{
		if (nSector == 0)
			return GuardReserved;

		if (Mbr[510] != 0x55 || Mbr[511] != 0xAA)
			return GuardNoMBR;

		for (unsigned i = 0; i < 4; i++) {
			const u8 *pEntry = Mbr + 446 + 16 * i;

			// Boot flag is 0x00 or 0x80 in a real partition table;
			// anything else is boot code or data, not an MBR.
			if (pEntry[0] != 0x00 && pEntry[0] != 0x80)
				return GuardNoMBR;
		}

		for (unsigned i = 0; i < 4; i++) {
			const u8 *pEntry = Mbr + 446 + 16 * i;
			u8 ucType = pEntry[4];

			if (ucType == 0x00)		// unused entry
				continue;
			if (ucType == 0xEE)		// GPT protective MBR
				return GuardGPT;
			if (GetLE32(pEntry + 8) <= nSector)
				return GuardOverlap;
		}

		return GuardOk;
	}

	static const char *ResultName(TResult Result)
	{
		switch (Result) {
		case ResultOk:		return "ok";
		case ResultBlank:	return "blank (no stored settings)";
		case ResultBadVersion:	return "unknown format version";
		case ResultBadLength:	return "bad payload length";
		case ResultBadCRC:	return "bad CRC (damaged)";
		case ResultBadValue:	return "value out of range";
		}
		return "?";
	}

	static const char *GuardName(TGuard Guard)
	{
		switch (Guard) {
		case GuardOk:		return "ok";
		case GuardNoMBR:	return "no valid MBR";
		case GuardGPT:		return "GPT partition table";
		case GuardOverlap:	return "sector is inside or after a partition";
		case GuardReserved:	return "sector 0 is the MBR";
		}
		return "?";
	}

	// CRC-32 as in zlib/gzip (reflected, polynomial 0xEDB88320), so
	// that it can be checked with any standard tool.
	static u32 CRC32(const u8 *pData, unsigned nLength)
	{
		u32 nCRC = 0xFFFFFFFFu;

		while (nLength--) {
			nCRC ^= *pData++;
			for (int bit = 0; bit < 8; bit++)
				nCRC = (nCRC >> 1) ^ (0xEDB88320u & (0u - (nCRC & 1)));
		}

		return ~nCRC;
	}

	static bool ValidChannel(unsigned nChannel)
	{
		return nChannel >= 1 && nChannel <= 16;
	}

private:
	static u32 GetLE32(const u8 *p)
	{
		return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24);
	}

	static void PutLE32(u8 *p, u32 n)
	{
		p[0] = n & 0xFF;
		p[1] = (n >> 8) & 0xFF;
		p[2] = (n >> 16) & 0xFF;
		p[3] = (n >> 24) & 0xFF;
	}
};

#endif
