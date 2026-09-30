/*
	File:		recognition/ShapeEquations.cpp

	Contains:	FindEquations: what a shape of straight sides ought to be,
				written as equations for SolveEquations - and PlugNewVals,
				which puts the solution back into the shape.

				Every side is measured (its length and its direction, in
				whole pixels and whole degrees) and the two are clustered
				(TTrend): the directions into at most eight AngClusters,
				each keeping the lengths of its sides as members (a member
				being one length and the list of sides that have it).  A
				side near level or upright makes its cluster an axis
				(Inserter); the axes are put in if the shape has none
				(StuffAxes), and the clusters are related to one another
				(RelateAngs): two about 90 degrees apart are made exactly
				perpendicular, and a cluster half way between two others is
				the axis they are mirrored in (BisectTest, MergeClusters).

				A closed shape then has its equations written over the edge
				vectors x[2i-1], x[2i] (GenAuxEqs puts the drawn ones in):

					FamilyRotEqs	sides of one cluster, and of a
									cluster and its perpendicular, keep
									their directions, and lengths in a
									ratio of about 1 or 2 are made exact
					FamilyReflEqs	sides mirrored in a cluster make
									equal angles with it
					AlignRotEqs		(a shape all of axes) level and
									upright sides stay so
					DirSumEqs		the sides add up to nothing across
									and down, so the shape stays closed

				(GenSameAngEqs, GenSlopeEqs, GenAlignEqs, GenEqEqs and
				GenSumEqs write one or two equations each), and the answer is
				whether there are any worth solving.  A shape with nothing
				to solve for, or an open one, is squared up directly instead:
				each side laid along its cluster's direction at its cluster's
				length.  Along the way the type is worked out: a triangle, a
				square or a rectangle, a parallelogram or a rhombus.

	Reconstructed from the MP2x00 US ROM (0x002231e0-0x00226a10 and
	AccessPoint at 0x00216e54); each function cites its origin.
*/

#include "ShapeGeometry.h"
#include "EdgeList.h"
#include "StrokeQueue.h"
#include "RecObject.h"
#include "FixedMath.h"
#include "Angles.h"

#include <string.h>


long	gFourSided;				// ROM 0x0c104d44 (unnamed) - FindEquations found a four-sided closed shape to solve


static inline long
WAbs(long v)
{
	return v < 0 ? (long) (int32_t) (0u - (uint32_t) v) : v;
}

// A 16.16 value rounded to a whole number, as a short.
static inline long
RoundShort(long v)
{
	return (short) ((uint32_t) (v + 0x8000) >> 16);
}

// Whether two lengths are in a ratio close to 1 or 2 (within a tenth):
// the ratio rounded is answered in *rounded, as 16.16.
static Boolean
SimpleRatio(long big, long small, Fixed* rounded)
{
	Fixed ratio = FixedDivide(ShiftLeft(big, 16), ShiftLeft(small, 16));
	Fixed whole = ShiftLeft((ratio + 0x8000) >> 16, 16);
	Fixed q = (ratio <= whole) ? FixedDivide(whole, ratio) : FixedDivide(ratio, whole);
	*rounded = whole;
	return !(q - 0x10000 > 0x199a || whole > 0x20000);
}


// ROM 0x00216e54 AccessPoint__FlP7TDArrayP9GeneralPt
void
AccessPoint(long index, TDArray* shape, GeneralPt* pt)
{
	memcpy(pt, shape->GetEntry(index), sizeof(GeneralPt));
}


#pragma mark - The equations


// ROM 0x00225330 NewCoeffs__FP8EqSystem
// A new equation of 37 nought coefficients ('cof0'); nil if there are 42
// already or no memory.  ROM BUG: the count allowed is one more than the
// 41 the system has room for.  Its fN is left for FindEquations to set.
Handle
NewCoeffs(EqSystem* system)
{
	long count = system->fCount;
	if (count > 0x29)
		return nil;
	Handle h = MakeHandle(0x94);
	NameHandle(h, 'cof0');
	if (h == nil)
		return nil;
	system->fCount = system->fCount + 1;
	system->fEqs[count].fKind = 0;
	system->fEqs[count].fCoeffs = h;
	for (long i = 0; i <= 0x24; i++)
		((Fixed*) *h)[i] = 0;
	return h;
}


// ROM 0x0022531c GenTopLevEqs__FlN21Pl
// x[i+1], x[i+2] = dx, dy (if they fit in the 75 values).
void
GenTopLevEqs(long i, long dx, long dy, long* values)
{
	if (i + 2 < 0x4b)
	{
		values[i + 1] = dx;
		values[i + 2] = dy;
	}
}


// ROM 0x002252f0 GenAuxEqs__FlP9GeneralPtT2Pl
// Side i's edge vector, from a to b, as the starting values.
void
GenAuxEqs(long i, GeneralPt* a, GeneralPt* b, long* values)
{
	GenTopLevEqs(i * 2, b->fPt.x - a->fPt.x, b->fPt.y - a->fPt.y, values);
}


// ROM 0x00225268 FindCoords__FlUcT1P7TDArrayPlT5
// Side i's edge vector as drawn (the last side of a closed shape going
// back to the first point).
void
FindCoords(long i, UByte closed, long n, TDArray* shape, long* dx, long* dy)
{
	GeneralPt a, b;
	AccessPoint(i, shape, &a);
	if (closed && n - 1 == i)
		AccessPoint(0, shape, &b);
	else
		AccessPoint(i + 1, shape, &b);
	*dx = b.fPt.x - a.fPt.x;
	*dy = b.fPt.y - a.fPt.y;
}


// ROM 0x002253b4 GenSameAngEqs__FlT1UcN41P8EqSystem
// Sides i and j parallel with lengths in the ratio b:a (their components
// swapped, when `swap`, for a perpendicular pair), c and d the signs of
// j's two components against i's: a xi = c b xj and a yi = d b yj.  True
// if there was no room.
Boolean
GenSameAngEqs(long i, long j, UByte swap, long a, long b, long c, long d, EqSystem* system)
{
	long xi = 1 + i * 2, xj = 1 + j * 2;
	long yi = 2 + i * 2, yj = 2 + j * 2;
	long first, second;
	if (swap)
	{
		first = yj;
		second = xj;
	}
	else
	{
		first = xj;
		second = yj;
	}
	Handle h = NewCoeffs(system);
	if (h == nil)
		return true;
	((Fixed*) *h)[xi] = a;
	((Fixed*) *h)[first] = (long) (int32_t) ((uint32_t) b * (uint32_t) -c);
	h = NewCoeffs(system);
	if (h == nil)
		return true;
	((Fixed*) *h)[yi] = a;
	((Fixed*) *h)[second] = (long) (int32_t) ((uint32_t) b * (uint32_t) -d);
	return false;
}


