/*
	File:		recognition/WordSegment.cpp

	Contains:	ParaGraph's word segmenter (WordSegment.h).

	Reconstructed from the MP2x00 US ROM (0x0026e8e8-0x00271da0,
	0x001286b4-0x00128ab8); each function cites its origin.
*/

#include "WordSegment.h"
#include "ParaGraph.h"
#include "FixedMathExtra.h"
#include <string.h>
#include <stddef.h>

extern const int	rom_ncells[2];
extern const int	rom_matrix[242];
extern const short	rom_cell[3360];
extern const short	EXP_TABL[1032];

// the widest line, in trace units: 0x7d8 histogram columns of four
const long	kWSLineEnd = 0x1f60;
const long	kWSLineMax = 0x1f5c;

static inline long	WSAbs(long x)				{ return x < 0 ? -x : x; }
static inline long	Clamp0(long x)			{ return x < 0 ? 0 : (x > kWSLineMax ? kWSLineMax : x); }
// a byte of a histogram increased, at most to 0x80
static inline UByte	HistAdd(UByte v, long add)	{ return (long) v + add < 0x80 ? (UByte) (v + add) : 0x80; }
// the mean with a learnt value, when there is one
static inline long	MeanWith(long learnt, long value)	{ return learnt < 1 ? value : (learnt + value) / 2; }


#pragma mark - the segmenter

// ROM 0x0026e8e8 WordStrokes__FP13PS_point_typeP15ws_control_typeP15ws_results_type
// One more stroke of the writing (or, with the control's flag 1, the end
// of it; with 0x80, the state thrown away): measured, laid into the
// line's histogram and its gaps worked out again; when the stroke starts
// a new line, or the writing ends, the line is cut into words.
long
WordStrokes(PS_point_type* stroke, ws_control_type* control, ws_results_type* results)
{
	ws_memory_header_type* header = nil;
	if ((control->fFlags & 0x80) == 0)
	{
		if (InitWSData(control, &header) == 0)
		{
			ws_data_type* data = header->fDataPtr;
			data->fTrace = stroke;
			long numPoints = control->fNumPoints;
			data->f024_numPoints = numPoints;
			if (data->f0b4_numStrokes < 1 || numPoints != 0 || (data->f020_flags & 1) == 0)
			{
				if (data->f0b4_numStrokes < 0xfa)
				{
					ws_word_type* words = numPoints != 0 ? results->fWords : nil;
					if (numPoints != 0 && words != nil && WS_GetStrokeBoxAndSlope(data) == 0)
					{
						WS_CalcLineHeight(data);
						if (0 < data->f08c_strokeInLine && WS_NewLine(data) != 0)
						{
							data->f004_endDist = 0;
							data->f0ac_closing = 1;
							if (WordLineStrokes(data, results) != 0)
								goto failed;
							InitForNewLine(data);
						}
						if (WS_HistTheStroke(data) == 0)
						{
							WS_AddStrokeToHist(data);
							WS_WriteStrokeHorzValues(data);
							if (WS_CalcGaps(data) == 0)
							{
								WS_PostprocessGaps(data);
								WS_CountPiks(data);
								WS_SetLineVars(data);
								goto added;
							}
						}
					}
				}
			}
			else
			{
				data->f020_flags |= 2;
added:
				if ((control->fFlags & 1) == 0)
				{
					long mode = control->fMode;
					long go = mode;
					if (0 < mode)
						go = data->f08c_strokeInLine;
					if (0 < go && data->f0a0 < data->f07c_lineRight - data->f068_letterPitch * 4)
					{
						data->f004_endDist = data->f064_letterWidth * mode * 2;
						if (WordLineStrokes(data, results) != 0)
							goto failed;
						data->f0a0 = data->f07c_lineRight;
					}
					if ((data->f020_flags & 2) == 0)
					{
						data->f0b4_numStrokes++;
						data->f08c_strokeInLine++;
					}
					UnlockWSData(control, &header);
					return 0;
				}
				data->f0ac_closing = 2;
				data->f004_endDist = 0;
				if (WordLineStrokes(data, results) == 0)
				{
					WS_FlyLearn(control, header, data);
					ReleaseWSData(control, &header);
					return 0;
				}
			}
		}
failed:
		ReleaseWSData(control, &header);
		return 1;
	}
	ReleaseWSData(control, &header);
	return 0;
}


// ROM 0x0026eb18 InitWSData__FP15ws_control_typePP21ws_memory_header_type
// The state locked (made the first time, from what was learnt on earlier
// lines when the writer's spacing is left to be learnt).  ==> 0, or 1
// when there is no memory (or nothing to do).
long
InitWSData(ws_control_type* control, ws_memory_header_type** header)
{
	if (control != nil
	&& (0 < control->fNumPoints || (control->fFlags & 1) != 0)
	&& (control->fFlags & 0x80) == 0)
	{
		ws_memory_header_type* hdr;
		if (control->fMem == nil)
		{
			// DEVIATION: sized from sizeof (the ROM's 0x44 bytes hold 32-bit handles)
			control->fMem = HWRMemoryAllocHandle(sizeof(ws_memory_header_type));
			if (control->fMem == nil)
				return 1;
			hdr = (ws_memory_header_type*) HWRMemoryLockHandle(control->fMem);
			if (hdr == nil)
				return 1;
			memset(hdr, 0, sizeof(ws_memory_header_type));
		}
		else
		{
			hdr = (ws_memory_header_type*) HWRMemoryLockHandle(control->fMem);
			if (hdr == nil)
				return 1;
		}
		if (hdr->fData == nil)
		{
			// DEVIATION: sized from sizeof (0x18cc bytes in the ROM)
			hdr->fData = HWRMemoryAllocHandle(sizeof(ws_data_type));
			ws_data_type* data;
			if (hdr->fData != nil
			&& (data = (ws_data_type*) HWRMemoryLockHandle(hdr->fData)) != nil)
			{
				memset(data, 0, sizeof(ws_data_type));
				data->f004_endDist = 0;
				data->f014_sureLevel = control->fSureLevel;
				data->f008_wordDistLevel = control->fWordDistLevel;
				if (control->fWordDistLevel < 0)
					data->f008_wordDistLevel = 0;
				else if (10 < control->fWordDistLevel)
					data->f008_wordDistLevel = 10;
				data->f00c_lineDist = control->fLineDist;
				if (control->fLineDist < 0)
					data->f00c_lineDist = 0;
				ws_learnt_type* learnt = (control->fWordDistLevel == 0 && 0 < hdr->fLearnt.fLineHeight) ? &hdr->fLearnt : nil;
				long lineHeight = learnt == nil ? control->fDefLineHeight : learnt->fLineHeight;
				data->f018_defLineHeight = lineHeight;
				data->f06c_lineHeight = lineHeight;
				data->f060_wordDist = lineHeight;
				data->f064_letterWidth = learnt != nil ? learnt->fLetterWidth : lineHeight;
				data->f068_letterPitch = learnt != nil ? learnt->fLetterPitch : lineHeight;
				data->f0e0_meanHeight = lineHeight;
				data->f09c_level = learnt == nil ? 0x1e : learnt->fLevel;
				data->f0d8_heightSum = lineHeight;
				data->f0dc_heightCount = 1;
				if (learnt != nil)
				{
					data->f0d4 = learnt->fLineHeight;
					data->f0c0 = learnt->fLetterWidth;
					data->f0c4 = learnt->fLetterPitch;
					data->f0c8 = learnt->fLevel;
					data->f0ec_slope = learnt->fSlope;
					data->f0f4_slopeY = learnt->fLineHeight * 10;
					data->f0f0_slopeX = (learnt->fLineHeight * 10 * learnt->fSlope) / 100;
				}
				data->f18c8 = control->fField20;
				InitForNewLine(data);
				hdr->fDataPtr = data;
				*header = hdr;
				return 0;
			}
		}
		else
		{
			ws_data_type* data = (ws_data_type*) HWRMemoryLockHandle(hdr->fData);
			if (data != nil)
			{
				if (data->f188c_gapsHandle != nil)
					data->f1888_gaps = (ws_gap_type*) HWRMemoryLockHandle(data->f188c_gapsHandle);
				data->f010_controlFlags = control->fFlags;
				data->f020_flags = control->fFlags & 3;
				hdr->fDataPtr = data;
				*header = hdr;
				return 0;
			}
		}
	}
	return 1;
}


