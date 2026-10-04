// Raw key codes for USB numeric keypad

#ifndef _RAWKEYS_H_
#define _RAWKEYS_H_

enum eRawKeys {
	KEYPAD_TAB	= 0x2b,
	KEYPAD_NUMLOCK	= 0x53,
	KEYPAD_SLASH	= 0x54,
	KEYPAD_STAR	= 0x55,
	KEYPAD_BS	= 0x2a,
	KEYPAD_9	= 0x61,
	KEYPAD_8	= 0x60,
	KEYPAD_7	= 0x5f,
	KEYPAD_6	= 0x5e,
	KEYPAD_5	= 0x5d,
	KEYPAD_4	= 0x5c,
	KEYPAD_3	= 0x5b,
	KEYPAD_2	= 0x5a,
	KEYPAD_1	= 0x59,
	KEYPAD_0	= 0x62,
	KEYPAD_DEL	= 0x63,
	KEYPAD_SPACE	= 0x2c,
	KEYPAD_MINUS	= 0x56,
	KEYPAD_PLUS	= 0x57,
	KEYPAD_ENTER	= 0x58,
};

inline bool isNumeric(unsigned key)
{
	return (key >= KEYPAD_1 && key <= KEYPAD_0);
}

// Only meaningful if isNumeric(key) is true; returns -1 otherwise.
inline int keyVal(unsigned key)
{
	if (!isNumeric(key)) return -1;

	int val = (int) (key - KEYPAD_1) + 1;

	// KEYPAD_0 behaves as if it were KEYPAD_10 (comes after 9)
	// Shades of a rotary dial phone here...
	if (val == 10) val = 0;

	return val;
}

#endif // _RAWKEYS_H_