// ROM 0x0022546c GenSlopeEqs__FlT1UcT1P8EqSystem
// A relation between two sides' slopes that is not linear, recorded as an
// equation of kind 1 - which the solver does not square into its
// function, so it is only a marker.  True if there was no room.
Boolean
GenSlopeEqs(long i, long j, UByte swap, Fixed k, EqSystem* system)
{
	long xi = 1 + i * 2, xj = 1 + j * 2;
	long yi = 2 + i * 2, yj = 2 + j * 2;
	long first, second;
	if (!swap)
	{
		first = yj;
		second = xj;
	}
	else
	{
		first = xj;
		second = yj;
	}
	Handle h = NewCoeffs(system);
	if (h == nil)
		return true;
	((Fixed*) *h)[0] = k;
	((Fixed*) *h)[xi] = 0x10000;
	((Fixed*) *h)[first] = 0x10000;
	((Fixed*) *h)[yi] = 0x8000;
	((Fixed*) *h)[second] = 0x8000;
	system->fEqs[system->fCount - 1].fKind = 1;
	return false;
}


// ROM 0x00225514 GenAlignEqs__FlT1P8EqSystem
// Side i upright (axis 1: its x component nought) or level (axis 2: its
// y).  (A side's direction is measured from the vertical, so an angle
// near 0 or 180 is upright and one near 90 level.)
Boolean
GenAlignEqs(long i, long axis, EqSystem* system)
{
	Handle h = NewCoeffs(system);
	if (h == nil)
		return true;
	((Fixed*) *h)[axis + i * 2] = 0x10000;
	return false;
}


// ROM 0x002256bc GenEqEqs__FlN21UcN31P8EqSystem
// One component of side i against one of side j: a x_i = -(b c) x_j, the
// component k of j and (the other one, when `swap`) of i.
Boolean
GenEqEqs(long i, long j, long k, UByte swap, long a, long b, long c, EqSystem* system)
{
	Handle h = NewCoeffs(system);
	if (h == nil)
		return true;
	((Fixed*) *h)[k + j * 2] = (long) (int32_t) ((uint32_t) b * (uint32_t) c);
	if (swap)
		k = (k == 1) ? 2 : 1;
	((Fixed*) *h)[k + i * 2] = a;
	return false;
}


// ROM 0x00225740 GenSumEqs__FlPlT1P8EqSystem
// The sides' components (1: across, 2: down) added up with the weights
// given come to nought.
Boolean
GenSumEqs(long last, long* weights, long component, EqSystem* system)
{
	Handle h = NewCoeffs(system);
	if (h == nil)
		return true;
	for (long k = 0; k <= last; k++)
		if (weights[k] != 0)
			((Fixed*) *h)[component + k * 2] = ShiftLeft(weights[k], 16);
	return false;
}


#pragma mark - The angle clusters


// A side's list of sides can hold eight; the ROM does not check, and a
// ninth goes on into whatever follows.
static inline void
AddToList(SideList* list, long side)
{
	long count = list->fCount;
	list->fCount = count + 1;
	((long*) list)[1 + count] = side;
}


// ROM 0x00225810 Inserter__FlN21PlPUcT4P10AngClusterP6TTrendT1P7SideMap
// Side `side`, of the length and direction given, put into the angle
// cluster at *index.  The first side a cluster gets decides whether it is
// an axis - within 8 degrees of upright (1) or level (2) - and an axis
// cluster next to another of the same axis is merged into it (in the trend
// too; the side map's cluster numbers follow).  Any other direction
// clears *allAxes.  The side then joins the member of its length, or
// starts one, the members kept in order of length.
void
Inserter(long side, long length, long angle, long* index, UByte* allAxes, long* nClusters,
		 AngCluster* clusters, TTrend* trend, long sides, SideMap* map)
{
	AngCluster* cl = &clusters[*index];
	if (cl->fMembers == 0)
	{
		if (WAbs(angle) <= 8 || WAbs(angle - 180) <= 8)
			cl->fAxis = 1;
		else if (WAbs(angle - 90) <= 8)
			cl->fAxis = 2;
		else
		{
			*allAxes = 0;
			if (cl->fAxis == 0)
				goto insert;
		}
		long i = *index;
		if (i > 0 && clusters[i - 1].fAxis == cl->fAxis)
		{
			for (; i < *nClusters - 1; i++)
				clusters[i] = clusters[i + 1];
			*nClusters = *nClusters - 1;
			trend->Merge(*index - 1, trend->At(*index - 1), trend->At(*index));
			*index = *index - 1;
			cl = &clusters[*index];
			for (long s = 0; s < sides; s++)
				if (map[s].fCluster > *index)
					map[s].fCluster--;
		}
		else if (cl->fAxis != 0 && i < *nClusters - 1 && clusters[i + 1].fAxis == cl->fAxis)
		{
			for (; i < *nClusters - 1; i++)
				clusters[i] = clusters[i + 1];
			*nClusters = *nClusters - 1;
			trend->Merge(*index, trend->At(*index), trend->At(*index + 1));
			for (long s = 0; s < sides; s++)
				if (map[s].fCluster > *index)
					map[s].fCluster--;
		}
	}

insert:
	long n = cl->fMembers;
	if (n < 0)
		return;
	for (long j = 0; j <= n; j++)
	{
		if (j == n || cl->fMember[j].fLength > length)
		{
			for (long k = n - 1; k >= j; k--)
				cl->fMember[k + 1] = cl->fMember[k];
			cl->fMember[j].fList = n;
			cl->fMember[j].fBase = -1;
			cl->fMember[j].fMultiple = 0;
			cl->fMember[j].fLength = length;
			SideList* list = &cl->fLists[n];
			list->fSides[0] = side;
			list->fCount = 1;
			cl->fMembers = cl->fMembers + 1;
			cl->fAngle = angle;
			return;
		}
		if (cl->fMember[j].fLength == length)
		{
			AddToList(&cl->fLists[cl->fMember[j].fList], side);
			return;
		}
	}
}


// ROM 0x00225bb8 PlaceAngle__FPlN41
// A side's direction (a line's, so taken modulo 180, and measured from
// the vertical) brought into the
// window the ones before it are in, which is widened to take it: the
// window starts as (-45, 225] with the inner (45, 135] marking where it
// can still move.
void
PlaceAngle(long* angle, long* low, long* high, long* innerLow, long* innerHigh)
{
	long a = *angle;
	if (a > *high)
		*angle = a - 180;
	else if (a <= *low)
		*angle = a + 180;
	a = *angle;
	if (a > *innerHigh)
	{
		long wrapped = a - 180;
		if (*high - a > wrapped - *low)
		{
			*low = wrapped;
			*innerHigh = *angle;
			return;
		}
		*high = a;
		*angle = *angle - 180;
		*innerLow = *angle;
		return;
	}
	if (a > *innerLow)
		return;
	if ((*high - a) - 180 > a - *low)
	{
		*low = a;
		*angle = *angle + 180;
		*innerHigh = *angle;
		return;
	}
	*high = a + 180;
	*innerLow = *angle;
}