// ROM 0x0026ed6c WS_HistTheStroke__FP12ws_data_type
// The stroke laid out along x, slanted by the writing's slope, into a
// histogram of its own: the pen's path is walked in steps of an eighth of
// the line height (a quarter for a wide stroke's end), and each step adds
// to the columns it crossed - more for a steep step near the line, less
// for a stroke four times the line height or out of the line - and the
// bottom of each downstroke adds a peak (0x18) that a second peak close
// by takes back.  Where the columns worth more than 0xb start and end is
// the stroke's *core*; a stroke with none gets a small peak in its middle
// (flag 0x80).  ==> 0, or 1 for no memory.
long
WS_HistTheStroke(ws_data_type* data)
{
	Boolean lastStep = false;
	PS_point_type* points = data->fTrace;
	long numPoints = data->f024_numPoints;
	long slope = data->f0ec_slope;
	long lineHeight = data->f06c_lineHeight;
	long step = lineHeight / 8;
	if (data->f088_peaks < 4)
		step = step / 2;
	if (step < 2)
		step = 2;
	else if (100 < step)
		step = 100;
	long x0 = data->f028_x0;
	long baseY = data->f8d8_lineY[x0 / 16] < 1 ? data->f044_meanY : (data->f044_meanY + data->f8d8_lineY[x0 / 16]) / 2;
	long top = (slope * (data->f030_y0 - baseY)) / 100;
	long bottom = (slope * (data->f034_y1 - baseY)) / 100;
	long left = Clamp0((top < bottom ? top : bottom) + x0);
	long right = Clamp0((bottom < top ? top : bottom) + data->f02c_x1);
	long span = right - left;
	ULong size = span / 4 + 8;
	long origin = left / 4;
	UByte* hist = (UByte*) HWRMemoryAlloc(size);
	data->f8d4_stroke = hist;
	data->f0f8_histOrigin = origin * 4;
	if (hist == nil)
		return 1;
	memset(hist, 0, size);

	long height2 = data->f06c_lineHeight * 2;
	Boolean wide = height2 < span;
	Boolean rising = true;
	long weight = 1;
	long extreme = 0;
	long quarter = data->f06c_lineHeight / 4;
	long peakX = 0;
	long maxX = 0;
	long minX = kWSLineEnd;
	long travelled = 0;
	long markedX = 0;
	long prevX = 0, prevY = 0, fromX = 0, fromY = 0;
	for (long i = 0; i < numPoints; i++)
	{
		long y = points[i].y;
		long x = Clamp0((slope * (y - baseY)) / 100 + points[i].x);
		if (x < minX)
			minX = x;
		if (maxX < x)
			maxX = x;
		if (i == 0)
		{
			prevX = x;
			fromY = y;
			fromX = x;
			prevY = y;
		}
		travelled += WSAbs(prevX - x) + WSAbs(prevY - y);
		if (rising)
		{
			if (y < extreme - quarter / 2)
				rising = false;
			else if (extreme < y)
				extreme = y;
		}
		else if (extreme + quarter / 2 < y)
		{
			// the bottom of a downstroke
			rising = true;
			if (quarter < x - markedX)
			{
				long at = (x - origin * 4) / 4;
				hist[at] = HistAdd(hist[at], 0x18);
				peakX = x;
				markedX = x;
			}
		}
		else if (y < extreme)
			extreme = y;
		long limit;
		if (height2 < span && numPoints - 5 < i)
		{
			wide = true;
			limit = step * 2;
		}
		else
			limit = wide ? step * 2 : step;
		if (limit < travelled || numPoints - 1 == i)
		{
			if (numPoints - 1 == i)
				lastStep = true;
			long dx = WSAbs(fromX - x);
			long dy = WSAbs(fromY - y);
			long w = (dy * 0x48) / data->f06c_lineHeight;
			if (y < baseY - lineHeight / 2 || lineHeight / 2 + baseY < y)
				w = 1;
			if (wide)
				w = w / 4;
			if (w < 1)
				w = 1;
			if (dx == 0)
			{
				// ROM QUIRK: the last step adds what the step before added
				if (!lastStep)
					weight = w * 4;
				long at = (x - origin * 4) / 4;
				hist[at] = HistAdd(hist[at], weight);
			}
			else
			{
				if (!lastStep)
					weight = dy / dx + 1;
				if (w < weight)
					weight = w;
				long dir = fromX < x ? 1 : -1;
				for (long k = 0; k < dx; k++)
				{
					long px = fromX + dir * k;
					long at = (px - origin * 4) / 4;
					UByte v = HistAdd(hist[at], weight);
					hist[at] = v;
					if (0x17 < v && (markedX = px, 0 < peakX))
					{
						// a second peak close to the last takes it back
						long d = WSAbs(peakX - px);
						if (d < quarter && 4 < d)
						{
							long pat = (peakX - origin * 4) / 4;
							hist[pat] = hist[pat] < 0x19 ? 1 : hist[pat] - 0x18;
							peakX = 0;
						}
					}
				}
			}
			wide = false;
			travelled = 0;
			fromY = y;
			fromX = x;
		}
		prevX = x;
		prevY = y;
	}

	long end = maxX + 4;
	long coreLeft = 0, coreRight = 0;
	UByte highest = 0;
	if (minX <= end)
	{
		for (long x = minX; x <= end; x += 4)
		{
			UByte v = hist[(x - origin * 4) / 4];
			if (highest < v)
				highest = v;
			if (0xb < v)
			{
				coreRight = x;
				if (coreLeft == 0)
					coreLeft = x;
			}
		}
	}
	if (!(minX <= end && 0xb < highest))
	{
		coreLeft = ((maxX + minX) / 8) * 4;
		long at = (coreLeft - origin * 4) / 4;
		hist[at] = HistAdd(hist[at], 0xc);
		data->f020_flags |= 0x80;
		coreRight = coreLeft;
	}
	if (coreLeft < minX)
		coreLeft = minX;
	if (maxX + 1 < coreRight)
		coreRight = maxX + 1;
	data->f048_step = step;
	data->f04c_coreLeft = (coreLeft / 4) * 4;
	data->f050_coreRight = (coreRight / 4) * 4 + 4;
	data->f028_x0 = (minX / 4) * 4;
	data->f02c_x1 = (end / 4) * 4;
	data->f038_width = data->f02c_x1 - (minX / 4) * 4;
	ws_stroke_box* box = &data->fcc4_boxes[data->f08c_strokeInLine];
	box->x0 = (short) data->f028_x0;
	box->x1 = (short) data->f02c_x1;
	box->coreRight = (short) data->f050_coreRight;
	return 0;
}


