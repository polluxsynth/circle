//
// spscqueue.h
//
// Fixed-size lock-free queue for exactly ONE producer context (e.g. an IRQ
// callback) and exactly ONE consumer context (e.g. the main loop).
// N must be a power of two. Not safe for multiple producers or consumers.
// T should be trivially copyable.
//
// On a single-core Raspberry Pi a compiler barrier plus volatile indices is
// sufficient. If this is ever used across cores, replace Barrier() with
// DataMemBarrier() from <circle/synchronize.h>.
//
#ifndef _spscqueue_h
#define _spscqueue_h

template <typename T, unsigned N>
class CSpscQueue
{
	static_assert (N > 0 && (N & (N - 1)) == 0, "N must be a power of two");

public:
	CSpscQueue(void) : m_nHead(0), m_nTail(0), m_nDropped(0) {}

	// --- producer side only ---

	// Returns false (and counts a drop) if the queue is full.
	bool Push(const T &Item)
	{
		unsigned nHead = m_nHead;
		if (nHead - m_nTail >= N)	// full
		{
			// ++ on volatiles is deprecated in C++20
			m_nDropped = m_nDropped + 1;
			return false;
		}

		m_Data[nHead & (N - 1)] = Item;
		Barrier();			// data must be visible before the index
		m_nHead = nHead + 1;

		return true;
	}

	// --- consumer side only ---

	// Returns false if the queue is empty.
	bool Pop(T &Item)
	{
		unsigned nTail = m_nTail;
		if (nTail == m_nHead)		// empty
			return false;

		Barrier();
		Item = m_Data[nTail & (N - 1)];
		Barrier();			// finish reading before freeing the slot
		m_nTail = nTail + 1;

		return true;
	}

	// --- either side (informational) ---

	bool IsEmpty(void) const	{ return m_nTail == m_nHead; }
	unsigned Count(void) const	{ return m_nHead - m_nTail; }
	unsigned Dropped(void) const	{ return m_nDropped; }

private:
	static void Barrier(void)	{ asm volatile("" ::: "memory"); }

private:
	T m_Data[N];

	// Indices run freely and wrap at 2^32 (harmless, as N divides 2^32);
	// they are masked only when indexing, so all N slots are usable.
	volatile unsigned m_nHead;	// written by producer only
	volatile unsigned m_nTail;	// written by consumer only
	volatile unsigned m_nDropped;	// written by producer only
};

#endif
