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




/* bpBins.c
 *
 * Routines for creating and manipulating bin arrays.
 *
 */

#include <stdio.h>
#include <math.h>
#include "utils/utils.h"
#include "utils/malloc.h"
#include "database/database.h"
#include "utils/geometry.h"
#include "bplane/bplaneInt.h"

/* debug */
#define BPD 0

/* Tcl linked Parameters */
int bpMinBAPop = 10;  /* don't sub(bin) when count less than this */
double bpMinAvgBinPop = 1.0;  /* try to keep average bin pop at or
			       * below this
			       */

/*
 * ----------------------------------------------------------------------------
 *
 * roundUp -- Round up a number to a grid.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ int roundUp(int i, int res)
{
    int r = (i % res);

    /* Subtract negative number */
    if (r > 0) r = r - res;
    return i - r;
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinArrayNew -- allocate new bin array.
 *
 * ----------------------------------------------------------------------------
 */
static BinArray *bpBinArrayNew(int dx,      /* x diameter of bins */
			       int dy,      /* y diameter of bins */
			       Rect *bbox)  /* area covered */

{
  BinArray *new;
  Rect abbox;
  int w, h, dimX, dimY, numBins;
  int size;

  /* compute array dimensions */
  w = roundUp(GEO_WIDTH(bbox),dx);
  h = roundUp(GEO_HEIGHT(bbox),dy);
  dimX = w/dx;
  dimY = h/dy;
  numBins = dimX*dimY;

  /* allocate array */
  size = sizeof(BinArray) + numBins*(sizeof(void *));
  new = (BinArray *)callocMagic(1, size);

  /* initial */
  new->ba_bbox = *bbox;
  new->ba_dx = dx;
  new->ba_dy = dy;
  new->ba_dimX = dimX;
  new->ba_numBins = numBins;

  /* pull bbox back one from top-edge, right-edge, to simplify index
   * computation in bpEnumPush
   */
  new->ba_bbox.r_xtop --;
  new->ba_bbox.r_ytop --;



  return new;
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinAdd -- add element to bin array
 *
 * ----------------------------------------------------------------------------
 */
void bpBinAdd(BinArray *ba,
	      Element *e)
{
  int i; /* bin index */

  /* compute bin index */
  if(GEO_WIDTH(&e->e_rect) >= ba->ba_dx ||
     GEO_HEIGHT(&e->e_rect) >= ba->ba_dy)
  {
    /* oversized */
    i = ba->ba_numBins;
  }
  else
  {
    /* x and y indices */
    int xi = (e->e_rect.r_xbot - ba->ba_bbox.r_xbot) / ba->ba_dx;
    int yi = (e->e_rect.r_ybot - ba->ba_bbox.r_ybot) / ba->ba_dy;

    i = xi + yi*ba->ba_dimX ;
  }

  /* add element */
  if(bpBinType(ba,i) == BT_ARRAY)
  {
    /* sub-binned */
    bpBinAdd(bpSubArray(ba,i), e);
  }
  else
  {
    /* simple list */
    Element *next = bpBinList(ba,i);

    /* link with next */
    e->e_link = next;
    if(next) next->e_linkp = &e->e_link;

    /* link to head */
    ba->ba_bins[i] = e;
    e->e_linkp =  (Element **) &ba->ba_bins[i];
  }
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinArrayUnbuild - remove elements from bin array and Free the array
 *
 * Returns: (singly linked) list of elements formerly in the array
 *
 * ----------------------------------------------------------------------------
 */
static Element *bpBinArrayUnbuild(BinArray *ba)
{
  Element *elements = NULL;
  int numBins = ba->ba_numBins;
  int i;

  /* empty the bins */
  for(i=0;i<=numBins;i++)
  {
    Element *l;

    if(bpBinType(ba,i) == BT_ARRAY)
    {
      /* sub-array, unbuild recursively */
      l = bpBinArrayUnbuild(bpSubArray(ba,i));
    }
    else
    {
      /* Simple list */
      l = bpBinList(ba,i);
    }

    /* collect elements */
    while(l)
    {
      Element *e = l;
      l = e->e_link;

      e->e_link = elements;
      elements = e;
    }
  }

  /* free the array */
  freeMagic((char *)ba);

  return elements;
}



/*
 * ----------------------------------------------------------------------------
 * bpListExceedsQ --
 *
 * check if element list exceeds given length
 *
 * Returns size of list.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ int
bpListExceedsQ(Element *e,   /* list */
	       int n)        /* length to check against */
{
  n++;

  while(e && n)
  {
    n--;
    e = e->e_link;
  }

  return n==0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinArraySizeIt -- choose bin sizes for new bin array.
 *
 * RESULT:
 *
 * normally returns TRUE,
 * returns FALSE on failure: could not come up with binning that
 * makes progress.
 *
 * NOTE: the various 'return' parameters are not set on failure.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ bool
bpBinArraySizeIt(Rect *bbox,            /* bin array bbox */
		 Element *elements,     /* initial elements */
		 int *dxp,              /* return bin x-diameter here */
		 int *dyp,              /* return bin y-diameter here */
		 int *maxDXp,
		 int *maxDYp,
		 int *numBinsp,         /* return number of bins here */
		 int *countp)           /* return number of elements here */
{
  BinArray *ba;
  int count;
  double numBins;  /* need double to avoid overflow on tentative calc. */
  int h = GEO_HEIGHT(bbox);
  int w = GEO_WIDTH(bbox);

  int dx,dy;                       /* individual bin diameter */
  int maxEX, maxEY;                /* max element dimensions */
  int maxDX, maxDY;                /* max bin diameter allowed */
  int xDim, yDim;                  /* array dimensions */

  int maxBins;                     /* max number of bins
				    * (due to bpMinAvgBinPop)
				    */

  /* compute max element dimensions
   * (would like bins coarser than max dimensisons)
   */
  {
    Element *e;

    maxEX = 0;
    maxEY = 0;
    count = 0;

    for(e=elements; e; e=e->e_link)
    {
      int ew = GEO_WIDTH(&e->e_rect);
      int eh = GEO_HEIGHT(&e->e_rect);

      maxEX = MAX(maxEX,ew);
      maxEY = MAX(maxEY,eh);

      count++;
    }
  }

  /* if too few elements, don't bother with binning */
  if(count < bpMinBAPop) return FALSE;

  /* if too tiny don't subbin,
   * avoid nasty corner-cases in code below
   */
  if(h<2 || w<2) return FALSE;

  /* tentatively choose bin size to fit all elements */
  dx = maxEX+1;
  dy = maxEY+1;

  /* ensure we get at least two bins, so that
   * subbining of sparse designs will work.
   */
  maxDX = (w+1)/2;
  maxDY = (h+1)/2;

  /*
  fprintf(stderr,"bpBinArraySizeIt, initial "
	  "maxEX=%d maxEY=%d  maxDX=%d maxDY=%d  dx=%d dy=%d\n",
	  maxEX,maxEY,maxDX,maxDY,dx,dy);
  */

  if(dx <= maxDX)
  {
    /* x is cool */

    if(dy <= maxDY)
    {
      /* totally cool */
      ;
    }
    else
    {
      /* y-dim too big for two bins, but x-dim cool,
       * just reduce in x this time.
       */
      dy = h+1;
    }
  }
  else
  {
    /* x-dim too big for two bins */

    if(dy <= maxDY)
    {
      /* x-dim too big for two bins but y=dim cool,
       * just reduce in y this time
       */
      dx = w+1;
    }
    else
    {
      /* BOTH x-dim and y-dim too big for two bins.
       * We are screwed:  will have some oversized.
       *
       * Choose betwen splitting in two horizontally or
       * vertically, by which ever method results in the minimum
       * number of oversized elements.
       */
      int xOver=0;  /* number of oversized if we reduce x-dim. */
      int yOver=0;  /* number of oversized if we reduce y-dim. */
      Element *e;

      /* count potential oversized */
      for(e=elements; e; e=e->e_link)
      {
	int ew = GEO_WIDTH(&e->e_rect);
	int eh = GEO_HEIGHT(&e->e_rect);

	if(ew >= maxDX) xOver++;
	if(eh >= maxDY) yOver++;
      }

      if(xOver<yOver)
      {
	/* reduce x-dim to minimize oversized */
	dx = maxDX;
	dy = h+1;
      }
      else
      {
	/* are we making progress? */
	if(yOver == count) return FALSE;

	/* reduce y-dim to minimize oversized */
	dx = w+1;
	dy = maxDY;
      }
    }
  }

  /* tentative number of bins */
  xDim = roundUp(w,dx)/dx;
  yDim = roundUp(h,dy)/dy;
  numBins = xDim*((double)yDim);


  /* if too many bins, need to increase at least one dimension */
  /* (note this step will NOT reduce dimensions) */
  maxBins = MAX(count / bpMinAvgBinPop,1);
  /*
  fprintf(stderr,"DEBUG numBins = %g count= %d bpMinAvgBinPop=%f maxBins= %d\n",
	  numBins,count,bpMinAvgBinPop,maxBins);
  */
  if(numBins>maxBins)
  {
    if(dx == w+1)
    {
      /* can't increase x-dim, so try increasing y-dim  */
      int yDimTarget = maxBins/xDim;

      dy = (h+1) / MAX(yDimTarget,1);
      dy = MIN(dy,maxDY);
    }
    else if (dy == h+1)
    {
      /* can't increase y-dim, so try increasing x-dim  */
      int xDimTarget = maxBins/yDim;

      dx = (w+1) / MAX(xDimTarget,1);
      dx = MIN(dx,maxDX);
    }
    else
    {
      /* try for square bins */
      double area = h * (w + 0.0);
      int d = MAX(sqrt(area/maxBins),1);

      if(d<dx)
      {
	/* target d too small in x-dim
	 * leave xdim fixed and just increase y-dim
	 */
	int yDimTarget = maxBins/xDim;

	dy = (h+1) / MAX(yDimTarget,1);
	dy = MIN(dy,maxDY);
      }
      else if (d<dy)
      {
	/* target d too small in y-dim
	 * leave xdim fixed and just increase y-dim
	 */
	int xDimTarget = maxBins/yDim;

	dx = (w+1) / MAX(xDimTarget,1);
	dx = MIN(dx,maxDX);
      }
      else if(d>maxDX)
      {
	/* d too big for x-dim
	 * (this can happen for tall skinny bins)
	 *
	 * make x-dim maximal, and adjust y accordingly
	 */
	dx = w+1;
	dy = MAX((h+1)/maxBins,dy);
	dy = MIN(dy,maxDY);
      }
      else if(d>maxDY)
      {
	/* d too big for y-dim
	 * (this can happen for long squat bins)
	 *
	 * make y-dim maximal, and adjust x-dim accordingly
	 */
	dy = h+1;
	dx = MAX((w+1)/maxBins,dx);
	dx = MIN(dx,maxDX);
      }
      else
      {
	/* we're cool, create square bins */
	dx = d;
	dy = d;
      }
    }

    /* update numBins */
    xDim = roundUp(w,dx)/dx;
    yDim = roundUp(h,dy)/dy;
    numBins = xDim*yDim;

  }

  /* DEBUG */
  if(BPD)
  {
    fprintf(stderr,"\nDEBUG bpBinArraySizeIt DONE, count=%d h=%d w=%d\n"
	    "\tmaxDX=%d maxDY=%d maxBins=%d\n"
	    "\tnumBins=%g dx=%d dy=%d\n",
	    count,h,w,
	    maxDX,maxDY,maxBins,
	    numBins,dx,dy);
  }

  /* set results */
  if(dxp) *dxp = dx;
  if(dyp) *dyp = dy;
  if(maxDXp) *maxDXp = maxDX;
  if(maxDYp) *maxDYp = maxDY;
  if(numBinsp) *numBinsp = numBins;
  if(countp) *countp = count;

  /* success */
  return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinArrayBuild1 -- build and populate bin array of given area and
 *                    bin size.
 *
 * Returns: pointer to new bin array.
 *
 * ----------------------------------------------------------------------------
 */
static BinArray *bpBinArrayBuild1(Rect *bbox,
				  Element *elements, /* initial elements */
				  int dx,   /* bin diameter */
				  int dy)

{
  BinArray *ba;

  /* build bin array */
  ba = bpBinArrayNew(dx, dy, bbox);

  /* transfer elements to bin array */
  while(elements)
  {
    Element *e;

    /* pop list */
    e = elements;
    elements = e->e_link;

    bpBinAdd(ba, e);
  }

  return ba;
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinArrayBuild -- build and populate bin array of given area,
 *
 * NOTE:  optimal bin size determined by trial and error.
 *        oversized subbinned, as indicated.
 *
 * Returns: pointer to new bin array, NULL on failure.
 *
 * ----------------------------------------------------------------------------
 */
BinArray *bpBinArrayBuild(Rect bbox,
			  Element *elements, /* initial elements */
			  bool subbin) /* subbin as needed */
{
  BinArray *ba;
  int dx,dy;      /* individual bin diameter */
  int maxDX, maxDY;
  int numBins;
  int count;

  /* Added by Tim, 2/19/2024 */
  /* This line is not supposed to be needed? */
  if ((!subbin) && ((pointertype)elements & BT_ARRAY)) return NULL;

  if(BPD) DumpRect("#### bpBinArrayBuild, TOP bbox= ", &bbox);

  /* figure out good bin dimensions */
  if(!bpBinArraySizeIt(&bbox,
		       elements,
		       &dx,
		       &dy,
		       &maxDX,
		       &maxDY,
		       &numBins,
		       &count)) return NULL;

  /* build the bin array */
  ba = bpBinArrayBuild1(&bbox, elements, dx, dy);

  if(!subbin) return ba;

  /* sub-bin normal bins */
  {
    int dimX = ba->ba_dimX;

    int i;
    for(i=0;i<numBins;i++)
    {
      BinArray *sub;

      sub = bpBinArrayBuild(bpBinArea(ba,i),
			    bpBinList(ba, i),
			    TRUE);

      if(sub)
      {
	ba->ba_bins[i] =
	  (void *) ((pointertype) sub | BT_ARRAY);
      }
    }
  }

  /* sub-bin oversized */
  {
    BinArray *sub;

    sub = bpBinArrayBuild(bbox,
			  bpBinList(ba, numBins),
			  TRUE);

    if(sub)
    {
      ba->ba_bins[numBins] =
	(void *) ((pointertype) sub | BT_ARRAY);
    }
  }

  if(BPD)
  {
    DumpRect("\n#### bpBinArrayBuild, DONE bbox= ", &bbox);
    fprintf(stderr,"\n");
  }
  return ba;
}

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinsUpdate -- update bplane bins
 *
 * Called prior to enumerations.
 *
 * ----------------------------------------------------------------------------
 */
int bpBinLife = 0;
void bpBinsUpdate(BPlane *bp)
{
  Rect bbox;
  bool oldBins;

  /* rebuild whenever inbox gets big */
  if(!bpListExceedsQ(bp->bp_inBox, bpMinBAPop-1)) return;

  /* fprintf(stderr,"DEBUG bpBinsUpdate - rebuilding bins.\n"); */

  /* do bins already exist ? */
  oldBins = (bp->bp_rootNode != 0);

  /* if bins exist, dissolve them */
  if(oldBins)
  {
    Element *elist = bpBinArrayUnbuild(bp->bp_rootNode);

    /* add inbox to list */
    while(bp->bp_inBox)
    {
      /* pop from inbox */
      Element *e = bp->bp_inBox;
      bp->bp_inBox = e->e_link;

      /* add to elist */
      e->e_link = elist;
      elist = e;
    }

    bp->bp_inBox = elist;
  }

  /* compute accurate bbox */
  {
    Element *e = bp->bp_inBox;

    bbox = e->e_rect;

    for(e=bp->bp_inBox; e; e=e->e_link)
    {
      GeoIncludeRectInBBox(&e->e_rect, &bbox);
    }
  }

  /* if rebuild, double bounding box, to avoid too many rebuilds */
  if(oldBins)
  {
    int dx = GEO_WIDTH(&bbox)/2;
    int dy = GEO_HEIGHT(&bbox)/2;
    bbox.r_xbot -= dx;
    bbox.r_ybot -= dy;
    bbox.r_xtop += dx;
    bbox.r_ytop += dy;
  }

  /* build and populate bin array */
  bp->bp_rootNode = bpBinArrayBuild(bbox, bp->bp_inBox, TRUE);
  if(bp->bp_rootNode) bp->bp_inBox = NULL;
  bp->bp_binArea = bbox;
  bp->bp_binLife = bpBinLife;
  bp->bp_inAdds = 0;
  /*  if(BPD) bpDump(bp, 0); */
}