// ROM 0x0026f5c0 WS_AddStrokeToHist__FP12ws_data_type
// The stroke's histogram added into the line's (a quarter of it, each
// column at most 0x3f) and its core marked, unless it is a small stroke
// with no peak under the line (a dot or a comma, which does not mark a
// letter); the line's extent grown to hold it.
long
WS_AddStrokeToHist(ws_data_type* data)
{
	long coreLeft = data->f04c_coreLeft;
	long lineY = data->f8d8_lineY[coreLeft / 16];
	if ((data->f020_flags & 0x80) != 0)
	{
		if (data->f0fc_line[coreLeft / 4] != 0)
		{
			long at = (coreLeft - data->f0f8_histOrigin) / 4;
			UByte* hist = data->f8d4_stroke;
			hist[at] = hist[at] < 0xd ? 1 : hist[at] - 0xc;
		}
		if (data->f038_width < data->f064_letterWidth / 2 && 0 < lineY && lineY < data->f030_y0)
			goto extent;
	}
	{
		long x = data->f028_x0;
		UByte* from = data->f8d4_stroke + (x - data->f0f8_histOrigin) / 4;
		UByte* to = &data->f0fc_line[x / 4];
		for ( ; x < data->f02c_x1; x += 4, from++, to++)
		{
			UByte core = (x < data->f04c_coreLeft || data->f050_coreRight <= x) ? 0 : 0x80;
			UByte v = (*to & 0x3f) + (*from >> 2);
			if (0x3f < v)
				v = 0x3f;
			*to = v | (*to & 0x80) | core;
		}
	}
extent:
	if (data->f028_x0 < data->f078_lineLeft)
		data->f078_lineLeft = data->f028_x0;
	if (data->f07c_lineRight < data->f02c_x1)
		data->f07c_lineRight = data->f02c_x1;
	if (data->f04c_coreLeft < data->f080_coreLeft)
		data->f080_coreLeft = data->f04c_coreLeft;
	if (data->f084_coreRight < data->f050_coreRight)
		data->f084_coreRight = data->f050_coreRight;
	return 0;
}


// ROM 0x0026f758 WS_CalcGaps__FP12ws_data_type
// The line's gaps found again: each run of columns outside every core
// ends in a gap - its middle, its start, where its empty columns start,
// how wide it is and how much of it is empty, and the size it counts as
// (the empty columns and a part of the rest that the line's level says).
// ==> 0, or 1 for no memory.
long
WS_CalcGaps(ws_data_type* data)
{
	long level = MeanWith(data->f0c8, data->f09c_level) + 10;
	if (level < 10)
		level = 10;
	else if (0x5a < level)
		level = 0x5a;
	long strokes = data->f0b4_numStrokes;
	long first = data->f070_firstStroke;
	if (data->f188c_gapsHandle != nil)
	{
		if (data->f1888_gaps != nil)
			HWRMemoryUnlockHandle(data->f188c_gapsHandle);
		HWRMemoryFreeHandle(data->f188c_gapsHandle);
	}
	data->f188c_gapsHandle = HWRMemoryAllocHandle(((strokes - first) + 4) * sizeof(ws_gap_type));
	if (data->f188c_gapsHandle == nil)
		return 1;
	data->f1888_gaps = (ws_gap_type*) HWRMemoryLockHandle(data->f188c_gapsHandle);
	if (data->f1888_gaps == nil)
		return 1;
	Boolean inCore = false;
	long numGaps = 0, empty = 0, width = 0;
	long x = data->f078_lineLeft;
	long start = x, emptyStart = x;
	for ( ; x < data->f07c_lineRight + 4; x += 4)
	{
		UByte v = data->f0fc_line[x / 4];
		if (data->f07c_lineRight <= x)
		{
			v |= 0x80;
			inCore = false;
		}
		if ((v & 0x80) == 0)
		{
			inCore = false;
			if ((v & 0x3f) == 0)
			{
				if (empty == 0)
					emptyStart = x;
				empty++;
			}
			if (width == 0)
				start = x;
			width++;
		}
		else if (!inCore)
		{
			ws_gap_type* gap = &data->f1888_gaps[numGaps];
			long mid = (start + x) / 2;
			gap->fMid = (short) mid;
			gap->fZeroStart = empty != 0 ? (short) emptyStart : (short) mid;
			gap->fStart = (short) (width != 0 ? start : x);
			long counts = ((100 - level) * (width - empty)) / 100;
			gap->fSize2 = gap->fSize = (short) (((counts + empty) * 0x40000) >> 16);
			gap->fZeroWidth = (short) (empty << 2);
			gap->fWidth = (short) (width << 2);
			gap->fFlags = 0;
			numGaps++;
			empty = 0;
			width = 0;
			inCore = true;
		}
		else
		{
			start = x;
			emptyStart = x;
		}
	}
	data->f0a8_numGaps = numGaps;
	return 0;
}


// ROM 0x0026f9c8 WS_CountPiks__FP12ws_data_type
// The peaks of the line's cores counted - a rise and a fall of three or
// more, no nearer together than a sixteenth of the line height (more
// for a line at a higher level) - which is roughly how many letters it
// has.
long
WS_CountPiks(ws_data_type* data)
{
	long level = MeanWith(data->f0c8, data->f09c_level);
	long unit = data->f06c_lineHeight / 16;
	long skip = (unit * level) / 0x32 + unit;
	Boolean leading = true;
	long low = 1;
	Boolean up = true;
	long high = 0;
	long wait = 0;
	long peaks = 0;
	long x = data->f080_coreLeft;
	if (x < data->f084_coreRight)
	{
		for ( ; x < data->f084_coreRight; x += 4)
		{
			long v = data->f0fc_line[x / 4] & 0x3f;
			if (wait < 1)
			{
				if (!leading || v != 0)
				{
					leading = false;
					if (up)
					{
						if (high < v)
							high = v;
						if (v <= high - 3)
						{
							peaks++;
							up = false;
							wait = skip;
							low = v;
						}
					}
					else
					{
						if (v < low)
							low = v;
						if (low + 3 <= v)
						{
							up = true;
							wait = skip;
							high = v;
						}
					}
				}
			}
			else
				wait -= 4;
		}
		if (!up)
			goto store;
	}
	peaks++;
store:
	data->f088_peaks = peaks;
	data->f0a4 = skip;
	return 0;
}


// ROM 0x0026fb00 WS_SetLineVars__FP12ws_data_type
// The line's letter width and pitch and its level worked out from the
// gaps: those bigger than a letter (the spaces) and those that are not.
long
WS_SetLineVars(ws_data_type* data)
{
	if (data->f088_peaks < 4)
	{
		long v = data->f0c4;
		if (v < 1)
			v = data->f06c_lineHeight / 2;
		data->f068_letterPitch = v;
		data->f064_letterWidth = v;
	}
	else
		data->f068_letterPitch = (data->f07c_lineRight - data->f078_lineLeft) / data->f088_peaks;
	long pitch = MeanWith(data->f0c4, data->f068_letterPitch);
	long peaks = data->f088_peaks;
	long ref = peaks < 4 ? data->f06c_lineHeight : pitch;
	long threshold = (ref * 0x1e) / 100;
	long small = 0, big = 0, smallWidths = 0, bigSizes = 0, emptiness = 0, excess = 0;
	for (long i = 1; i < data->f0a8_numGaps - 1; i++)
	{
		ws_gap_type* gap = &data->f1888_gaps[i];
		long size = gap->fSize;
		if (threshold + ref < size)
		{
			if (pitch * 3 < size)
			{
				excess += size - pitch * 3;
				size = pitch * 3;
			}
			bigSizes += size;
			big++;
		}
		else
		{
			long z = gap->fZeroWidth;
			if (pitch < z)
				z = 0;
			else if (pitch / 2 < z)
				z = pitch / 2;
			emptiness += z;
			smallWidths += gap->fWidth;
			small++;
		}
	}
	long lineWidth = data->f07c_lineRight - data->f078_lineLeft;
	data->f090 = lineWidth - bigSizes;
	if (data->f090 < 1)
		data->f090 = 1;
	data->f098 = big < 1 ? 0 : bigSizes / big;
	data->f094 = small < 1 ? 0 : smallWidths / small;
	if (3 < peaks)
	{
		long letters = data->f090;
		data->f064_letterWidth = data->f090 / peaks;
		data->f068_letterPitch = (lineWidth - excess) / peaks;
		long level = ((emptiness + smallWidths / 4) * 200) / letters;
		data->f09c_level = level;
		if (0x64 < level)
			data->f09c_level = 100;
		else if (level < 2)
			data->f09c_level = 2;
	}
	return 0;
}


