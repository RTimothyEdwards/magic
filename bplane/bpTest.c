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



/* bpTest.c
 *
 * (regression) tests of bplane code
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "message.h"
#include "database.h"
#include "geometry.h"
#include "bplane.h"
#include "bplaneInt.h"
#include "debug.h"
#include "cifInt.h"


/* 
 * elements used by test code.
 */
typedef struct rectc
{
  struct rectc *rc_links[BP_NUM_LINKS];
  Rect rc_rect;
} RectC;


/*
 * ----------------------------------------------------------------------------
 * bpRand -- 
 *    generate a random int in given range.
 *
 *    side effects:  sets coords of input rect.
 * ----------------------------------------------------------------------------
 */		 
int bpRand(int min, int max)
{
  double f = rand()/ (double) RAND_MAX;     /* random number in unit interval */
  return min + (int) ((max-min+1)*f);
}
  
/*
 * ----------------------------------------------------------------------------
 * bpTestRandRect -- 
 *    generate a random unit rectangle inside bbox
 *
 *    side effects:  sets coords of input rect.
 * ----------------------------------------------------------------------------
 */		 
static void bpTestRandRect(Rect *r, Rect *bbox)
{
  r->r_xbot = bpRand(bbox->r_xbot,bbox->r_xtop-1);
  r->r_ybot = bpRand(bbox->r_ybot,bbox->r_ytop-1);
  r->r_xtop = r->r_xbot+1;
  r->r_ytop = r->r_ybot+1;
}

/* ====== GOLD snow test.
 * ====== linked list implementation (to determine 'right' answer)
 */

/*
 * ----------------------------------------------------------------------------
 * bpTestIntersectGold -- 
 *    
 *    check whether rc intersects list
 * ----------------------------------------------------------------------------
 */		 