// ROM 0x00225c8c AxisAngle__FlPl
// A direction snapped onto the axis given, if it is within 8 degrees of
// it: upright is 0 or +/-180, level +/-90.  Whether it was.
Boolean
AxisAngle(long axis, long* angle)
{
	if (axis == 1)
	{
		long a = *angle;
		if (a >= 172)
		{
			*angle = 180;
			return true;
		}
		if (a <= -172)
		{
			*angle = -180;
			return true;
		}
		if (WAbs(a) <= 8)
		{
			*angle = 0;
			return true;
		}
		return false;
	}
	if (axis == 2)
	{
		long a = *angle;
		if (a >= 82)
		{
			*angle = 90;
			return true;
		}
		if (a > -82)
			return false;
		*angle = -90;
		return true;
	}
	return false;
}


// An empty axis cluster of the angle and axis given put in at index,
// the clusters after it and the side map's numbers moving up one.
static void
InsertAxis(long* nClusters, AngCluster* clusters, long sides, SideMap* map, long angle, long axis)
{
	long n = *nClusters;
	long i;
	for (i = 0; i < n; i++)
		if (clusters[i].fAngle > (axis == 1 ? 0 : 90))
			break;
	for (long k = n - 1; k >= i; k--)
		clusters[k + 1] = clusters[k];
	clusters[i].fAngle = angle;
	clusters[i].fAxis = axis;
	clusters[i].fPerpendicular = -1;
	clusters[i].fMembers = 0;
	clusters[i].fMirror = -1;
	clusters[i].fPairs = 0;
	for (long s = 0; s < sides; s++)
		if (map[s].fCluster >= i)
			map[s].fCluster++;
	*nClusters = *nClusters + 1;
}


// ROM 0x00225d04 StuffAxes__FPlP10AngClusterlP7SideMap
// An upright and a level cluster put in, sides or none, if the shape has
// not got them, so its other directions can be related to them.  Nine
// clusters (too many) if that would make more than eight.
void
StuffAxes(long* nClusters, AngCluster* clusters, long sides, SideMap* map)
{
	Boolean hasUpright = false, hasLevel = false;
	long n = *nClusters;
	for (long i = 0; i < n; i++)
	{
		if (!hasLevel)
			hasLevel = clusters[i].fAxis == 2;
		if (!hasUpright)
			hasUpright = clusters[i].fAxis == 1;
	}
	long total = n;
	if (!hasUpright)
		total++;
	if (!hasLevel)
		total++;
	if (total > 8)
	{
		*nClusters = 9;
		return;
	}
	if (!hasUpright)
		InsertAxis(nClusters, clusters, sides, map, 0, 1);
	if (hasLevel)
		return;
	InsertAxis(nClusters, clusters, sides, map, 90, 2);
}


// ROM 0x0022632c BisectTest__FP10AngClusterlN22
// Whether cluster j is half way between clusters i and k (to within 8
// degrees), neither of those being an axis.
Boolean
BisectTest(AngCluster* clusters, long i, long j, long k)
{
	if (clusters[i].fAxis != 0 || clusters[k].fAxis != 0)
		return false;
	long d = (clusters[j].fAngle - clusters[i].fAngle) - (clusters[k].fAngle - clusters[j].fAngle);
	return WAbs(d) <= 8;
}


// ROM 0x00226388 MergeClusters__FlPlP10AngClusterT1P7SideMap
// Cluster index+1 merged into cluster index, their directions averaged:
// each of its sides put in again (Inserter) and the rest moved down.
void
MergeClusters(long index, long* nClusters, AngCluster* clusters, long sides, SideMap* map)
{
	AngCluster* cl = &clusters[index];
	AngCluster* next = cl + 1;
	cl->fAngle = (cl->fAngle + next->fAngle) / 2;
	UByte scratch;
	for (long m = 0; m < next->fMembers; m++)
	{
		SideList* list = &next->fLists[next->fMember[m].fList];
		long length = next->fMember[m].fLength;
		for (long k = 0; k < list->fCount; k++)
			Inserter(((long*) list)[1 + k], length, cl->fAngle, &index, &scratch, nClusters, clusters, nil, sides, map);
	}
	long i;
	for (i = index + 1; i < *nClusters - 1; i++)
		clusters[i] = clusters[i + 1];
	*nClusters = *nClusters - 1;
	for (long s = 0; s < sides; s++)
		if (map[s].fCluster > index)
			map[s].fCluster--;
	for (long c = 0; c < *nClusters; c++)
	{
		long p = clusters[c].fPerpendicular;
		if (p != -1 && p > index)
			clusters[c].fPerpendicular = p - 1;
		p = clusters[c].fMirror;
		if (p != -1 && p > index)
			clusters[c].fMirror = p - 1;
	}
}


// ROM 0x00225f50 RelateAngs__FPlP10AngClusterlP7SideMap
// The clusters related: each one with no relation yet is paired with the
// first after it (within 98 degrees) that is 82 to 98 degrees on - made
// exactly perpendicular, about their mean or onto the axes - or else
// with a pair it is the bisector of: the two either side are set
// symmetrically about it (merging a neighbour that is really the same
// direction) and recorded as its mirrored pair.  A cluster left alone
// gets its slope.  False when a direction meant to be an axis could not
// be snapped onto it.
Boolean
RelateAngs(long* nClusters, AngCluster* clusters, long sides, SideMap* map)
{
	for (long i = 0; i < *nClusters; i++)
	{
		AngCluster* cl = &clusters[i];
		if (!(cl->fMirror < 0 && cl->fPerpendicular < 0))
			continue;
		Boolean failed = false;
		long j = i + 1;
		if (j < *nClusters)
		{
			for (; j < *nClusters; j++)
			{
				AngCluster* cj = &clusters[j];
				if (cj->fMirror >= 0)
					continue;
				long d = cj->fAngle - cl->fAngle;
				if (d > 98)
					break;
				if (cj->fPerpendicular == -1)
				{
					if (cl->fAxis != 0 && cj->fAxis != 0)
					{
						AxisAngle(cl->fAxis, &cl->fAngle);
						failed = !AxisAngle(cj->fAxis, &cj->fAngle);
					}
					else
					{
						if (d < 82)
							goto bisector;
						long mean = (cl->fAngle + cj->fAngle - 90) / 2;
						cl->fAngle = mean;
						cj->fAngle = mean + 90;
						cl->fSlope = SlopeFromAngle((short) cl->fAngle);
						cj->fSlope = SlopeFromAngle((short) cj->fAngle);
					}
					cj->fPerpendicular = i;
					cl->fPerpendicular = j;
					break;
				}
			bisector:
				if (cj->fAxis == 0)
					continue;
				for (long k = j + 1; k < *nClusters; k++)
				{
					if (!BisectTest(clusters, i, j, k))
						continue;
					long dk = clusters[k].fAngle - cl->fAngle;
					if (cl->fMember[0].fLength != clusters[k].fMember[0].fLength && dk >= 82 && dk <= 98)
						break;
					if (BisectTest(clusters, i + 1, j, k))
					{
						MergeClusters(i, nClusters, clusters, sides, map);
						j--;
						k--;
					}
					else if (k + 1 < *nClusters && BisectTest(clusters, i, j, k + 1))
						MergeClusters(k, nClusters, clusters, sides, map);
					AngCluster* ck = &clusters[k];
					cj = &clusters[j];
					long half = (ck->fAngle - cl->fAngle) / 2;
					failed = !AxisAngle(cj->fAxis, &cj->fAngle);
					cl->fAngle = cj->fAngle - half;
					ck->fAngle = cj->fAngle + half;
					cl->fSlope = SlopeFromAngle((short) cl->fAngle);
					ck->fSlope = SlopeFromAngle((short) ck->fAngle);
					long pairs = cj->fPairs + 1;
					cj->fPairs = pairs;
					((long*) cj)[70 + pairs * 2] = i;		// fPair[2(pairs-1)], unchecked
					((long*) cj)[71 + pairs * 2] = k;
					cl->fPerpendicular = -1;
					ck->fPerpendicular = -1;
					cl->fMirror = j;
					ck->fMirror = j;
					break;
				}
				if (cl->fMirror >= 0 || failed)
					break;
			}
			if (failed)
				return false;
		}
		if (cl->fAxis == 0 && cl->fPairs == 0 && cl->fPerpendicular == -1 && cl->fMirror == -1)
			cl->fSlope = SlopeFromAngle((short) cl->fAngle);
	}
	return true;
}