// ROM 0x0026fd3c WS_CalcLineHeight__FP12ws_data_type
// The line height moved a third of the way towards what the strokes so
// far say (the mean stroke height, plus a part the line's level says),
// twenty units at least.
long
WS_CalcLineHeight(ws_data_type* data)
{
	long a, b;
	if (data->f0cc + data->f088_peaks < 4)
	{
		a = data->f0e0_meanHeight + data->f06c_lineHeight;
		b = data->f018_defLineHeight;
	}
	else
	{
		long level = MeanWith(data->f0c8, data->f09c_level);
		a = data->f06c_lineHeight + ((level / 2 + 0x28) * data->f0e0_meanHeight) / 100;
		b = data->f068_letterPitch;
	}
	data->f06c_lineHeight = (a + b) / 3;
	if (data->f06c_lineHeight < 0x14)
		data->f06c_lineHeight = 0x14;
	return 0;
}


// ROM 0x0026fde8 WS_PostprocessGaps__FP12ws_data_type
// Two gaps close together (under half a letter apart) are not both
// spaces: the one with less empty in it is halved and marked (flag 2).
long
WS_PostprocessGaps(ws_data_type* data)
{
	long pitch = data->f068_letterPitch;
	UByte kept[252];
	long numKept = 0;
	ws_gap_type* gaps = data->f1888_gaps;
	for (long i = 0; i < data->f0a8_numGaps; i++)
	{
		gaps[i].fSize2 = gaps[i].fSize;
		if (i == 0 || data->f0a8_numGaps - 1 == i || pitch / 2 < gaps[i].fSize)
			kept[numKept++] = (UByte) i;
	}
	for (long i = 0; i < numKept - 1; i++)
	{
		ws_gap_type* a = &gaps[kept[i]];
		ws_gap_type* b = &gaps[kept[i + 1]];
		if (b->fStart - (a->fStart + a->fWidth) < pitch / 2)
		{
			long right = 0x7d00, left = 0x7d00;
			if (0 < i)
				left = a->fZeroWidth;
			if (i < numKept - 2)
				right = b->fZeroWidth;
			Boolean close = right < pitch * 2 ? true : left < pitch * 2;
			ws_gap_type* which = (close && left / 2 <= right) ? a : b;
			which->fSize2 = which->fZeroWidth / 2;
			which->fFlags |= 2;
		}
	}
	return 0;
}


// ROM 0x0026ff4c WS_SegmentWords__FiP12ws_data_type
// The gaps right of `left` put to the net one by one; a gap it is sure
// enough is a space ends a word (the last gap always does, and is not
// asked).  ==> how many words (their first and last gaps in
// f149c_segments).
long
WS_SegmentWords(long left, ws_data_type* data)
{
	long numWords = 0;
	long previous = 0;
	long answer = left;
	Boolean notLast = false;
	for (long i = 1; i < data->f0a8_numGaps; i++)
	{
		ws_gap_type* gap = &data->f1888_gaps[i];
		if (left < gap->fStart)
		{
			notLast = data->f0a8_numGaps - 1 != i;
			if (notLast)
			{
				long inputs[11];
				for (int k = 0; k < 7; k++)
					inputs[k] = data->f18a4_net[k];
				if (inputs[3] == 0 && 0x50 < inputs[5])
					inputs[5] = 0x50;
				inputs[7] = i;
				inputs[8] = (gap->fSize2 * 100) / data->f1898_pitch;
				inputs[9] = (gap->fZeroWidth * 100) / data->f1898_pitch;
				inputs[10] = (gap->fWidth * 100) / data->f1898_pitch;
				if (data->f008_wordDistLevel != 0)
				{
					inputs[8] += (inputs[8] * (6 - data->f008_wordDistLevel)) / 4;
					inputs[9] += (inputs[9] * (6 - data->f008_wordDistLevel)) / 4;
					inputs[10] += (inputs[10] * (6 - data->f008_wordDistLevel)) / 4;
				}
				answer = NeuroNetWS(inputs);
			}
			// ROM QUIRK: the last gap is not asked, and is judged by the
			// answer the gap before it had (by `left` if none was asked)
			if (answer < 1 || WSAbs(answer) <= data->f014_sureLevel)
			{
				if (notLast)
				{
					gap->fSure = (SByte) answer;
					continue;
				}
			}
			data->f149c_segments[numWords][0] = (UByte) previous;
			data->f149c_segments[numWords][1] = (UByte) i;
			numWords++;
			previous = i;
		}
	}
	return numWords;
}


// ROM 0x00270154 WS_GetWordDist__FP12ws_data_type
// How far apart the line's words are: the mean small and big gaps
// against the letter pitch, what the net is told about the line
// (f18a4_net), and the distance itself, held within what the writer's
// spacing setting allows.  ==> it.
long
WS_GetWordDist(ws_data_type* data)
{
	// the smallest and largest word distance for each spacing setting, in
	// hundredths of the letter pitch (nought: no limit); on the stack in
	// the ROM
	static const UByte kMinDist[11] = { 0x00, 0x28, 0x3c, 0x50, 0x64, 0x78, 0x00, 0x96, 0xb4, 0xc8, 0xf0 };
	static const UByte kMaxDist[11] = { 0x00, 0x32, 0x46, 0x5a, 0x6e, 0x82, 0x00, 0xa0, 0xbe, 0xd2, 0xff };
	long width = MeanWith(data->f0c0, data->f064_letterWidth);
	long pitch = MeanWith(data->f0c4, data->f068_letterPitch);
	long level = data->f09c_level;
	if (pitch < 1)
		pitch = 1;
	long threshold = (pitch * (level / 2)) / 100;
	long smallSum = 0, big = 0, bigSum = 0, small = 0;
	long last = data->f0a8_numGaps - 1;
	long smallMean;
	if (1 < last)
	{
		for (long i = 1; i < last; i++)
		{
			long size = data->f1888_gaps[i].fSize;
			if (size < pitch)
			{
				long m = size;
				if (size < pitch / 8)
					m = pitch / 8;
				smallSum += m;
				small++;
			}
			if (threshold + pitch < size)
			{
				if (pitch * 3 < size)
					size = pitch * 3;
				bigSum += size;
				big++;
			}
		}
		if (0 < small)
		{
			smallMean = smallSum / small;
			goto means;
		}
	}
	smallMean = 0;
means:
	long bigMean = big < 1 ? 0 : bigSum / big;
	long smallRatio = (smallMean * 100) / pitch;
	data->f18a4_net[0] = smallRatio;
	data->f18a4_net[1] = small;
	data->f18a4_net[3] = big;
	data->f18a4_net[2] = (bigMean * 100) / pitch;
	data->f18a4_net[4] = level;
	data->f18a4_net[5] = (width * 100) / pitch;
	long peaks = data->f088_peaks;
	data->f18a4_net[6] = peaks;
	long mode;
	if (smallMean < (bigMean * 0x28) / 100 && width < (bigMean * 0x58) / 100 && pitch < (bigMean * 0x5b) / 100)
		mode = 1;
	else
		mode = 2;
	long v = smallRatio;
	if (small * level + peaks < 0x65)
		v = (level - small) + peaks;
	long dist = ((v + (peaks < 10 ? 0x6e : 0x4f)) * pitch) / 100;
	if (dist < pitch - pitch / 4)
		dist = pitch - pitch / 4;
	if (pitch * 3 < dist)
		dist = pitch * 3;
	if (data->f0cc + data->f088_peaks < 6 && dist < data->f018_defLineHeight + (data->f018_defLineHeight >> 2))
	{
		mode = 0;
		dist = data->f018_defLineHeight + (data->f018_defLineHeight >> 2);
	}
	long lo = 0, hi = 0;
	long setting = data->f008_wordDistLevel;
	if (kMinDist[setting] != 0)
		lo = (pitch * kMinDist[setting]) / 100;
	if (kMaxDist[setting] != 0)
		hi = (pitch * kMaxDist[setting]) / 100;
	if (lo != 0 && dist < lo)
		dist = lo;
	if (hi != 0 && hi < dist)
		dist = hi;
	data->f060_wordDist = dist;
	data->f1890_smallGap = smallMean;
	data->f1894_bigGap = bigMean;
	data->f1898_pitch = pitch;
	data->f189c_wordDist = dist;
	data->f18a0_mode = mode;
	return dist;
}


