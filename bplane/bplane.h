// ************************************************************************
//
// Copyright (c) 1995-2002 Juniper Networks, Inc. All rights reserved.
//
// Permission is hereby granted, without written agreement and without
// license or royalty fees, to use, copy, modify, and distribute this
// software and its documentation for any purpose, provided that the
// above copyright notice and the following three paragraphs appear in
// all copies of this software.
//
// IN NO EVENT SHALL JUNIPER NETWORKS, INC. BE LIABLE TO ANY PARTY FOR
// DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
// JUNIPER NETWORKS, INC. HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// JUNIPER NETWORKS, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
// NON-INFRINGEMENT.
//
// THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND JUNIPER
// NETWORKS, INC. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
// UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
//
// ************************************************************************



#ifndef _MAGIC__BPLANE__BPLANE_H
#define _MAGIC__BPLANE__BPLANE_H

/*
 * bplane.h --
 *
 * This file defines the interface between the bplane
 * module and the rest of max.
 *
 * BUGS
 * ====
 *
 * NOTE nested enums are currently broken do to dynamic binning.
 *
 * OVERVIEW OF BPLANES
 * ===================
 *
 * BPlanes ('Binned' Planes) are a data-structure for storing, sorting,
 * and accessing two dimensional geometric objects.
 *
 * BPlanes are an alternative to Planes, i.e. corner-stitched tile planes,
 * defined in the tile module.
 *
 * Differences between Planes and BPlanes:
 * --------------------------------------
 *
 *   1.  BPlanes are more memory efficient.
 *       Three pointers (+ a modest amount of binning overhead)
 *       replaces approximately 8 pointers (4/tile, approx. 1 space
 *       tile for each data tile).
 *
 *   2.  BPlanes use a 'next' procedure for enumeration, instead of
 *       function call-backs.  This allows client code to be simpler
 *       and more readable, partcularly with regard to passing state info.
 *       In addition gprof results are easier to interpet, since
 *       there is not confusion about which client procedures belong to
 *       a given "loop".
 *
 *   3.  Planes fundamentally assume objects don't overlap, while BPlanes
 *       make no such assumption.  This makes BPlanes more generally useful.
 *       In particular they are a natural choice for instance uses,
 *       labels, and non-manhattan polygons (sorted on bounding boxes).
 *
 *   4.  Planes are optimized for merging (merging recangles
 *       into maximal horizontal strips) and neighbor access
 *       (finding nearest elements to current element).  BPlanes are less
 *       efficient for these operations.
 *
 *   5.  Planes generally cannot be safely modified during an enumeration,
 *       but BPlanes can.  This makes operations such as delete, copy, and
 *       move simpler in BPlanes.
 *
 * Interface
 * ---------
 *
 *   1. The structure of elements to be stored in BPlanes must be as
 *      follows:
 *
 *       typedef struct foo
 *       {
 *         struct void *foo_bpLinks[BP_NUM_LINKS];
 *         Rect foo_rect;
 *         <client fields go here>
 *       } Foo
 *
 *   2. It is the clients responsiblity to alloc/free elements.
 *
 *   3. The client must set foo_rect before adding the element
 *      to a BPlane.  foo_rect must be canonical: not inverted.
 *
 *   4. The BPlane module does not access or modify any client fields.
 *
 *   5. The client may access/modify client fields at any time.
 *
 *   6. As long as an element belongs to a BPlane (i.e. has been
 *   added via BPAdd() and not removed via BPDelete()):
 *
 *      a.  The client should not reference/modify the foo_bpLinks[] fields.
 *      b.  The client may reference but should not modify the foo_rect
 *          field.
 *      c.  The client should not call BPAdd() for the element
 *          (an element can only belong to one BPlane at at time).
 *      d.  The client should not free the element!
 *
 *   7.  The client may assume tht BP_NUM_LINKS is at least one, and
 *       may use foo_bpLinks[0], for his own purposes as long as an
 *       element does not belong to a bplane.
 *
 *   8.  Concurrent (nested) enumerations of a bplane are permitted.
 *
 *   9.  Elements may not be added to a bplane during active enumeration(s)
 *       on that bplane.
 *
 *   10.  An element may be deleted from a bplane at any time, including
 *        during active enumeration(s) of that bplane.  After an element
 *        has been deleted from a bplane, it will not be enumerated
 *        (i.e. returned by BPEnumNext() on that bplane).
 *
 * Example
 * -------
 *
 * Here is a procedure that takes an array of id'ed rectangles and an
 * area as input, and prints the ids of all rectangles impinging on the
 * area.
 *
 *       typedef struct rid
 *       {
 *         struct rid *rid_bpLinks[BP_NUM_LINKS];
 *         Rect rid_rect;
 *         char *rid_id;
 *       } RId;
 *
 *       void findRects(RId data[],    // ided rects
 *                      int n,         // number of rects in data
 *                      Rect *area)    // area to search
 *       {
 *         int i;
 *         BPEnum bpe;
 *         BPlane *bp;
 *         RId *rid;
 *
 *         bp = BPNew();
 *         for(i=0;i<n;i++) BPAdd(bp,&data[i]);
 *
 *         BPEnumInit(&bpe,bp,area,BPE_OVERLAP,"findRects");
 *         while(rid = BPEnumNext(&bpe))
 *         {
 *            printf("%s\n", rid->rid_id);
 *         }
 *         BPEnumTerm(&bpe);
 *
 */