#pragma mark - Writing the equations


// ROM 0x0022483c FamilyRotEqs__FP10AngClusterlUcT2P7TDArrayP7SideMapPUcP8EqSystemPl
// The equations of cluster c and its perpendicular (once, from the first
// of the two): every side of a member parallel to the member's first side
// at the same length; the first side of every other member a whole
// multiple (1 or 2) of the first member's, or failing that only parallel;
// and the perpendicular's first side at right angles to c's, its length
// likewise.  An axis cluster's sides are held to their axis as well.  Each
// side's signs are recorded in the side map.  *families counts the
// clusters with sides that are perpendicular families: -1 one, 3 more, and
// for the second of a pair 1 if the pair's lengths agree (a square), 2 if
// not.  True if there was no room.
Boolean
FamilyRotEqs(AngCluster* clusters, long c, UByte closed, long n, TDArray* shape, SideMap* map,
			 UByte* nonlinear, EqSystem* system, long* families)
{
	AngCluster* cl = &clusters[c];
	long partner = cl->fPerpendicular;
	if (partner >= 0 && partner < c)
	{
		if (cl->fMembers == 0)
			return false;
		if (*families == -1 && clusters[partner].fMembers != 0)
			*families = (cl->fMember[0].fLength != clusters[partner].fMember[0].fLength) ? 2 : 1;
		else
			*families = 3;
		return false;
	}
	if (cl->fMembers != 0)
		*families = (*families == 0) ? -1 : 3;

	long firstMembers = 0;
	long side0 = 0, dir0 = 0, len0 = 0, small = 0;
	long first = 0, dirFirst = 0, lenFirst = 0;
	long axis = 0;
	for (long pass = 0; pass < 2; pass++)
	{
		if (c == -1)
			break;
		AngCluster* cc = &clusters[c];
		if (pass == 0)
			firstMembers = cc->fMembers;
		for (long m = 0; m < cc->fMembers; m++)
		{
			AngMember* mem = &cc->fMember[m];
			SideList* list = &cc->fLists[mem->fList];
			long side = list->fSides[0];
			long dx, dy;
			FindCoords(side, closed, n, shape, &dx, &dy);
			Boolean across = WAbs(dx) > WAbs(dy);
			map[side].fSignX = Signum(dx);
			map[side].fSignY = Signum(dy);
			long dir = Signum(across ? dx : dy);
			Fixed ratio;
			if (m == 0)
			{
				dirFirst = dir;
				first = side;
				lenFirst = cc->fMember[0].fLength;
				axis = cc->fAxis;
				if (pass == 0 || firstMembers == 0)
				{
					dir0 = dirFirst;
					side0 = first;
					len0 = lenFirst;
					small = lenFirst;
					if (axis != 0 && GenAlignEqs(side0, axis, system))
						return true;
				}
				else
				{
					long big = lenFirst;
					if (lenFirst < len0)
					{
						big = len0;
						small = lenFirst;
					}
					if (SimpleRatio(big, small, &ratio))
					{
						long sgn = (dir0 == dirFirst) ? 1 : -1;
						long a = ratio, b = 0x10000;
						if (small != len0)
						{
							a = 0x10000;
							b = ratio;
						}
						if (axis == 0)
						{
							if (across ? GenSameAngEqs(side0, first, 1, a, b, -sgn, sgn, system)
									   : GenSameAngEqs(side0, first, 1, a, b, sgn, -sgn, system))
								return true;
						}
						else
						{
							if (GenEqEqs(side0, first, axis == 1 ? 2 : 1, 1, a, b, -sgn, system))
								return true;
							if (GenAlignEqs(first, axis, system))
								return true;
						}
					}
					else if (axis == 0)
					{
						if (GenSlopeEqs(side0, first, 1, 0x10000, system))
							return true;
						*nonlinear = 1;
					}
					else if (GenAlignEqs(first, axis, system))
						return true;
				}
			}
			else
			{
				if (SimpleRatio(mem->fLength, lenFirst, &ratio))
				{
					long sgn = (dir == dirFirst) ? 1 : -1;
					if (axis == 0)
					{
						if (GenSameAngEqs(first, side, 0, ratio, 0x10000, sgn, sgn, system))
							return true;
					}
					else
					{
						if (GenEqEqs(first, side, axis == 1 ? 2 : 1, 0, ratio, 0x10000, -sgn, system))
							return true;
						if (GenAlignEqs(side, axis, system))
							return true;
					}
					mem->fBase = 0;
					mem->fMultiple = ratio >> 16;
					mem->fLength = lenFirst * (ratio >> 16);
					for (long k = 0; k < list->fCount; k++)
						map[((long*) list)[1 + k]].fLength = mem->fLength;
				}
				else if (axis == 0)
				{
					if (cc->fMirror == -1 || cc->fMirror > c)
					{
						if (GenSlopeEqs(first, side, 0, -0x10000, system))
							return true;
						*nonlinear = 1;
					}
				}
				else if (GenAlignEqs(side, axis, system))
					return true;
			}

			// the member's other sides, parallel to its first at its length
			for (long k = 1; k < list->fCount; k++)
			{
				long s = ((long*) list)[1 + k];
				FindCoords(s, closed, n, shape, &dx, &dy);
				map[s].fSignX = Signum(dx);
				map[s].fSignY = Signum(dy);
				long sgn = (Signum(across ? dx : dy) == dir) ? 1 : -1;
				if (axis == 0)
				{
					if (GenSameAngEqs(side, s, 0, 0x10000, 0x10000, sgn, sgn, system))
						return true;
				}
				else
				{
					if (GenEqEqs(side, s, axis == 1 ? 2 : 1, 0, 0x10000, 0x10000, -sgn, system))
						return true;
					if (GenAlignEqs(s, axis, system))
						return true;
				}
			}
		}
		c = cc->fPerpendicular;
	}
	return false;
}