// ROM 0x00270520 WS_FlyLearn__FP15ws_control_typeP21ws_memory_header_typeP12ws_data_type
// What this line was like kept among the last four, and once there are
// four their mean learnt for the lines to come.  ==> 0, or 1 before
// there are four (or for nothing to learn with).
long
WS_FlyLearn(ws_control_type* control, ws_memory_header_type* header, ws_data_type* data)
{
	if (header != nil && control != nil && data != nil)
	{
		long i;
		for (i = 0; i < 4; i++)
			if (header->fHistory[i].fLineHeight == 0)
				break;
		if (3 < i)
		{
			i = 3;
			memmove(&header->fHistory[0], &header->fHistory[1], 3 * sizeof(ws_learnt_type));
		}
		ws_learnt_type* line = &header->fHistory[i];
		line->fLineHeight = (short) data->f06c_lineHeight;
		line->fLetterWidth = (short) data->f0c0;
		line->fLetterPitch = (short) data->f0c4;
		line->fSlope = (short) data->f0ec_slope;
		line->fLevel = (UByte) data->f0c8;
		if (2 < i)
		{
			ULong level = 0, slope = 0, pitch = 0, width = 0, height = 0;
			for (i = 0; i < 4; i++)
			{
				height += header->fHistory[i].fLineHeight;
				width += header->fHistory[i].fLetterWidth;
				pitch += header->fHistory[i].fLetterPitch;
				slope += header->fHistory[i].fSlope;
				level += header->fHistory[i].fLevel;
			}
			header->fLearnt.fLineHeight = (short) ((uint32_t) height >> 2);
			header->fLearnt.fLetterWidth = (short) ((uint32_t) width >> 2);
			header->fLearnt.fLetterPitch = (short) ((uint32_t) pitch >> 2);
			header->fLearnt.fSlope = (short) ((uint32_t) slope >> 2);
			header->fLearnt.fLevel = (UByte) ((uint32_t) level >> 2);
			return 0;
		}
	}
	return 1;
}


// ROM 0x0027068c ReleaseWSData__FP15ws_control_typePP21ws_memory_header_type
// The line's state freed (its histograms, its gaps and itself); with the
// control's flag 0x80 the header too.
long
ReleaseWSData(ws_control_type* control, ws_memory_header_type** header)
{
	ws_memory_header_type* hdr = nil;
	if (header == nil)
	{
		if (control->fMem == nil)
			goto header;
		hdr = (ws_memory_header_type*) HWRMemoryLockHandle(control->fMem);
	}
	else
		hdr = *header;
	if (hdr != nil)
	{
		ws_data_type* data = hdr->fDataPtr;
		if (data == nil)
		{
			if (hdr->fData == nil)
				goto header;
			data = (ws_data_type*) HWRMemoryLockHandle(hdr->fData);
		}
		if (data != nil)
		{
			if (data->f8d4_stroke != nil)
			{
				HWRMemoryFree((Ptr) data->f8d4_stroke);
				data->f8d4_stroke = nil;
			}
			if (data->f1888_gaps != nil)
			{
				HWRMemoryUnlockHandle(data->f188c_gapsHandle);
				data->f1888_gaps = nil;
			}
			if (data->f188c_gapsHandle != nil)
				HWRMemoryFreeHandle(data->f188c_gapsHandle);
			HWRMemoryUnlockHandle(hdr->fData);
			hdr->fDataPtr = nil;
			HWRMemoryFreeHandle(hdr->fData);
			hdr->fData = nil;
		}
	}
header:
	if ((control->fFlags & 0x80) != 0 && control->fMem != nil)
	{
		if (hdr != nil)
			HWRMemoryUnlockHandle(control->fMem);
		*header = nil;
		HWRMemoryFreeHandle(control->fMem);
		control->fMem = nil;
	}
	if (header != nil && *header != nil && control->fMem != nil)
	{
		HWRMemoryUnlockHandle(control->fMem);
		*header = nil;
	}
	return 0;
}


// ROM 0x002707a0 UnlockWSData__FP15ws_control_typePP21ws_memory_header_type
// The state unlocked until the next stroke (the stroke's histogram freed).
long
UnlockWSData(ws_control_type* control, ws_memory_header_type** header)
{
	ws_memory_header_type* hdr;
	if (header == nil)
	{
		if (control->fMem == nil)
			return 0;
		hdr = (ws_memory_header_type*) HWRMemoryLockHandle(control->fMem);
	}
	else
		hdr = *header;
	if (hdr != nil)
	{
		ws_data_type* data = hdr->fDataPtr;
		if (data == nil)
		{
			if (hdr->fData == nil)
				goto header;
			data = (ws_data_type*) HWRMemoryLockHandle(hdr->fData);
		}
		if (data != nil)
		{
			if (data->f8d4_stroke != nil)
			{
				HWRMemoryFree((Ptr) data->f8d4_stroke);
				data->f8d4_stroke = nil;
			}
			if (data->f1888_gaps != nil)
			{
				HWRMemoryUnlockHandle(data->f188c_gapsHandle);
				data->f1888_gaps = nil;
			}
			HWRMemoryUnlockHandle(hdr->fData);
			hdr->fDataPtr = nil;
		}
	}
header:
	if (header != nil && *header != nil && control->fMem != nil)
	{
		HWRMemoryUnlockHandle(control->fMem);
		*header = nil;
	}
	return 0;
}


// ROM 0x0027086c InitForNewLine__FP12ws_data_type
// A new line: what the last one measured averaged into what has been
// learnt, and the line's histograms and extent cleared.
void
InitForNewLine(ws_data_type* data)
{
	if (0 < data->f0b8_numLines)
	{
		data->f0cc += data->f088_peaks;
		data->f0d0 += data->f090;
		data->f0f0_slopeX = data->f0f0_slopeX / 2;
		data->f0f4_slopeY = data->f0f4_slopeY / 2;
		data->f0c0 = MeanWith(data->f0c0, data->f064_letterWidth);
		data->f0bc = MeanWith(data->f0bc, data->f060_wordDist);
		data->f0c4 = MeanWith(data->f0c4, data->f068_letterPitch);
		data->f0c8 = MeanWith(data->f0c8, data->f09c_level);
		data->f0e4 = MeanWith(data->f0e4, data->f098);
		data->f0e8 = MeanWith(data->f0e8, data->f094);
		data->f068_letterPitch = data->f0c4;
		data->f064_letterWidth = data->f0c0;
		data->f09c_level = MeanWith(data->f01c, data->f0c8);
		memset(data->f0fc_line, 0, sizeof(data->f0fc_line));
		memset(data->f8d8_lineY, 0, sizeof(data->f8d8_lineY));
		if (0 < data->f08c_strokeInLine)
		{
			// the stroke that started the line is its first
			data->fcc4_boxes[0] = data->fcc4_boxes[data->f08c_strokeInLine];
			data->f08c_strokeInLine = 0;
		}
	}
	data->f078_lineLeft = kWSLineEnd;
	data->f07c_lineRight = 0;
	data->f080_coreLeft = kWSLineEnd;
	data->f084_coreRight = 0;
	data->f088_peaks = 0;
	data->f0a0 = 0;
	data->f090 = 0;
	data->f098 = 0;
	data->f0ac_closing = 0;
	data->f0b8_numLines++;
	data->f074_firstWord = data->f0b0_numWords;
	data->f05c = 0;
	data->f070_firstStroke = data->f0b4_numStrokes;
	if ((data->f020_flags & 0x10) != 0)
		data->f05c = 1;
}


