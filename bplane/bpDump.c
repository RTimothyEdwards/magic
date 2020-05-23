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



/* bpDump.c
 *
 * routines to dump bin system (for debugging)
 *
 */

#include <stdio.h>
#include "utils/utils.h"
#include "database/database.h"
#include "utils/geometry.h"
#include "cif/cif.h"
#include "bplane/bplaneInt.h"

static int bpDumpFlags; /* set by bpDump, used by subroutines */

/*
 * ----------------------------------------------------------------------------
 * bpIndent --
 *
 * tab over n spaces on stderr
 *
 * ----------------------------------------------------------------------------
 */
static void bpIndent(int n)
{
  int i;

  for(i=0;i<n;i++) fprintf(stderr," ");
}

/*
 * ----------------------------------------------------------------------------
 * bpDumpRect --
 *
 * list rects.
 *
 * ----------------------------------------------------------------------------
 */
void bpDumpRect(Rect *r)
{
  if(bpDumpFlags & BPD_INTERNAL_UNITS)
  {
    fprintf(stderr,"%d ",
	    r->r_xbot);
    fprintf(stderr,"%d ",
	    r->r_ybot);
    fprintf(stderr,"%d ",
	    r->r_xtop);
    fprintf(stderr,"%d",
	    r->r_ytop);
  }
  else
  {
    float oscale;

    oscale = CIFGetOutputScale(1000);

    fprintf(stderr,"%f ",
	    oscale * (float)r->r_xbot);
    fprintf(stderr,"%f ",
	    oscale * (float)r->r_ybot);
    fprintf(stderr,"%f ",
	    oscale * (float)r->r_xtop);
    fprintf(stderr,"%f",
	    oscale * (float)r->r_ytop);
  }
}

/*
 * ----------------------------------------------------------------------------
 * bpDumpElements --
 *
 * list rects.
 *
 * ----------------------------------------------------------------------------
 */
void bpDumpElements(Element *list, int indent)
{
  Element *e;

  for(e = list; e; e=e->e_link)
  {
    bpIndent(indent);
    fprintf(stderr,"{element ");

    if(bpDumpFlags & BPD_LABELED)
    {
      LabeledElement *le = (LabeledElement *) e;
      fprintf(stderr,"%s ", le->le_text);
    }

    bpDumpRect(&e->e_rect);
    fprintf(stderr,"}\n");

  }
}

/*
 * ----------------------------------------------------------------------------
 * bpDumpEnums --
 *
 * list active enumerations
 *
 * ----------------------------------------------------------------------------
 */
void bpDumpEnums(BPEnum *bpe, int indent)
{
  for(; bpe; bpe=bpe->bpe_next)
  {
    bpIndent(indent);
    fprintf(stderr,"{enum \"%s\"}",
	    bpe->bpe_id);
  }
}

/*
 * ----------------------------------------------------------------------------
 * bpBinArrayDump --
 *
 * recursively dump hierarchical bin system
 *
 * ----------------------------------------------------------------------------
 */
static void bpBinArrayDump(BinArray *ba, int indent)
{

  int numBins = ba->ba_numBins;
  int dx = ba->ba_dx;
  int dy = ba->ba_dy;
  int dimX = ba->ba_dimX;
  int dimY = numBins/dimX;
  Rect *bbox = &ba->ba_bbox;
  int xi,yi;

  /* open */
  bpIndent(indent);
  fprintf(stderr,"{bin-array ");

  if(bpDumpFlags & BPD_INTERNAL_UNITS)
  {
    fprintf(stderr,"{dx %d} {dy %d} ",
	    dx,dy);
  }
  else
  {
    float oscale;

    oscale = CIFGetOutputScale(1000);

    fprintf(stderr,"{dx %f} ",
	    (float)dx * oscale);

    fprintf(stderr,"{dy %f} ",
	    (float)dy * oscale);
  }
    fprintf(stderr,"{dimX %d} {dimY %d} {  bbox ",
	  dimX,
	  dimY);
  bpDumpRect(bbox);
  fprintf(stderr,"  }\n");

  /* bins */
  for(yi=0; yi<dimY; yi++)
  {
    for(xi=0; xi<dimX; xi++)
    {
      Rect area;
      int i = xi + yi*dimX;

      area.r_xbot = bbox->r_xbot + xi*dx;
      area.r_ybot = bbox->r_ybot + yi*dy;
      area.r_xtop = area.r_xbot + dx;
      area.r_ytop = area.r_ybot + dy;

      /* skip empty bins */
      if(bpBinEmpty(ba,i)) continue;

      /* open bin */
      bpIndent(indent+2);
      fprintf(stderr,"{bin {number %d} {  bbox ",
	    i);
      bpDumpRect(&area);
      fprintf(stderr,"  }\n");

      /* list bin contents */
      if(bpBinType(ba,i) == BT_ARRAY)
      {
	/* dump sub array */
	bpBinArrayDump( bpSubArray(ba,i),
			indent+4);
      }
      else
      {
        /* list elements */
	bpDumpElements(bpBinList(ba,i),indent+4);
      }

      /* close bin */
      bpIndent(indent+2);
      fprintf(stderr,"}\n");
    }
  }

  /* oversized */
  if(!bpBinEmpty(ba,numBins))
  {
    /* open oversized */
    bpIndent(indent+2);
    fprintf(stderr,"{oversized {bbox ");
    bpDumpRect(bbox);
    fprintf(stderr,"}\n");

    /* list bin contents */
    if(bpBinType(ba,numBins) == BT_ARRAY)
    {
      /* dump sub array */
      bpBinArrayDump( bpSubArray(ba,numBins),
		      indent+4);
    }
    else
    {
      /* list elements */
      bpDumpElements(bpBinList(ba,numBins),indent+4);
    }

    /* close oversized */
    bpIndent(indent+2);
    fprintf(stderr,"}\n");
  }

  /* close bin array */
  bpIndent(indent);
  fprintf(stderr,"}\n");
}

/*
 * ----------------------------------------------------------------------------
 * bpDump --
 *
 * dump bplane (for debugging)
 *
 * ----------------------------------------------------------------------------
 */
void bpDump(BPlane *bp, int flags)
{
  fprintf(stderr, "======= BPLANE DUMP ======\n");

  bpDumpFlags = flags;

  /* open bplane */
  fprintf(stderr,"{bplane {count %d} {bbox ",
	  bp->bp_count);
  bpDumpRect(&bp->bp_bbox);
  fprintf(stderr,"}\n");

  /* list in box rects */
  bpIndent(2);
  fprintf(stderr,"{in_box\n");

  bpDumpElements(bp->bp_inBox,4);
  bpIndent(2);
  fprintf(stderr,"}\n");

  /*** bins ***/
  bpIndent(2);
  fprintf(stderr,"{binned {area ");
  bpDumpRect(&bp->bp_binArea);
  fprintf(stderr,"}\n");

  if(bp->bp_rootNode) bpBinArrayDump(bp->bp_rootNode, 4);
  bpIndent(2);
  fprintf(stderr,"}\n");

  /*** enums ***/
  bpIndent(2);
  fprintf(stderr,"{enums\n");

  bpDumpEnums(bp->bp_enums,4);

  bpIndent(2);
  fprintf(stderr,"}\n");

  /* close bplane */
  fprintf(stderr,"}\n");
}