// ROM 0x00224f74 FamilyReflEqs__FP10AngClusterlUcT2P7TDArrayPUcP8EqSystem
// For an axis cluster that pairs of clusters are mirrored in: the first
// side of each pair of members of the same length made to meet the axis
// at equal angles (the signs of the components by which way the two
// run); a pair with no length in common gets only the non-linear slope
// marker.  True if there was no room.
Boolean
FamilyReflEqs(AngCluster* clusters, long c, UByte closed, long n, TDArray* shape, UByte* nonlinear, EqSystem* system)
{
	AngCluster* cl = &clusters[c];
	if (cl->fAxis == 0)
		return false;
	for (long t = 0; t < cl->fPairs; t++)
	{
		AngCluster* a = &clusters[cl->fPair[t * 2]];
		AngCluster* b = &clusters[cl->fPair[t * 2 + 1]];
		Boolean done = false;
		for (long i = 0; i < a->fMembers; i++)
		{
			long len = a->fMember[i].fLength;
			long listA = 0, listB = 0;
			long j;
			for (j = 0; j < b->fMembers; j++)
			{
				long lb = b->fMember[j].fLength;
				if (lb > len)
				{
					j = b->fMembers;
					break;
				}
				if (lb == len)
				{
					listA = a->fMember[i].fList;
					listB = b->fMember[j].fList;
					break;
				}
			}
			if (j >= b->fMembers)
				continue;
			if (done && a->fMember[i].fBase != -1)
			{
				done = true;
				continue;
			}
			long s1 = a->fLists[listA].fSides[0];
			long s2 = b->fLists[listB].fSides[0];
			long dx, dy;
			FindCoords(s1, closed, n, shape, &dx, &dy);
			Boolean across = WAbs(dx) > WAbs(dy);
			UByte sign1 = (UByte) Signum(across ? dx : dy);
			FindCoords(s2, closed, n, shape, &dx, &dy);
			UByte sign2 = (UByte) Signum(across ? dx : dy);
			Boolean same = sign2 == sign1;
			if (same == across)
			{
				if (GenSameAngEqs(s1, s2, 0, 0x10000, 0x10000, 1, -1, system))
					return true;
			}
			else
			{
				if (GenSameAngEqs(s1, s2, 0, 0x10000, 0x10000, -1, 1, system))
					return true;
			}
			done = true;
		}
		if (!done)
		{
			if (GenSlopeEqs(a->fLists[a->fMember[0].fList].fSides[0], b->fLists[b->fMember[0].fList].fSides[0], 0, 0x10000, system))
				return true;
			*nonlinear = 1;
		}
	}
	return false;
}


// ROM 0x00226584 AlignRotEqs__FP10AngClusterlP7TDArrayP7SideMapP8EqSystem
// A shape of upright and level sides only (its two clusters the axes):
// every side held to its axis, the sides of a member equal in length,
// and the members' first sides whole multiples of one another where they
// nearly are - within a cluster and between the two.  True if there was no
// room, or a cluster is not an axis.
Boolean
AlignRotEqs(AngCluster* clusters, long n, TDArray* shape, SideMap* map, EqSystem* system)
{
	long side0 = 0, dir0 = 0, len0 = 0, small = 0;
	long first = 0, dirFirst = 0, lenFirst = 0;
	for (long c = 0; c < 2; c++)
	{
		AngCluster* cl = &clusters[c];
		for (long m = 0; m < cl->fMembers; m++)
		{
			AngMember* mem = &cl->fMember[m];
			SideList* list = &cl->fLists[mem->fList];
			long side = list->fSides[0];
			long dx, dy;
			FindCoords(side, 1, n, shape, &dx, &dy);
			map[side].fSignY = 0;
			map[side].fSignX = 0;
			long axis = cl->fAxis;
			long sign;
			if (axis == 1)
			{
				sign = Signum(dy);
				map[side].fSignY = sign;
			}
			else if (axis == 2)
			{
				sign = Signum(dx);
				map[side].fSignX = sign;
			}
			else
				return true;
			Fixed ratio;
			if (m == 0)
			{
				dirFirst = sign;
				first = side;
				lenFirst = cl->fMember[0].fLength;
				if (c == 0)
				{
					dir0 = dirFirst;
					side0 = first;
					len0 = lenFirst;
					small = lenFirst;
					if (GenAlignEqs(side0, cl->fAxis, system))
						return true;
				}
				else
				{
					long big = lenFirst;
					if (lenFirst < len0)
					{
						big = len0;
						small = lenFirst;
					}
					if (SimpleRatio(big, small, &ratio))
					{
						long sgn = (dir0 == dirFirst) ? 1 : -1;
						long a = ratio, b = 0x10000;
						if (small != len0)
						{
							a = 0x10000;
							b = ratio;
						}
						if (GenEqEqs(side0, first, axis == 1 ? 2 : 1, 1, a, b, -sgn, system))
							return true;
					}
					if (GenAlignEqs(first, axis, system))
						return true;
				}
			}
			else
			{
				if (SimpleRatio(mem->fLength, lenFirst, &ratio))
				{
					long sgn = (sign == dirFirst) ? 1 : -1;
					if (GenEqEqs(first, side, axis == 1 ? 2 : 1, 0, ratio, 0x10000, -sgn, system))
						return true;
					if (GenAlignEqs(side, axis, system))
						return true;
					mem->fBase = 0;
					mem->fMultiple = ratio >> 16;
					mem->fLength = lenFirst * (ratio >> 16);
					for (long k = 0; k < list->fCount; k++)
						map[((long*) list)[1 + k]].fLength = mem->fLength;
				}
				else if (GenAlignEqs(side, axis, system))
					return true;
			}

			for (long k = 1; k < list->fCount; k++)
			{
				long s = ((long*) list)[1 + k];
				FindCoords(s, 1, n, shape, &dx, &dy);
				map[s].fSignY = 0;
				map[s].fSignX = 0;
				long v;
				if (cl->fAxis == 1)
				{
					v = Signum(dy);
					map[s].fSignY = v;
				}
				else if (cl->fAxis == 2)
				{
					v = Signum(dx);
					map[s].fSignX = v;
				}
				else
					return true;
				long sgn = (v == sign) ? 1 : -1;
				if (GenEqEqs(side, s, axis == 1 ? 2 : 1, 0, 0x10000, 0x10000, -sgn, system))
					return true;
				if (GenAlignEqs(s, axis, system))
					return true;
			}
		}
	}
	return false;
}