// ROM 0x00270ab0 WordLineStrokes__FP12ws_data_typeP15ws_results_type
// The line cut into words: the word distance worked out, the gaps put to
// the net (WS_SegmentWords), each word given the strokes that fall
// between its gaps (with how sure the gap inside the stroke was), a
// stroke two words both have given to the one it overlaps more, and the
// words written into the results after those already there (a word
// already compressed keeps its strokes).  ==> 0, or 1 for no memory.
long
WordLineStrokes(ws_data_type* data, ws_results_type* results)
{
	ws_word_type* words = results->fWords;
	long lastBox = data->f08c_strokeInLine;
	if (data->f0ac_closing == 1)
		lastBox--;		// the stroke that started a new line is not this line's
	ULong level = MeanWith(data->f0c8, data->f09c_level);
	long width = data->f064_letterWidth;
	if (0 < data->f0c0)
		width = (data->f0c0 + width) / 2;
	WS_GetWordDist(data);
	long left = data->f078_lineLeft;
	long dist = data->f060_wordDist;
	long numDone = 0;
	UByte done[252];
	long firstWord = data->f074_firstWord;
	for (long w = data->f074_firstWord; w < data->f0b0_numWords; w++)
	{
		if ((words[w].fFlags & 4) != 0)
		{
			firstWord = w + 1;
			left = words[w].fRight;
			for (long k = 0; k < words[w].fCount; k++)
				done[numDone++] = results->fStrokes[words[w].fFirst + k];
		}
	}
	long numWords = WS_SegmentWords(left, data);
	// for each word, (stroke, sureness + 100) pairs
	UByte* pairs = (UByte*) HWRMemoryAlloc(numWords * 0x1f6);
	if (pairs == nil)
		return 1;
	ws_gap_type* gaps = data->f1888_gaps;
	long wordIndex = firstWord;
	for (long s = 0; s < numWords; s++)
	{
		ULong firstGap = data->f149c_segments[s][0];
		ULong lastGap = data->f149c_segments[s][1];
		long from = gaps[firstGap].fMid + gaps[firstGap].fSize / 4;
		long to = gaps[lastGap].fMid + gaps[lastGap].fSize / 4;
		if (s == 0)
			from = data->f078_lineLeft;
		if (numWords - 1 == s)
			to = data->f07c_lineRight;
		long count = 0;
		UByte* mine = pairs + s * 0x1f6;
		for (long b = 0; b <= lastBox; b++)
		{
			ULong strokeNo = data->f070_firstStroke + b;
			if (strokeNo == (ULong) data->f0b4_numStrokes && (data->f020_flags & 2) != 0)
				break;
			Boolean isDone = false;
			for (long k = 0; k < numDone; k++)
				if (strokeNo == done[k])
				{
					isDone = true;
					break;
				}
			if (isDone)
				continue;
			long x0 = data->fcc4_boxes[b].x0;
			long x1 = data->fcc4_boxes[b].x1;
			if ((from <= x0 && x0 <= to) || (from <= x1 && x1 <= to) || (x0 <= from && to <= x1))
			{
				mine[count * 2] = (UByte) strokeNo;
				long mid = (data->fcc4_boxes[b].coreRight + data->fcc4_boxes[b].x1 + 1) / 2;
				long found = 0;
				ULong g = firstGap + 1;
				ws_gap_type* gap = &gaps[g];
				if (g < lastGap)
				{
					for ( ; (long) g < (long) lastGap; g++, gap++)
						if (gap->fStart <= mid && mid < gap->fStart + gap->fWidth)
						{
							found = gap->fSize;
							break;
						}
					if (found != 0)
						mine[count * 2 + 1] = (UByte) (gap->fSure + 100);
					else
						mine[count * 2 + 1] = 0;
				}
				else
					mine[count * 2 + 1] = 0;
				count++;
			}
		}
		long ratio;
		if (s < numWords - 1)
		{
			ratio = (gaps[lastGap].fSize / (dist * 2)) * 100;
			if (100 < ratio)
				ratio = 100;
		}
		else
			ratio = 100;
		long wordLeft = left < from ? from : left;
		long slope = data->f0ec_slope;
		if (slope < -0x7f)
			slope = -0x7f;
		else if (0x7f < slope)
			slope = 0x7f;
		ws_word_type* word = &words[wordIndex];
		word->fWord = (UByte) ((firstWord - data->f074_firstWord) + s + 1);
		word->fLine = (UByte) data->f0b8_numLines;
		word->fGapRatio = (UByte) ratio;
		word->fWordDist = (UByte) level;
		word->fSlope = (SByte) slope;
		word->fLineY = data->f8d8_lineY[(wordLeft + to) / 32];
		word->fLeft = (short) ((UShort) gaps[firstGap].fMid + gaps[firstGap].fSize / 2);
		word->fRight = (short) ((UShort) gaps[lastGap].fMid - gaps[lastGap].fSize / 2);
		word->fLetterWidth = (short) width;
		word->fLineHeight = (short) data->f06c_lineHeight;
		word->fCount = (UByte) count;
		word->fFlags = 0;
		if (data->f0ac_closing != 0)
			word->fFlags = 1;
		if (data->f074_firstWord == wordIndex && (data->f05c & 1) != 0)
			word->fFlags |= 0x10;
		if (8 < data->f0cc + data->f088_peaks)
			word->fFlags |= 0x20;
		wordIndex++;
	}

	// a stroke two neighbouring words both took goes to the one it
	// overlaps more (the whole search done again for each of the line's
	// strokes, each pass ending at the first stroke it moves)
	long lastWord = numWords - 1;
	for (long pass = 1; pass <= data->f08c_strokeInLine && pass < 0xfb; pass++)
	{
		Boolean moved = false;
		long w = firstWord;
		for (long s = 0; s < lastWord && !moved; s++, w++)
		{
			long aFrom = gaps[data->f149c_segments[s][0]].fMid + gaps[data->f149c_segments[s][0]].fSize / 4;
			long aTo = gaps[data->f149c_segments[s][1]].fMid + gaps[data->f149c_segments[s][1]].fSize / 4;
			UByte* mine = pairs + s * 0x1f6;
			ws_word_type* word = &words[w];
			for (long k = 0; k < word->fCount && !moved; k++)
			{
				for (long m = 0; m < word[1].fCount && !moved; m++)
				{
					if (mine[k * 2] == mine[m * 2 + 0x1f6])
					{
						long b = mine[k * 2] - data->f070_firstStroke;
						long x0 = data->fcc4_boxes[b].x0;
						long x1 = data->fcc4_boxes[b].x1;
						long overlapA = ((x1 < aTo ? aTo : x1) - (aFrom < x0 ? aFrom : x0)) - (WSAbs(aTo - x1) + WSAbs(aFrom - x0));
						long bFrom = gaps[data->f149c_segments[s + 1][0]].fMid + gaps[data->f149c_segments[s + 1][0]].fSize / 4;
						long bTo = gaps[data->f149c_segments[s + 1][1]].fMid + gaps[data->f149c_segments[s + 1][1]].fSize / 4;
						long overlapB = ((x1 < bTo ? bTo : x1) - (bFrom < x0 ? bFrom : x0)) - (WSAbs(bTo - x1) + WSAbs(bFrom - x0));
						if (overlapA < overlapB)
						{
							memmove(mine + k * 2, mine + k * 2 + 2, (0xfb - (k + 1)) * 2);
							word->fCount--;
							moved = true;
						}
						else
						{
							// ROM QUIRK: the entry moved into this place is
							// not looked at (m goes on)
							memmove(mine + m * 2 + 0x1f6, mine + m * 2 + 0x1f8, (0xfb - (m + 1)) * 2);
							word[1].fCount--;
						}
					}
				}
			}
		}
	}

	// the words' strokes into the results
	long at = data->f070_firstStroke + numDone;
	for (long s = 0, w = firstWord; s < numWords; s++, w++)
	{
		ws_word_type* word = &words[w];
		for (long k = 0; k < word->fCount; k++)
		{
			results->fStrokes[at + k] = pairs[s * 0x1f6 + k * 2];
			results->fSure[at + k] = (SByte) (pairs[s * 0x1f6 + k * 2 + 1] - 100);
		}
		word->fFirst = (UByte) at;
		at += word->fCount;
	}

	// cut early (f004_endDist): the words that end too near the line's end
	// are left for later
	long s = 0;
	for ( ; ; s++)
	{
		if (data->f004_endDist < 1 || numWords <= s)
			goto cut;
		if (data->f07c_lineRight - data->f004_endDist < gaps[data->f149c_segments[s][1]].fMid)
			break;
	}
	memset(&words[firstWord + s], 0, sizeof(ws_word_type));
	numWords = s;
cut:
	// ROM QUIRK: a word left with no strokes is replaced by the one after
	// it alone (the rest are not moved down), and that one is not looked at
	for (long k = 0; k < numWords; k++)
	{
		ws_word_type* word = &words[firstWord + k];
		if (word->fCount == 0)
		{
			memcpy(word, word + 1, sizeof(ws_word_type));
			numWords--;
		}
	}
	// a line closed by a new one whose last stroke was a dash: the last
	// word may be one word and a dash
	if (data->f0ac_closing == 1 && 0 < numWords && data->f058_prevHeight * 2 < data->f054_prevWidth)
	{
		long lo = data->f078_lineLeft / 4;
		long x = data->f07c_lineRight / 4;
		long end;
		do
		{
			end = x - 1;
			if (data->f0fc_line[x - 1] != 0)
				break;
			x = end;
		} while (lo < end);
		long flats = 0;
		for (x = end; lo < x && flats < 2; x--)
		{
			UByte v = data->f0fc_line[x] & 0x3f;
			if (v == 0)
				break;
			if (1 < v)
			{
				if (v != 4)
					goto done;
				flats++;
			}
		}
		ws_word_type* lastOne = &words[firstWord + numWords - 1];
		if (flats == 1 && lastOne->fLeft < x * 4 - width)
		{
			long dash = (end - x) * 4;
			long half = data->f06c_lineHeight / 2;
			if (half < dash && dash < data->f06c_lineHeight * 2)
				lastOne->fFlags |= 8;
		}
	}
done:
	data->f0b0_numWords = firstWord + numWords;
	results->fNumWords = (UByte) (firstWord + numWords);
	results->fLineFirst = (UByte) firstWord;
	results->fNumStrokes = (UByte) (data->f070_firstStroke + numDone);
	HWRMemoryFree((Ptr) pairs);
	return 0;
}


