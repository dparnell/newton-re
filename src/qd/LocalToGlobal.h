/*
	File:		qd/LocalToGlobal.h

	Contains:	LocalToGlobal: a point in the current port's coordinates
				turned into its pixel map's (the port's portBits.bounds
				taken off).

	Reconstructed from the MP2x00 US ROM (0x003352b4); each function cites
	its origin.
*/

#ifndef __LOCALTOGLOBAL_H
#define __LOCALTOGLOBAL_H

#include "Ports.h"

void	LocalToGlobal(Point* pt);									// ROM 0x003352b4 LocalToGlobal__FP5Point

#endif	/* __LOCALTOGLOBAL_H */
