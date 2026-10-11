//
// nkstore.cpp
//
// Host tool: shows, sets or erases the settings stored in a raw sector of the
// SD card (or of an image of it), using the same header as the firmware
// (settingsstore.h), so the two can't disagree about the format.
//
//   nkstore [-s sector] show  <device|image>
//   nkstore [-s sector] set   <device|image> -c <1..16> [-f]
//   nkstore [-s sector] erase <device|image> [-f]
//
// show exits with 0 if valid settings were found, 2 if the sector holds
// none (blank, damaged, ...), and 1 on a usage or I/O error.
//
// set and erase write, so they need write access to the device (sudo), and
// refuse unless the partition table says that the sector is before the first
// partition. -f overrides that, at your own risk.
//
// Build:  g++ -std=c++17 -Wall -O2 nkstore.cpp -o nkstore   (or: make nkstore)
//
#define _FILE_OFFSET_BITS 64
#include "settingsstore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

typedef CSettingsStore S;
typedef S::u8 u8;

static void Usage(void)
{
	fprintf(stderr,
		"usage: nkstore [-s sector] show  <device|image>\n"
		"       nkstore [-s sector] set   <device|image> -c <1..16> [-f]\n"
		"       nkstore [-s sector] erase <device|image> [-f]\n"
		"  -s sector   sector to use (default %u, as in the firmware)\n"
		"  -c channel  MIDI channel to store\n"
		"  -f          write even if the partition check fails\n",
		(unsigned) S::DefaultSector);
}

static bool ReadSector(int fd, unsigned nSector, u8 *pBuf)
{
	ssize_t n = pread(fd, pBuf, S::SectorSize, (off_t) nSector * S::SectorSize);
	return n == (ssize_t) S::SectorSize;
}

static bool WriteSector(int fd, unsigned nSector, const u8 *pBuf)
{
	ssize_t n = pwrite(fd, pBuf, S::SectorSize, (off_t) nSector * S::SectorSize);
	return n == (ssize_t) S::SectorSize && fsync(fd) == 0;
}

static void ShowPartitions(const u8 *pMbr)
{
	for (unsigned i = 0; i < 4; i++) {
		const u8 *p = pMbr + 446 + 16 * i;
		if (p[4] == 0)
			continue;
		unsigned nStart = p[8] | (p[9] << 8) | (p[10] << 16) | ((unsigned) p[11] << 24);
		unsigned nSize = p[12] | (p[13] << 8) | (p[14] << 16) | ((unsigned) p[15] << 24);
		printf("  partition %u: type 0x%02X, starts at sector %u, %u sectors\n",
		       i + 1, p[4], nStart, nSize);
	}
}

int main(int argc, char **argv)
{
	unsigned nSector = S::DefaultSector;
	int nChannel = -1;
	bool bForce = false;

	int c;
	while ((c = getopt(argc, argv, "s:c:fh")) != -1) {
		char *pEnd;
		switch (c) {
		case 's':
			nSector = (unsigned) strtoul(optarg, &pEnd, 10);
			if (*optarg == '\0' || *pEnd != '\0') {
				Usage();
				return 1;
			}
			break;
		case 'c':
			nChannel = (int) strtol(optarg, &pEnd, 10);
			if (*optarg == '\0' || *pEnd != '\0') {
				Usage();
				return 1;
			}
			break;
		case 'f':
			bForce = true;
			break;
		default:
			Usage();
			return 1;
		}
	}

	if (argc - optind != 2) {
		Usage();
		return 1;
	}
	const char *pCommand = argv[optind];
	const char *pPath = argv[optind + 1];

	bool bShow = strcmp(pCommand, "show") == 0;
	bool bSet = strcmp(pCommand, "set") == 0;
	bool bErase = strcmp(pCommand, "erase") == 0;
	if (!bShow && !bSet && !bErase) {
		Usage();
		return 1;
	}
	if (bSet && !S::ValidChannel((unsigned) nChannel)) {
		fprintf(stderr, "set needs -c with a MIDI channel 1..16\n");
		return 1;
	}

	int fd = open(pPath, bShow ? O_RDONLY : O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: %s\n", pPath, strerror(errno));
		return 1;
	}

	u8 Mbr[S::SectorSize];
	if (!ReadSector(fd, 0, Mbr)) {
		fprintf(stderr, "%s: can't read the MBR\n", pPath);
		return 1;
	}

	S::TGuard Guard = S::CheckSector(Mbr, nSector);

	u8 Sector[S::SectorSize];
	bool bRead = ReadSector(fd, nSector, Sector);
	S::TSettings Settings;
	S::TResult Result = S::ResultBlank;
	if (bRead)
		Result = S::Decode(Sector, Settings);

	if (bShow) {
		printf("%s, sector %u (byte offset %llu)\n", pPath, nSector,
		       (unsigned long long) nSector * S::SectorSize);
		ShowPartitions(Mbr);
		printf("partition check: %s\n", S::GuardName(Guard));

		if (!bRead) {
			printf("stored settings: sector can't be read (device or image too small?)\n");
			return 2;
		}

		printf("stored settings: %s\n", S::ResultName(Result));
		if (Result != S::ResultOk)
			return 2;

		if (Settings.MidiChannel == S::NotStored)
			printf("  midi_channel: (not stored)\n");
		else
			printf("  midi_channel: %u\n", Settings.MidiChannel);
		return 0;
	}

	if (Guard != S::GuardOk && !bForce) {
		fprintf(stderr, "refusing to write sector %u: %s (-f overrides)\n",
			nSector, S::GuardName(Guard));
		return 1;
	}

	if (bSet) {
		// Keep whatever else is stored (nothing else yet)
		if (Result != S::ResultOk)
			Settings = S::TSettings();
		Settings.MidiChannel = (unsigned) nChannel;
		if (!S::Encode(Settings, Sector)) {
			fprintf(stderr, "value out of range\n");
			return 1;
		}
	} else {
		memset(Sector, 0, sizeof Sector);
	}

	if (!WriteSector(fd, nSector, Sector)) {
		fprintf(stderr, "%s: write failed: %s\n", pPath, strerror(errno));
		return 1;
	}

	if (bSet)
		printf("stored midi_channel %d\n", nChannel);
	else
		printf("erased\n");
	close(fd);
	return 0;
}