// ROM 0x002715f4 WS_GetStrokeBoxAndSlope__FP12ws_data_type
// The stroke's box and mean point, and the writing's slope learnt from
// its steep steps (a step down counted eight times over a step up).
// ==> 0, or 1 for a stroke with no points.
long
WS_GetStrokeBoxAndSlope(ws_data_type* data)
{
	long step = data->f06c_lineHeight / 16;
	if (step < 3)
		step = 3;
	long numPoints = data->f024_numPoints;
	PS_point_type* points = data->fTrace;
	long minX = kWSLineEnd, maxX = 0, minY = kWSLineEnd, maxY = 0;
	long sumX = 0, sumY = 0, slopeX = 0, slopeY = 0;
	long i = 0, anchor = 0;
	if (0 < numPoints)
	{
		for ( ; i < numPoints; i++)
		{
			long x = points[i].x;
			long y = points[i].y;
			if (y < 0)
				break;
			sumX += x;
			sumY += y;
			if (maxY < y)
				maxY = y;
			if (y < minY)
				minY = y;
			if (maxX < x)
				maxX = x;
			if (x < minX)
				minX = x;
			long dx = x - points[anchor].x;
			long adx = WSAbs(dx);
			long dy = y - points[anchor].y;
			long ndy = -dy;
			long ady = -1 < ndy ? ndy : dy;
			if (step < ady + adx && (anchor = i, ndy != 0) && (adx * 100) / ady < 0x65)
			{
				// ROM QUIRK: a step down is weighed eight times a step up
				if (ndy < 0)
				{
					ndy = dy * 8;
					dx = dx * -8;
				}
				slopeX += dx;
				slopeY += ndy;
			}
		}
		if (i != 0)
		{
			data->f024_numPoints = i;
			data->f028_x0 = minX;
			data->f02c_x1 = maxX + 1;
			data->f030_y0 = minY;
			data->f034_y1 = maxY + 1;
			data->f058_prevHeight = data->f03c_height;
			data->f054_prevWidth = data->f038_width;
			data->f03c_height = maxY + (1 - minY);
			data->f038_width = maxX + (1 - minX);
			data->f040_meanX = sumX / i;
			data->f044_meanY = sumY / i;
			ws_stroke_box* box = &data->fcc4_boxes[data->f08c_strokeInLine];
			box->x0 = (short) minX;
			box->x1 = (short) (maxX + 1);
			box->y0 = (short) data->f030_y0;
			if (9 < data->f024_numPoints && 0xa0 < slopeY)
			{
				long x = data->f0f0_slopeX;
				data->f0f0_slopeX = x + slopeX;
				data->f0f4_slopeY += slopeY;
				data->f0ec_slope = ((x + slopeX) * 100) / data->f0f4_slopeY;
				if (data->f0f4_slopeY < 500)
					data->f0ec_slope = data->f0ec_slope / 2;
			}
			if (data->f06c_lineHeight / 4 < data->f03c_height)
			{
				data->f0d8_heightSum += data->f03c_height;
				data->f0dc_heightCount++;
				data->f0e0_meanHeight = data->f0d8_heightSum / data->f0dc_heightCount;
			}
			return 0;
		}
	}
	return 1;
}


