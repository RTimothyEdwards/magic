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



/*
 * bplaneInt.h --
 *
 * This file defines constants and datastructures used internally by the
 * bplane module, but not exported to the rest of the world.
 */
#ifndef _MAGIC__BPLANE__BPLANEINT_H
#define _MAGIC__BPLANE__BPLANEINT_H

/* Tcl linked Parameters */
extern int bpMinBAPop;         /* don't sub(bin) when count less than this
				*/
extern double bpMinAvgBinPop;  /* try to keep average bin pop at or
				* below this
				*/

/* LabeledElement
 *
 * Used in this module as elements for test bplanes.
 */
typedef struct labeledelement
{
  struct element *le_links[BP_NUM_LINKS];
  Rect le_rect;
  /* client data goes here */
  char * le_text;
} LabeledElement;

/* bins */
extern void bpBinsUpdate(BPlane *bp);

extern void bpBinAdd(BinArray *ba, Element *e);

extern BinArray *bpBinArrayBuild(Rect bbox,
				 Element *elements, /* initial elements */
				 bool subbin); /* subbin as needed */

/* dump (for debug) */
extern void bpDumpRect(Rect *r);
extern void bpDump(BPlane *bp, int flags);
/* bpDump flags */
/* labeled elements */
# define BPD_LABELED 1
# define BPD_INTERNAL_UNITS 2

/* test */
void bpTestSnowGold(int size, bool trace);
extern BPlane *bpTestSnow(int size, bool trace);
extern Plane *bpTestSnowTile(int size, bool trace);

extern int bpRand(int min, int max);

#endif /* _MAGIC__BPLANE__BPLANEINT_H */