static bool bpTestIntersectGold(RectC *rc, RectC *list)
{
  while(list)
  {
    if(GEO_TOUCH(&rc->rc_rect,&list->rc_rect)) return TRUE;
    list=list->rc_links[0];
  }
  return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 * bpTestSnowGold -- 
 *    populate square of dimension 'size' with non-overlapping random unit 
 *    rectangles.  Keep adding rectangles until failures exceed successes.
 *    (stop when maxFailures reached.)
 *
 *    coded with simple linked list.
 *
 * ---------------------------------------------------------------------------- */		 
void bpTestSnowGold(int size, bool trace)
{
  int failures = 0;
  int successes = 0;
  int i = 0;
  int crc = 0;      
  RectC *result = NULL;
  RectC *rc = NULL;
  Rect area;

  fprintf(stderr,"BEGIN Snow GOLD, size=%d\n", size);

  /* set up area */
  area.r_xbot = 0;
  area.r_ybot = 0;
  area.r_xtop = size;
  area.r_ytop = size;

  while(failures<=successes)
  {
    i++;

    if(!rc) rc = MALLOC_TAG(RectC *,rc, sizeof(RectC), "RectC");
    bpTestRandRect(&rc->rc_rect, &area);

    if(!bpTestIntersectGold(rc,result))
    {
      if(trace) DumpRect("success ",&rc->rc_rect); 
      crc ^= i+ 3*rc->rc_rect.r_xbot+ 5*rc->rc_rect.r_ybot;
      
      rc->rc_links[0] = result;
      result = rc;
      rc = NULL;

      successes++;
    }
    else
    {
      if(trace) DumpRect("failure ",&rc->rc_rect); 

      failures++;
    }
  }

  /* clean up */
  while(result)
  {
    /* pop */
    rc=result;
    result=rc->rc_links[0];

    /* free */
    FREE(rc);
  }

  fprintf(stderr,"END Snow GOLD, size=%d failures=%d successes=%d crc=%d\n",
	  size,
	  failures,
	  successes,
	  crc);
}

/* ====== bplane snow test.
 */ 

/*
 * ----------------------------------------------------------------------------
 * bpTestIntersect -- 
 *    
 *    check whether rc intersects list
 * ----------------------------------------------------------------------------
 */		 
static bool bpTestIntersect(RectC *rc, BPlane *bp)
{
  BPEnum bpe;
  int result;
  
  BPEnumInit(&bpe,bp, &rc->rc_rect, BPE_TOUCH,"bpTestIntersect");
  result = (BPEnumNext(&bpe)!=NULL);
  BPEnumTerm(&bpe);

  return result;
}

/*
 * ----------------------------------------------------------------------------
 * bpTestSnow -- 
 *    populate area with non-overlapping random unit rectangles. 
 *
 *    using bplane.
 *
 * ----------------------------------------------------------------------------
 */		 
BPlane *bpTestSnow(int size, bool trace)
{
  int failures = 0;
  int successes = 0;
  int i = 0;
  int crc = 0;      
  RectC *result = NULL;
  RectC *rc = NULL;
  BPlane *bp = BPNew();
  Rect area;

  fprintf(stderr,"BEGIN Snow, size=%d\n", size);

  /* set up area */
  area.r_xbot = 0;
  area.r_ybot = 0;
  area.r_xtop = size;
  area.r_ytop = size;
  
  while(failures<=successes)
  {
    i++;

    if(!rc) rc = MALLOC_TAG(RectC *,rc, sizeof(RectC), "RectC");
    bpTestRandRect(&rc->rc_rect, &area);

    if(!bpTestIntersect(rc,bp))
    {
      if(trace) DumpRect("success ",&rc->rc_rect); 
      crc ^= i+ 3*rc->rc_rect.r_xbot+ 5*rc->rc_rect.r_ybot;
      
      BPAdd(bp,rc);
      rc = NULL;

      successes++;
    }
    else
    {
      if(trace) DumpRect("failure ",&rc->rc_rect); 
      failures++;
    }
  }

  fprintf(stderr,"END Snow, size=%d failures=%d success=%d crc=%d\n",
	  size,
	  failures,
	  successes,
	  crc);
	  
  return bp;
}

/* ====== Tile Plane based snow test.
 */

/*
 * ----------------------------------------------------------------------------
 * bpTestIntersectTile -- 
 *    
 *    check whether r intersects existing tiles
 *
 * ----------------------------------------------------------------------------
 */		 

int bpTestIntersectTileFunc(Tile *tile, ClientData cd) 
{
  return 1;
}

static bool bpTestIntersectTile(Rect *r, Plane *plane)
{
  Rect area;

  /* catch touching tiles */
  area = *r;
  area.r_xbot -= 1;
  area.r_ybot -= 1;

  area.r_xtop += 1;
  area.r_ytop += 1;

  return DBPlaneEnumAreaPaint((Tile *) NULL, 
			      plane,
			      &area,
			      &DBAllButSpaceBits,
			      bpTestIntersectTileFunc,
			      NULL);
}

/*
 * ----------------------------------------------------------------------------
 * bpTestSnowTile -- 
 *    populate area with non-overlapping random unit rectangles. 
 *    
 *    using tile plane
 *
 * ----------------------------------------------------------------------------
 */		 
Plane *bpTestSnowTile(int size, bool trace)
{
  int failures = 0;
  int successes = 0;
  int i = 0;
  int crc = 0;      
  RectC *result = NULL;
  RectC *rc = NULL;
  Plane *plane = DBPlaneNew((ClientData) TT_SPACE);
  Rect area;

  fprintf(stderr,"BEGIN Snow Tile, size=%d\n", size);

  /* set up area */
  area.r_xbot = 0;
  area.r_ybot = 0;
  area.r_xtop = size;
  area.r_ytop = size;
  
  while(failures<=successes)
  {
    Rect r;

    i++;
    bpTestRandRect(&r, &area);

    if(!bpTestIntersectTile(&r,plane))
    {
      if(trace) DumpRect("success ",&r); 
      crc ^= i+ 3*r.r_xbot + 5*r.r_ybot;
      
      DBPaintPlane(plane, &r, CIFPaintTable, (PaintUndoInfo *) NULL);
      rc = NULL;

      successes++;
    }
    else
    {
      if(trace) DumpRect("failure ",&r); 
      failures++;
    }
  }

  fprintf(stderr,"END Snow Tile, size=%d failures=%d success=%d crc=%d\n",
	  size,
	  failures,
	  successes,
	  crc);

  return plane;
}

