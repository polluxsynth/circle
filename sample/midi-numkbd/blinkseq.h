//
// blinkseq.h
//
// A one-shot LED sequence: a list of timed on/off segments, played once
// from a start time. Used for the acknowledgements of the settings mode,
// since the NumLock LED is the only output there is.
// Header-only and free of Circle dependencies so it can be tested on a
// host machine.
//
// The caller asks Active() whether the sequence is still playing, and if
// so IsOn() what the LED should be doing; when it is not playing, the LED
// goes back to whatever its normal pattern is.
//
// The sequences:
//
//   accepted   a rapid flicker: 4 flashes of 60 ms (half a second)
//   rejected   dark for 1.2 seconds
//
#ifndef _blinkseq_h
#define _blinkseq_h

class CBlinkSequence
{
public:
	enum { MaxSegments = 16 };

	CBlinkSequence(void)
	:	m_nCount(0),
		m_nTotalMs(0),
		m_nStartMs(0)
	{
	}

	void Clear(void)
	{
		m_nCount = 0;
		m_nTotalMs = 0;
	}

	void BuildAccepted(void)
	{
		Clear();
		for (int i = 0; i < 4; i++)
		{
			Add(false, 60);
			Add(true, 60);
		}
	}

	void BuildRejected(void)
	{
		Clear();
		Add(false, 1200);
	}

	// Starts playing what was built, at nNowMs (any free-running
	// millisecond count; wraparound is fine).
	void Start(unsigned nNowMs)
	{
		m_nStartMs = nNowMs;
	}

	bool Active(unsigned nNowMs) const
	{
		return    m_nCount > 0
		       && (unsigned) (nNowMs - m_nStartMs) < m_nTotalMs;
	}

	// What the LED should be doing now; only meaningful if Active()
	bool IsOn(unsigned nNowMs) const
	{
		unsigned nElapsedMs = nNowMs - m_nStartMs;

		for (unsigned i = 0; i < m_nCount; i++)
		{
			if (nElapsedMs < m_Segment[i].nMs)
				return m_Segment[i].bOn;

			nElapsedMs -= m_Segment[i].nMs;
		}

		return true;
	}

	unsigned TotalMs(void) const	{ return m_nTotalMs; }

private:
	void Add(bool bOn, unsigned nMs)
	{
		if (m_nCount >= MaxSegments)
			return;

		m_Segment[m_nCount].bOn = bOn;
		m_Segment[m_nCount].nMs = nMs;
		m_nCount++;
		m_nTotalMs += nMs;
	}

private:
	struct TSegment
	{
		bool bOn;
		unsigned nMs;
	};

	TSegment m_Segment[MaxSegments];
	unsigned m_nCount;
	unsigned m_nTotalMs;
	unsigned m_nStartMs;
};

#endif