/* data-structures opaque to clients */
#include "bplane/bpOpaque.h"

/* create a new BPlane */
extern BPlane *BPNew(void);

/* free storate assoicated with a BPlane
 * (The BPlane must be empty.)
 */
extern void BPFree(BPlane *bp);

/* add an element to a BPlane */
extern void BPAdd(BPlane *bp,
		  void *element);

/* remove an element from a BPlane */
extern void BPDelete(BPlane *bp,
		     void *element);

/* begin an enumeration */
extern void BPEnumInit(BPEnum *bpe, /* this procedure initializes this
				     * client supplied 'handle' for the
				     * enumeration.
				     */
		       BPlane *bp, /* bplane to search */
		       const Rect *area, /* area to search */
		       int match,  /* see below */
		       const char *id);  /* for debugging */
/* match values */

  /* enum all elements in the bplane (area arg must be null) */
#define BPE_ALL 0
  /* element need only touch area */
#define BPE_TOUCH 1
  /* some part of element must be inside (not just on boundary of) area */
#define BPE_OVERLAP 2
  /* elements rect must be identical to area */
#define BPE_EQUAL 3

/* return next element in enumeration (returns NULL if none) */
#include "bplane/bpEnum.h"
/* inlined extern void *BPEnumNext(BPEnum *bpe); */

/* terminate enumeration
 *
 * (unterminated enumerations can cause great inefficiency since
 *  all active enumerations for a bplane must be considered whenever
 *  an element is added or deleted.)
 */
extern void BPEnumTerm(BPEnum *bpe);

/* get current bounding box of BPlane */
extern Rect BPBBox(BPlane *bp);

/* compute number of bytes used by BPlane
 * (does not count memory of the elements themselves)
 */
extern unsigned int BPStatMemory(BPlane *bp);

/* tabulate statistics on a bplane */
extern unsigned int
BPStat(BPlane *bp,
       int *totCount,        /* ret total number of elements */
       int *inBox,           /* ret num of elements in inBox */
       int *totBins,         /* ret tot num of bins */
       int *emptyBins,       /* ret num of empty bins */
       int *binArraysp,      /* ret num of bin arrays */
       int *maxEffp,         /* ret max effective list length */
       int *maxBinCount,     /* ret max count for regular bin */
       int *totUnbinned,     /* ret tot num of e's not binned */
       int *maxDepth);       /* ret max bin array depth */

#endif /* _MAGIC__BPLANE__BPLANE_H */