// ROM 0x002718e4 WS_NewLine__FP12ws_data_type
// Whether the stroke starts a new line: it is further below the line (or
// left of where the line has got to) than the line height, adjusted for
// the stroke's own size and shape, allows - or where the line distance
// is fixed, further than that.  A space gesture always does, and so
// does a stroke where the line has no y yet.  ==> 1 for a new line.
long
WS_NewLine(ws_data_type* data)
{
	long lineY = data->f8d8_lineY[data->f028_x0 / 16];
	if (lineY < 1)
		return 1;
	if (CheckForSpaceGesture(data) == 0)
		return 1;
	long back = ((data->f084_coreRight - data->f064_letterWidth * 2) - data->f028_x0) / 2;
	long lineHeight = data->f06c_lineHeight;
	if (-back != lineHeight && back <= -lineHeight)
		back = -lineHeight;
	if (lineHeight < back)
		back = lineHeight;
	long overlap = ((data->f084_coreRight - data->f064_letterWidth) - data->f02c_x1) / 2;
	if (overlap < 0)
		overlap = 0;
	if (lineHeight < overlap)
		overlap = lineHeight;
	long above = ((lineY + lineHeight / 2) - data->f030_y0) * 3;
	if (above < 0)
		above = 0;
	if (lineHeight < above)
		above = lineHeight;
	long height = data->f03c_height;
	long width = data->f038_width;
	if (height < width)
		above = 0;
	long three = lineHeight * 3;
	long two = lineHeight * 2;
	if (height < lineHeight)
	{
		two += two / 3;
		if (width < data->f068_letterPitch)
			two += two / 3;
	}
	long down = three;
	if (width < data->f068_letterPitch)
		down = three + three / 3;
	if (height < lineHeight)
		down += down / 3;
	if (data->f024_numPoints < 100)
	{
		long length = 0;
		for (long i = 1; i < data->f024_numPoints; i++)
			length += WSAbs(data->fTrace[i].x - data->fTrace[i - 1].x) + WSAbs(data->fTrace[i].y - data->fTrace[i - 1].y);
		if (length * 2 <= (width + height) * 3)
		{
			// a short straight stroke (a dot, a dash)
			if (width < lineHeight * 2 && height < lineHeight * 2)
				down += down / 3;
			goto decide;
		}
	}
	if (three < width)
		down -= down / 3;
	if (lineHeight * 5 < width)
		down -= down / 3;
decide:
	above = ((two - back) - overlap) + above;
	long least = lineHeight + lineHeight / 4;
	long below = down - back;
	if (down - back < least)
		below = least;
	if (above < least)
		above = least;
	if (0 < data->f00c_lineDist)
	{
		above = data->f00c_lineDist;
		below = data->f00c_lineDist;
	}
	if (lineY - below <= (data->f044_meanY + data->f034_y1) / 2 && data->f044_meanY <= lineY + above)
		return 0;
	return 1;
}


// ROM 0x00271bac CheckForSpaceGesture__FP12ws_data_type
// Whether the stroke is a space gesture (when the control allows one):
// long and flat, beginning or ending at its left end.  ==> 0 for one
// (flag 0x10 set).
long
CheckForSpaceGesture(ws_data_type* data)
{
	long width = data->f038_width;
	if ((data->f010_controlFlags & 4) != 0 && data->f06c_lineHeight <= width * 2 && data->f03c_height * 3 <= width)
	{
		long from = (data->fTrace[0].x - data->f028_x0) * 3;
		if (from > width)
			from = (data->fTrace[data->f024_numPoints - 1].x - data->f028_x0) * 3;
		if (from <= width)
		{
			data->f020_flags |= 0x10;
			return 0;
		}
	}
	return 1;
}


// ROM 0x00271c2c WS_WriteStrokeHorzValues__FP12ws_data_type
// The line's y written under the stroke and on ahead of it (six line
// heights), moved towards the stroke's mean y - a quarter of the way for
// a small stroke, half for a big one.  ==> 1 when the stroke is left of
// where the line has got to and nothing is written.
long
WS_WriteStrokeHorzValues(ws_data_type* data)
{
	long lineY = data->f8d8_lineY[data->f028_x0 / 16];
	long lineHeight = data->f06c_lineHeight;
	long height = data->f03c_height;
	long y = data->f044_meanY;
	if (height < lineHeight / 2 && 0 < lineY)
		y = (lineY * 3 + y) / 4;
	if (0 < lineY && data->f02c_x1 < data->f07c_lineRight - data->f064_letterWidth)
		return 1;
	if (0 < lineY)
	{
		if (lineHeight <= height && data->f068_letterPitch <= data->f038_width)
			y = (lineY + y) / 2;
		else
			y = (lineY * 3 + y) / 4;
	}
	for (long x = data->f02c_x1 - 1; -1 < x; x -= 0x10)
	{
		if (data->f8d8_lineY[x / 16] != 0 && x < data->f028_x0)
			break;
		data->f8d8_lineY[x / 16] = (short) y;
	}
	long end = data->f02c_x1 + data->f06c_lineHeight * 6;
	if (kWSLineEnd < end)
		end = kWSLineEnd;
	for (long x = data->f02c_x1; x < end; x += 0x10)
		data->f8d8_lineY[x / 16] = (short) y;
	return 0;
}


#pragma mark - the net

// ROM 0x001286b4 NeuroNetWS__FPi
// Whether a gap is the space between two words, from eleven measurements
// of it and its line: the two classes' likelihoods (Rget_answer, in
// 24.8) as percentages, their difference times five (at most 100) scaled
// by the likelier one, negative for "not a space".  A gap numbered
// outside 0-249 is answered outright.
long
NeuroNetWS(long* inputs)
{
	long outputs[2];
	Rget_answer(inputs, outputs);
	long v = inputs[8];
	if (v < 0 || 0xf9 < v)
		return v < 0xfa ? -100 : 100;
	long notSpace = (outputs[0] * 100) >> 8;
	long space = (outputs[1] * 100) >> 8;
	long diff, scale;
	if (space < notSpace)
	{
		scale = -notSpace;
		diff = notSpace - space;
	}
	else
	{
		diff = space - notSpace;
		scale = space;
	}
	diff = diff * 5;
	if (100 < diff)
		diff = 100;
	return (diff * scale) / 100;
}


// ROM 0x0012874c Rget_answer__FPiPl
// For each of the two classes: the inputs turned through the class's
// 11x11 matrix, then each of its trained cells (a width and a centre)
// whose squared distance from them is within five times the width
// contributes e^(-distance/width) to the likelihood, the contributions
// combined as 1 - the product of their complements.  ==> 1.
long
Rget_answer(long* inputs, long* outputs)
{
	// the order the ROM adds the terms in, stopping at the first that
	// takes the sum past the cell's reach
	static const int kOrder[11] = { 2, 8, 9, 10, 4, 6, 0, 1, 5, 7, 3 };
	const int* matrix = rom_matrix;
	const short* cell = rom_cell;
	for (int cls = 0; cls < 2; cls++)
	{
		long product = 0x100;
		long t[11];
		for (int r = 0; r < 11; r++)
		{
			t[r] = 0;
			for (int c = 0; c < 11; c++)
				t[r] = (long) (int32_t) ((uint32_t) *matrix++ * (uint32_t) inputs[c] + (uint32_t) t[r]);
		}
		long numCells = rom_ncells[cls];
		for (long j = 0; j < numCells; j++, cell += 14)
		{
			int32_t width = (int32_t) (((uint32_t) (UShort) cell[0] << 16) | (UShort) cell[1]);
			int32_t reach = (int32_t) ((uint32_t) width * 0x500);
			// (the sum is a 32-bit word: a far input can wrap it negative
			// and so count as near, as on the machine)
			int32_t sum = 0;
			Boolean within = true;
			for (int k = 0; k < 11; k++)
			{
				int32_t d = (int32_t) ((uint32_t) t[kOrder[k]] - (uint32_t) (cell[2 + kOrder[k]] * 0x100));
				sum = (int32_t) ((uint32_t) sum + (uint32_t) FixMul32(d, d));
				if (sum > reach)
				{
					within = false;
					break;
				}
			}
			if (within)
			{
				long e = width < 1 ? (long) (int32_t) 0x80000000 : (int32_t) (0u - (uint32_t) sum) / width;
				product = FixMul32(product, 0x100 - EXP(e));
			}
		}
		outputs[cls] = 0x100 - product;
	}
	return 1;
}


// ROM 0x00128a44 EXP__Fl
// e^x in 24.8 for x from -5 to 0 (nought below, one above), interpolated
// in a table of 1/200ths.
long
EXP(long x)
{
	if (x < -0x4ff)
		return 0;
	if (x < 0)
	{
		long t = x * -200;
		long i = t >> 8;
		long f = t - i * 0x100;
		return FixMul32(0x100 - f, EXP_TABL[i]) + FixMul32(f, EXP_TABL[i + 1]);
	}
	return 0x100;
}