// One component's closing sums: side i's weight in sums[], sides of one
// length and direction sharing the weight of the first of them, a side
// that is a whole multiple of another folded into it, and mirrored pairs
// of equal length counted together.  sign is the side map's sign field
// for the component (fSignX or fSignY).
static void
AddToSums(long i, long* sums, long* last, SideMap* map, AngCluster* clusters, long SideMap::* sign)
{
	long cIdx = map[i].fCluster;
	AngCluster* cl = &clusters[cIdx];
	long mIdx = map[i].fMember;
	Boolean found = false;
	if (*last >= 0)
	{
		AngMember* mem = &cl->fMember[mIdx];
		for (long k = 0; k <= *last; k++)
		{
			if (sums[k] == 0)
				continue;
			long ck = map[k].fCluster;
			long mk = map[k].fMember;
			Boolean sameLength = mem->fLength == clusters[ck].fMember[mk].fLength;
			if (cIdx == ck)
			{
				if (mIdx < mk && cl->fMember[mk].fBase == mIdx)
				{
					// side k is a multiple of this one: it is folded in here
					sums[i] = 1;
					long sgn = (map[i].*sign == map[k].*sign) ? 1 : -1;
					sums[i] = cl->fMember[mk].fMultiple * (sums[k] * sgn) + 1;
					sums[k] = 0;
					if (i > k)
						*last = i;
					return;
				}
				if (mk < mIdx && mem->fBase == mk)
				{
					long sgn = (map[i].*sign == map[k].*sign) ? 1 : -1;
					sums[k] = mem->fMultiple * sgn + sums[k];
					return;
				}
				if (sameLength)
					found = true;
			}
			else if (sameLength && cl->fMirror >= 0)
			{
				AngCluster* mirror = &clusters[cl->fMirror];
				long pairs = mirror->fPairs;
				for (long p = 0; p < pairs; p++)
				{
					if (mirror->fPair[p * 2] == cIdx)
					{
						found = mirror->fPair[p * 2 + 1] == ck;
						break;
					}
					if (mirror->fPair[p * 2 + 1] == cIdx)
					{
						found = mirror->fPair[p * 2] == ck;
						break;
					}
				}
			}
			if (found)
			{
				sums[k] = (map[i].*sign == map[k].*sign) ? sums[k] + 1 : sums[k] - 1;
				return;
			}
		}
	}
	sums[i]++;
	*last = i;
}


// Whether exactly two of the sums are not nought, and they are equal.
static long
CountSums(long* sums, long last, long* firstValue)
{
	long count = 0;
	for (long k = 0; k <= last; k++)
	{
		if (sums[k] == 0)
			continue;
		count++;
		if (count == 1)
		{
			*firstValue = sums[k];
			continue;
		}
		if (count > 2 || sums[k] != *firstValue)
		{
			count++;
			break;
		}
	}
	return count;
}


// ROM 0x00223f58 DirSumEqs__FlP7SideMapP10AngClusterPUcT4P8EqSystem
// The closing equations of a closed shape: its sides' components add up
// to nothing across (all but the upright sides) and down (all but the
// level ones), with sides the other equations already tie together
// counted as one.  When one of the sums comes down to just two sides of
// equal weight, and there is a non-linear slope marker, the marker is
// replaced by a linear equation making those two parallel; more than two
// sides in a marker is an error.  *nonlinear is left saying whether a
// marker remains; *closing is set when a sum is written.
Boolean
DirSumEqs(long n, SideMap* map, AngCluster* clusters, UByte* nonlinear, UByte* closing, EqSystem* system)
{
	long across[15], down[15];
	for (long k = 0; k < 15; k++)
	{
		down[k] = 0;
		across[k] = 0;
	}
	long lastDown = 0, lastAcross = 0;
	for (long i = 0; i < n; i++)
	{
		long axis = clusters[map[i].fCluster].fAxis;
		if (axis != 1)
			AddToSums(i, across, &lastAcross, map, clusters, &SideMap::fSignX);
		if (axis != 2)
			AddToSums(i, down, &lastDown, map, clusters, &SideMap::fSignY);
	}

	long firstValue = 0;
	long countAcross = CountSums(across, lastAcross, &firstValue);
	long countDown = CountSums(down, lastDown, &firstValue);
	long kind = (countAcross == 2) ? 1 : (countDown == 2) ? 2 : 0;
	UByte remains = 0;
	if (kind != 0 && *nonlinear)
	{
		Boolean found = false;
		long which = 0;
		long idx[3];
		for (long e = 0; e < system->fCount; e++)
		{
			Equation* eq = &system->fEqs[e];
			if (eq->fKind == 0)
				continue;
			if (found)
			{
				remains = 1;
				break;
			}
			long count = 0;
			Boolean usable = true;
			for (long j = 0; j < n; j++)
			{
				Fixed* c = (Fixed*) *eq->fCoeffs;	// (Fixed: a coefficient is the ARM's word - NewCoeffs)
				long v = c[kind + j * 2];
				if (v == 0)
					continue;
				if ((count == 1 && v == c[kind + idx[1] * 2])
				 || (kind == 1 && across[j] == 0)
				 || (kind == 2 && down[j] == 0))
				{
					usable = false;
					break;
				}
				count++;
				if (count > 2)
					return true;
				idx[count] = j;
			}
			if (usable)
			{
				found = true;
				which = e;
			}
			else
				remains = 1;
		}
		if (found)
		{
			DeleteHandle(system->fEqs[which].fCoeffs);
			system->fCount--;
			long e;
			for (e = which; e < system->fCount; e++)
				system->fEqs[e] = system->fEqs[e + 1];
			system->fEqs[e].fCoeffs = nil;
			long c, d;
			if (map[idx[1]].fSignX == map[idx[2]].fSignX)
			{
				c = 1;
				d = -1;
			}
			else
			{
				d = (map[idx[1]].fSignY == map[idx[2]].fSignY) ? 1 : -1;
				c = -1;
			}
			if (GenSameAngEqs(idx[1], idx[2], 0, 0x10000, 0x10000, c, d, system))
				return true;
			if (kind == 1)
				goto downSum;
			countDown = 0;
		}
	}
	if (countAcross != 0)
	{
		if (GenSumEqs(lastAcross, across, 1, system))
			return true;
		*closing = 1;
	}
downSum:
	if (countDown != 0)
	{
		if (GenSumEqs(lastDown, down, 2, system))
			return true;
		*closing = 1;
	}
	*nonlinear = remains;
	return false;
}


#pragma mark - FindEquations


