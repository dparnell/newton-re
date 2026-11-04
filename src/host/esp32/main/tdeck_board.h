/*
	File:		host/esp32/main/tdeck_board.h

	Contains:	Which board this is, and the lines that differ between them
				(docs/host-tdeck-max.md).  One program runs on both the
				T-Deck Pro and the T-Deck Max: HostDeviceBoardStart brings
				up the I2C bus and asks for the Max's XL9555 I/O expander,
				and the drivers take their pins and power switches from
				here.  ESP-IDF's headers only (esp32host.h says why).
*/

#ifndef __TDECK_BOARD_H
#define __TDECK_BOARD_H

extern bool		gBoardMax;				// the T-Deck Max (set before any driver starts, never changed)

// The board's switched lines.  On the Pro those it has are GPIOs; on the
// Max they are the XL9555's.  High is on / enabled / pressed / running, as
// the line's own comment says.
enum BoardLine
{
	kLineModemPower,					// the A7682E's supply
	kLineModemKey,						// its PWRKEY (high: pressed)
	kLineLoraPower,						// the SX1262's supply
	kLineGpsPower,						// the MIA-M10Q's supply
	kLineTouchReset,					// the touch panel's reset (low: reset)
	kLineKeyboardReset,					// Max: the TCA8418's reset (low: reset)
	kLineImuPower,						// Max: the BHI260AP's 1.8 V
	kLineLoraAntenna,					// Max: high the internal antenna, low the external
	kLineMotorPower,					// Max: the DRV2605's enable
	kLineAmplifier,						// Max: the loudspeaker's amplifier
	kLineAudioFromModem,				// Max: high the A7682E to the loudspeaker, low the ES8311
	kLineCount
};

bool	HostDeviceBoardStart(void);					// the I2C bus up and the board found ==> whether there is a bus
void	HostDeviceBoardLineStart(BoardLine line);	// made an output, any GPIO hold a sleeping firmware left released
void	HostDeviceBoardSet(BoardLine line, bool high);
bool	HostDeviceBoardGet(BoardLine line);			// what the line was last set to

// the pins that moved (the rest are where they were: LoRa, the panel, the
// modem's UART, the keyboard's and touch panel's interrupts)
#define PIN_GPS_TX		(gBoardMax ? 16 : 43)		// ours, to the module's RXD
#define PIN_GPS_RX		(gBoardMax ? 2 : 44)		// ours, from the module's TXD

#endif	/* __TDECK_BOARD_H */
