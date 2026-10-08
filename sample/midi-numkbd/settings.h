//
// settings.h
//
// Run-time configuration, read from cmdline.txt on the SD card. Each
// setting is a "name=value" word on the (single) line of cmdline.txt,
// separated from the other words by spaces:
//
//   program_base=1 bank_base=1 bank_cc=32 midi_channel=1
//
//   program_base   0 or 1   number the user types for MIDI program 0
//                           (default 1: typing 1..128 sends 0..127)
//   bank_base      0 or 1   digit the user presses for bank wire value 0
//                           (default 1: keys 1..9 send 0..8, key 0 ignored)
//   bank_cc        0..127   controller number used for bank select
//                           (default 32, Bank Select LSB)
//   midi_channel   1..16    MIDI channel as numbered on the device
//                           (default 1)
//
// A setting that is absent gets its default. One that is present but not a
// plain decimal number in range is also replaced by its default. Report()
// logs each setting with its value and which of the three cases applied
// (the logger isn't up yet when the settings are read).
//
#ifndef _settings_h
#define _settings_h

#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/types.h>

class CSettings
{
public:
	enum {
		DefaultProgramBase = 1,
		DefaultBankBase = 1,
		DefaultBankCC = 32,
		DefaultMidiChannel = 1,	// as the user sees it, 1..16
		MinMidiChannel = 1,
		MaxMidiChannel = 16,
	};

	// Wire-level values, ready to use
	int FirstProgramNumber;		// 0 or 1
	int FirstBankNumber;		// 0 or 1
	u8 BankSelectCC;		// 0..127
	unsigned MidiChannel;		// 0..15 (0 is "channel 1")

	explicit CSettings(const CKernelOptions &Options)
	:	m_nInfo(0)
	{
		FirstProgramNumber = (int) Get(Options, "program_base", 0, 1,
					       DefaultProgramBase);
		FirstBankNumber = (int) Get(Options, "bank_base", 0, 1,
					    DefaultBankBase);
		BankSelectCC = (u8) Get(Options, "bank_cc", 0, 127,
					DefaultBankCC);
		MidiChannel = Get(Options, "midi_channel", MinMidiChannel,
				  MaxMidiChannel, DefaultMidiChannel) - 1;
	}

	// The MIDI channel as the user numbers it, 1..16
	unsigned MidiChannelNumber(void) const
	{
		return MidiChannel + 1;
	}

	// Changes the MIDI channel while running (the settings mode does
	// this). nChannel is 1..16; returns false, changing nothing, if not.
	bool SetMidiChannel(unsigned nChannel)
	{
		if (nChannel < (unsigned) MinMidiChannel || nChannel > (unsigned) MaxMidiChannel)
			return false;

		MidiChannel = nChannel - 1;
		return true;
	}

	// Logs each setting with its value and where it came from. Call once
	// the logger has been initialised.
	void Report(void) const
	{
		static const char From[] = "settings";

		for (unsigned i = 0; i < m_nInfo; i++) {
			const TInfo &Info = m_Info[i];

			switch (Info.Source) {
			case FromCmdline:
				LOGNOTE("%s = %d (from cmdline.txt)",
					Info.pName, Info.nValue);
				break;

			case FromDefault:
				LOGNOTE("%s = %d (default; not found in the command line)",
					Info.pName, Info.nValue);
				break;

			case Rejected:
				LOGWARN("%s = %d (default; \"%s\" in the command line is not valid)",
					Info.pName, Info.nValue, Info.pRaw);
				break;
			}
		}
	}

private:
	enum { MaxInfo = 4 };		// one per setting
	enum TSource { FromDefault, FromCmdline, Rejected };

	struct TInfo
	{
		const char *pName;	// string literal
		const char *pRaw;	// text after '=', owned by the CKernelOptions
		int nValue;		// the value in force, as the user writes it
		TSource Source;
	};

	// The value of the option, or nDefault if it is absent, or present
	// but not a decimal number within nMin..nMax. Either way, how it was
	// arrived at is noted for Report().
	unsigned Get(const CKernelOptions &Options, const char *pName,
		     unsigned nMin, unsigned nMax, unsigned nDefault)
	{
		const char *pRaw = Options.GetAppOptionString(pName);
		unsigned nValue = nDefault;
		TSource Source = FromDefault;

		if (pRaw != nullptr) {
			const unsigned Invalid = (unsigned) -1;
			unsigned nParsed = Options.GetAppOptionDecimal(pName, Invalid);

			if (nParsed != Invalid && nParsed >= nMin && nParsed <= nMax) {
				nValue = nParsed;
				Source = FromCmdline;
			} else {
				Source = Rejected;
			}
		}

		if (m_nInfo < MaxInfo) {
			TInfo &Info = m_Info[m_nInfo++];
			Info.pName = pName;
			Info.pRaw = pRaw;
			Info.nValue = (int) nValue;
			Info.Source = Source;
		}

		return nValue;
	}

private:
	TInfo m_Info[MaxInfo];
	unsigned m_nInfo;
};

#endif