// ROM 0x002231e0 FindEquations__FP17TGeneralShapeUnitPlP8EqSystemP6GSTypePUlT2
// See the file's head.  `values` gets the edge vectors as drawn; *type,
// *score and *angle what the shape is; true when there are equations to
// solve (a closed shape whose sides are tied together), false when the
// shape was squared up directly or is not one of straight sides.
Boolean
FindEquations(TGeneralShapeUnit* unit, long* values, EqSystem* system, long* type, ULong* score, long* angle)
{
	Boolean failed = false;
	gFourSided = 0;
	long kind = kShapeNothing;
	ULong quality = 10000;
	Boolean solvable = false;
	TDArray* shape = unit->GetGeneralShape();
	long n = shape->Count();
	GeneralPt p0, cur, last;
	AccessPoint(0, shape, &p0);
	AccessPoint(0, shape, &cur);
	AccessPoint(n - 1, shape, &last);
	UByte closed = (cur.fPt.x == last.fPt.x && cur.fPt.y == last.fPt.y);
	long gap = CheapDistPoint(&cur.fPt, &last.fPt);
	UByte lastCurved = last.f09;
	if (closed)
		n = n - 1;
	*angle = -0x10000;
	// DEVIATION: room for 18 sides where the ROM's 0x1a4 bytes have room
	// for 15 - a shape of 16 or 17 sides went on past the end of the block.
	// The ROM's 0x1a4 is 15 lengths and 15 angles (a word each) and then 15
	// SideMaps of 20 bytes; a SideMap holds five `long`s, which are wider
	// than the ARM's word on an LP64 host, so the block is sized from the
	// host's own types rather than from that count (romsizes.py --lp64).
	Handle sidesH = MakeHandle(30 * sizeof(long) + 18 * sizeof(SideMap));
	NameHandle(sidesH, 'GSSA');
	TTrend* lengths = TTrend::Make(6);
	TTrend* angles = TTrend::Make(7);
	AngCluster clusters[8];
	// (the ROM's are on its stack, set only where they are used)
	memset(clusters, 0, sizeof(clusters));
	long lo = -45, hi = 225, innerLo = 45, innerHi = 135;
	long sides = 0;
	long nLen, nAng;
	UByte allAxes;
	UByte nonlinear, closing;
	if (sidesH == nil || lengths == nil || angles == nil)
		goto finish;
	{
		// ROM QUIRK: the lengths and the angles are two arrays of 15
		// longs one after the other, and a shape of more sides than that
		// has its lengths run on into its angles.
		long* L = (long*) *sidesH;
		if (n < 0x25 && n > 0)
		{
			for (long i = 0; i < n; )
			{
				long next = i + 1;
				long at = next;
				if (n - 1 == i)
				{
					if (!closed)
					{
						kind = kShapeOpen;
						goto measured;
					}
					at = 0;
				}
				GeneralPt pt;
				AccessPoint(at, shape, &pt);
				GenAuxEqs(i, &cur, &pt, values);
				if (cur.f09 != 0 || pt.f09 != 0)
				{
					// a curve in it: not a shape of straight sides
					if (!closed)
					{
						kind = kShapeOpenCurve;
						quality = 600;
					}
					else
					{
						kind = kShapeClosedCurve;
						quality = lastCurved ? 400 : 500;
					}
					goto finish;
				}
				long len = RoundShort(CheapDistPoint(&cur.fPt, &pt.fPt));
				long ang = RoundShort(PtsToAngle(&pt.fPt, &cur.fPt, 0x10000));
				if (ang == 180)
					ang = 179;
				else if (ang < -180 || ang >= 180)
					goto finish;
				PlaceAngle(&ang, &lo, &hi, &innerLo, &innerHi);
				failed = lengths->AddToTrend(len, nil, 1);
				if (failed)
					goto finish;
				failed = angles->AddToTrend(ang, nil, 1);
				if (failed)
					goto finish;
				L[sides] = len;
				L[15 + sides] = ang;
				sides++;
				cur = pt;
				i = next;
			}
		}
		kind = closed ? kShapeClosed : kShapeOpen;
	measured:
		quality = 600;
		SideMap* map = (SideMap*) (L + 30);
		nAng = angles->Count();
		nLen = lengths->Count();
		if (n * 2 >= 0x24)
			goto finish;
		if (nAng > 8 || nLen > 5)
			goto finish;
		if (unit->fGroupInfo->f50[1] >= 0)
			goto finish;
		for (long c = 0; c < nAng; c++)
		{
			clusters[c].fPerpendicular = -1;
			clusters[c].fMembers = 0;
			clusters[c].fMirror = -1;
			clusters[c].fAxis = 0;
			clusters[c].fSlope = 0;
			clusters[c].fPairs = 0;
		}
		allAxes = 1;
		for (long i = 0; i < sides; i++)
		{
			long angIdx = angles->FindCluster(L[15 + i]);
			long lenIdx = lengths->FindCluster(L[i]);
			if (angIdx == -1 || lenIdx == -1)
				goto finish;
			Cluster* lenCluster = lengths->At(lenIdx);
			Cluster* angCluster = angles->At(angIdx);
			Inserter(i, lenCluster->fMean, angCluster->fMean, &angIdx, &allAxes, &nAng, clusters, angles, sides, map);
			map = (SideMap*) ((long*) *sidesH + 30);
			map[i].fCluster = angIdx;
			map[i].fLength = lenCluster->fMean;
		}
		angles->Dispose();
		angles = nil;
		lengths->Dispose();
		lengths = nil;
		if (sides > 1 && nAng == 1)
			goto finish;
		if (closed && sides == 3 && nAng <= 2)
			goto finish;

		if (nAng == 1)
		{
			// one direction: a straight line, upright or level if it
			// nearly is
			GeneralPt* end = (GeneralPt*) shape->GetEntry(1);
			if (clusters[0].fAxis == 1)
			{
				end->fPt.x = p0.fPt.x;
				*angle = 0;
			}
			else if (clusters[0].fAxis == 2)
			{
				end->fPt.y = p0.fPt.y;
				*angle = 90 << 16;
			}
			else
			{
				long a = ShiftLeft(clusters[0].fAngle, 16);
				*angle = a;
				if (a < 0)
					*angle = a + (180 << 16);
			}
			kind = kShapeLine;
			if (sides == 1)
				quality = 500;
			else
			{
				quality = 1000;
				shape->CutToIndex(2);
			}
			goto finish;
		}

		if (!allAxes)
		{
			StuffAxes(&nAng, clusters, sides, map);
			if (nAng > 8)
				goto finish;
			if (!RelateAngs(&nAng, clusters, sides, map))
				goto finish;
		}

		// the score: fewer lengths and directions than sides is better
		long distinct = 0;
		for (long c = 0; c < nAng; c++)
		{
			if (clusters[c].fMembers == 0)
				distinct = clusters[c].fPairs + distinct;
			else if (clusters[c].fMirror == -1 && clusters[c].fPerpendicular < c)
				distinct = clusters[c].fPairs + distinct + 1;
		}
		{
			Fixed a = FixedDivide(sides, nLen);
			long r = RoundShort(a + FixedDivide(sides, distinct));
			if (r > 15)
				r = 15;
			quality = (ULong) ((15 - r) * 100);
			if ((uint32_t) quality < 600)
				quality = 600;
		}
		for (long i = 0; i < sides; i++)
		{
			AngCluster* cl = &clusters[map[i].fCluster];
			long j = 0;
			while (((long*) cl)[51 + j * 4] != map[i].fLength)		// fMember[j].fLength, unchecked
				j++;
			if (cl->fMembers <= j)
				goto finish;
			map[i].fMember = j;
		}

		closing = 0;
		nonlinear = 0;
		if (closed)
		{
			if (sides == 3)
				kind = kShapeTriangle;
			if (!allAxes)
			{
				long families = 0;
				for (long c = 0; c < nAng; c++)
				{
					failed = FamilyRotEqs(clusters, c, closed, n, shape, map, &nonlinear, system, &families);
					if (failed)
						goto finish;
					failed = FamilyReflEqs(clusters, c, closed, n, shape, &nonlinear, system);
					if (failed)
						goto finish;
				}
				if (sides == 4)
				{
					if (families == 1)
						kind = kShapeSquare;
					else if (families == 2)
						kind = kShapeRectangle;
				}
			}
			else
			{
				if (clusters[0].fMembers == 1 && clusters[1].fMembers == 1 && sides == 4)
					kind = (clusters[1].fMember[0].fLength == clusters[0].fMember[0].fLength) ? kShapeSquare : kShapeRectangle;
				if (AlignRotEqs(clusters, n, shape, map, system))
					goto finish;
			}

			if (kind == kShapeSquare)
			{
				long a;
				if (allAxes)
					a = 0;
				else
				{
					long d = clusters[map[0].fCluster].fAngle;
					if ((d >= -90 && d < 0) || d >= 90)
						d = clusters[map[1].fCluster].fAngle;
					if (d < 0)
						d += 180;
					a = ShiftLeft(d, 16);
				}
				*angle = a;
			}
			else if (kind == kShapeRectangle)
			{
				long k = (L[1] >= L[0]) ? 1 : 0;
				long d = clusters[map[k].fCluster].fAngle;
				if (allAxes)
					AxisAngle(clusters[map[k].fCluster].fAxis, &d);
				if (d < 0)
					d += 180;
				*angle = ShiftLeft(d, 16);
			}

			if (DirSumEqs(sides, map, clusters, &nonlinear, &closing, system))
				goto finish;
			if (closed && (nonlinear || closing))
			{
				system->fN = n * 2;
				for (long e = 0; e < system->fCount; e++)
					system->fEqs[e].fN = n * 2;
				solvable = true;
				if (kind == kShapeClosed && sides == 4)
					gFourSided = 1;
				goto finish;
			}
		}

		// nothing to solve: each side laid along its cluster's direction
		if (sides == 4 && kind == kShapeClosed)
			kind = kShapeQuadrilateral;
		GeneralPt* prev = (GeneralPt*) shape->GetEntry(0);
		for (long k = 1; k <= sides; k++)
		{
			GeneralPt* pt = (GeneralPt*) shape->GetEntry(k);
			SideMap* sm = &map[k - 1];
			AngCluster* cl = &clusters[sm->fCluster];
			long dx = pt->fPt.x - prev->fPt.x;
			long dy = pt->fPt.y - prev->fPt.y;
			long ox, oy;
			if (cl->fAxis == 1)
			{
				ox = 0;
				oy = (long) (int32_t) ((uint32_t) ShiftLeft(sm->fLength, 16) * (uint32_t) Signum(dy));
			}
			else if (cl->fAxis == 2)
			{
				ox = (long) (int32_t) ((uint32_t) ShiftLeft(sm->fLength, 16) * (uint32_t) Signum(dx));
				oy = 0;
			}
			else
			{
				FPoint v;
				v.x = WAbs(cl->fSlope);
				v.y = 0x10000;
				ScaleToSize(&v, ShiftLeft(sm->fLength, 16));
				ox = (long) (int32_t) ((uint32_t) v.x * (uint32_t) Signum(dx));
				oy = (long) (int32_t) ((uint32_t) v.y * (uint32_t) Signum(dy));
			}
			pt->fPt.x = prev->fPt.x + ox;
			pt->fPt.y = prev->fPt.y + oy;
			prev = pt;
		}
		if ((kind == kShapeClosed || kind == kShapeQuadrilateral || kind == kShapeOpen)
		 && sides == 4 && allAxes)
		{
			long len = map[0].fLength;
			if (len > RoundShort(gap))
			{
				Boolean equal = true;
				for (long k = 1; k < 4; k++)
					if (map[k].fLength != len)
						equal = false;
				*angle = 0;
				kind = equal ? kShapeSquare : kShapeRectangle;
			}
		}
	}

finish:
	if (sidesH != nil)
		DeleteHandle(sidesH);
	if (lengths != nil)
		lengths->Dispose();
	if (angles != nil)
		angles->Dispose();
	if (failed)
	{
		kind = kShapeNothing;
		solvable = false;
		quality = 10000;
		ReleaseEqs(system);
	}
	else if (!solvable)
		ReleaseEqs(system);
	*type = kind;
	*score = quality;
	return solvable;
}


