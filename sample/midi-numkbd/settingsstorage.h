//
// settingsstorage.h
//
// Reads and writes the stored settings (format: settingsstore.h) in a raw
// sector of the SD card, through Circle's EMMC driver (addon/SDCard).
//
// Initialize() reads the MBR and only lets the storage be used if the sector
// lies before the first partition, so that a card with an unexpected layout
// can never have a filesystem overwritten. Load() and Save() block for
// a few milliseconds: call them from the main loop only, never from IRQ
// context.
//
#ifndef _settingsstorage_h
#define _settingsstorage_h

#include <SDCard/emmc.h>
#include <circle/logger.h>
#include <circle/types.h>

#include "settingsstore.h"

class CSettingsStorage
{
public:
	typedef CSettingsStore::TSettings TSettings;

	CSettingsStorage(CEMMCDevice *pEMMC,
			 unsigned nSector = CSettingsStore::DefaultSector)
	:	m_pEMMC(pEMMC),
		m_nSector(nSector),
		m_bUsable(false)
	{
	}

	// Checks the partition table; call once after the EMMC device is
	// initialised. Logs the reason if the storage can't be used.
	bool Initialize(void)
	{
		static const char From[] = "store";

		if (!ReadSector(0, m_Buffer)) {
			LOGWARN("Can't read the MBR: settings can't be stored");
			return false;
		}

		CSettingsStore::TGuard Guard = CSettingsStore::CheckSector(m_Buffer, m_nSector);
		if (Guard != CSettingsStore::GuardOk) {
			LOGWARN("Sector %u not usable for settings (%s)",
				m_nSector, CSettingsStore::GuardName(Guard));
			return false;
		}

		m_bUsable = true;
		return true;
	}

	bool Usable(void) const		{ return m_bUsable; }

	// Reads the stored settings. Anything but ResultOk leaves Out alone.
	CSettingsStore::TResult Load(TSettings &Out)
	{
		if (!m_bUsable || !ReadSector(m_nSector, m_Buffer))
			return CSettingsStore::ResultBlank;

		return CSettingsStore::Decode(m_Buffer, Out);
	}

	// Writes the settings; false if that failed.
	bool Save(const TSettings &Settings)
	{
		if (!m_bUsable || !CSettingsStore::Encode(Settings, m_Buffer))
			return false;

		return WriteSector(m_nSector, m_Buffer);
	}

private:
	bool ReadSector(unsigned nSector, u8 *pBuffer)
	{
		m_pEMMC->Seek((u64) nSector * CSettingsStore::SectorSize);
		return m_pEMMC->Read(pBuffer, CSettingsStore::SectorSize)
			== (int) CSettingsStore::SectorSize;
	}

	bool WriteSector(unsigned nSector, const u8 *pBuffer)
	{
		m_pEMMC->Seek((u64) nSector * CSettingsStore::SectorSize);
		return m_pEMMC->Write(pBuffer, CSettingsStore::SectorSize)
			== (int) CSettingsStore::SectorSize;
	}

private:
	CEMMCDevice *m_pEMMC;
	unsigned m_nSector;
	bool m_bUsable;

	// The EMMC driver wants a word-aligned buffer
	u8 m_Buffer[CSettingsStore::SectorSize] __attribute__((aligned(4)));
};

#endif
