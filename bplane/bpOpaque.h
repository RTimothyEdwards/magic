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



#ifndef _MAGIC__BPLANE__BPOPAQUE_H
#define _MAGIC__BPLANE__BPOPAQUE_H

#ifndef _MAGIC__UTILS__IHASH_H
#include "utils/ihash.h"
#endif

/*
 * bpOpaque.h --
 *
 * This file contains strucs directly or indirectly referenced by
 * clients of bplane module, whose internals should be treated
 * as opaque by clients.
 *
 * It is included by bplane.h
 *
 */

/* data element, stored in BPlane
 *
 * Storage managed by caller.
 * Inital part must correspond to below.
 */
typedef struct element
{
  struct element *e_hashLink;
  struct element *e_link;
  struct element **e_linkp; /* back pointer for quick deletes */
  Rect e_rect;
  /* client data goes here */
} Element;

/* number of link fields in element
 *
 * user code should not depend on more than 1 link.
 * (and should only use/ref that link when element is not in a bplane)
 */
#define BP_NUM_LINKS 3

/* bin array */
typedef struct binarray
{
  Rect ba_bbox;       /* area covered by array */
  int  ba_dx;         /* dimensions of a single bin */
  int  ba_dy;
  int  ba_dimX;       /* number of bins in a row */
  int  ba_numBins;    /* number of regular bins (size of array - 1) */

  void *ba_bins[1];   /* low order bit(s) used to encode type info.
		       * DON'T ACCESS DIRECTLY, USE MACROS BELOW
		       *
		       * (last bin is for oversized)
		       */
} BinArray;

/* bin types
 *
 * NOTE: its important that simple lists have type 0, i.e. are
 *        just standard pointers.  This is so that the list head
 *        'link' can be treated just as any other link, during
 *        deletion etc.
 */

#define BT_TYPE_MASK 1
#define BT_LIST 0
#define BT_ARRAY 1

static __inline__ bool bpBinEmpty(BinArray *ba, int i)
{
  return ba->ba_bins[i] == NULL;
}

static __inline__ bool bpBinType(BinArray *ba, int i)
{
  return (bool) (((pointertype) ba->ba_bins[i]) & BT_TYPE_MASK);
}

static __inline__ Element *bpBinList(BinArray *ba, int i)
{
#ifdef BPARANOID
  ASSERT(bpBinType(ba,i)==BT_LIST,"bpBinList");
#endif
  return (Element *) ba->ba_bins[i];
}

static __inline__ Element **bpBinListHead(BinArray *ba, int i)
{
#ifdef BPARANOID
  ASSERT(bpBinType(ba,i)==BT_LIST,"bpBinList");
#endif
  return (Element **) &ba->ba_bins[i];
}

static __inline__ BinArray *bpSubArray(BinArray *ba, int i)
{
#ifdef BPARANOID
  ASSERT(bpBinType(ba,i)==BT_ARRAY,"bpSubArray");
#endif
  return (BinArray *) ((pointertype) ba->ba_bins[i] & ~BT_TYPE_MASK);
}

/* BPlane - toplevel struc */
typedef struct bplane
{
  Rect bp_bbox;             /* bbox (bin + in) */
  bool bp_bbox_exact;       /* if set bp_bbox, is exact,
			     * if reset bp_bbox may be over-sized.
			     */
  int bp_count;             /* total number of elements in bplane */
  struct bpenum *bp_enums;  /* list of active enums */

  /* HASH TABLE */
  IHashTable *bp_hashTable; /* hash table
			     * (for expediting BP_EQUAL searches) */
  /* IN BOX */
  Element *bp_inBox;        /* elements not yet added to bin system */

  /* BINS */
  int bp_binLife;          /* set to binCount when bin system
			    * built, decremented on add/delete ops.
			    * bin system rebuilt when zero reached.
			    */
  int bp_inAdds;           /* additions to inBox since last rebuild */

  Rect bp_binArea;         /* area covered by bin arrays */
  BinArray *bp_rootNode;    /* top bin node */
} BPlane;

/* context for BPlane enumeration */
typedef struct bpStack
{
  int        bps_state;     /* where we are at rolled in one convenient
			     * number (see BPS_* defs in bpEnum.h)
			     */
  BinArray   *bps_node;     /* current bin array */
  int        bps_i;         /* current index */
  int        bps_rowMax;    /* max index for this row */
  int        bps_rowDelta;  /* increment from end of one row to beginning
			     * of next.
			     */
  int        bps_max;       /* max index */
  int        bps_dimX;      /* row length */
  bool       bps_subbin;    /* if set consider subbinning */
  int        bps_rejects;   /* number of unmatching elements in current
			     * bin, used to decide when to subbin.
			     */
} BPStack;

/* enumeration 'handle' */
typedef struct bpenum
{
  struct bpenum *bpe_next;     /* all enums for bplane linked together */
  BPlane *bpe_plane;           /* plane being searched */
  Rect    bpe_srchArea;        /* area being searched */
  int     bpe_match;           /* match criteria */
  const char *bpe_id;          /* for debug */
  int     bpe_subBinMinX;
  int     bpe_subBinMinY;      /* consider subbinning
				* for bins bigger than this.
				*/
  Element *bpe_nextElement;    /* next element in current list */
  BPStack *bpe_top;            /* top of stack */
  BPStack bpe_stack[10000];    /* stack for tree traversal during enum */
} BPEnum;

#endif /* _MAGIC__BPLANE__BPOPAQUE_H */