// ROM 0x00225560 PlugNewVals__FP17TGeneralShapeUnitPlP8EqSystem
// The solved edge vectors put back into the shape from its first point
// (rounded to a whole pixel).  After a four-sided shape, values[0] says
// whether the four sides came out level or upright and all one length
// (0x80000000, a square) or not (0x40000000); it is left alone if any side
// is slanted.
void
PlugNewVals(TGeneralShapeUnit* unit, long* values, EqSystem* /*system*/)
{
	TDArray* shape = unit->GetGeneralShape();
	long n = shape->Count();
	GeneralPt* prev = (GeneralPt*) shape->GetEntry(0);
	prev->fPt.x = ShiftLeft((prev->fPt.x + 0x8000) >> 16, 16);
	prev->fPt.y = ShiftLeft((prev->fPt.y + 0x8000) >> 16, 16);
	for (long k = 1; k < n; k++)
	{
		GeneralPt* pt = (GeneralPt*) shape->GetEntry(k);
		pt->fPt.x = values[k * 2 - 1] + prev->fPt.x;
		pt->fPt.y = prev->fPt.y + values[k * 2];
		prev = pt;
	}
	if (gFourSided == 0)
		return;
	Boolean equal = true;
	long first = 0;
	for (long s = 0; s < 4; s++)
	{
		long dx = RoundShort(values[s * 2 + 1]);
		long dy = RoundShort(values[s * 2 + 2]);
		long len;
		if (dx == 0)
			len = WAbs(dy);
		else
		{
			if (dy != 0)
				return;
			len = WAbs(dx);
		}
		if (s == 0)
			first = len;
		else if (len != first)
			equal = false;
	}
	values[0] = equal ? (long) 0x80000000 : 0x40000000;
}
