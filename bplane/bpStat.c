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



/* bpStat.c
 *
 * routines to get bplane statistics.
 *
 */

#include <stdio.h>
#include <limits.h>
#include "utils/utils.h"
#include "database/database.h"
#include "utils/geometry.h"
#include "bplane/bplaneInt.h"

/*
 * ----------------------------------------------------------------------------
 * bpCount --
 *
 * count list of elements.
 *
 * Returns size of list.
 *
 * ----------------------------------------------------------------------------
 */
int bpCount(Element *e)
{
  int i = 0;

  while(e)
  {
    i++;
    e = e->e_link;
  }

  return i;
}

/*
 * ----------------------------------------------------------------------------
 * bpStatBA --
 *
 * compute bin array statistics.
 * (includes sub-arrays)
 *
 *
 * Returns memory used by bplane (excluding elements)
 *
 * ----------------------------------------------------------------------------
 */
unsigned int bpStatBA(BinArray *ba,
		      int *totCount,        /* total number of elements */
		      int *totBins,         /* ret tot num of bins */
		      int *emptyBins,       /* ret num of empty bins */
		      int *binArraysp,      /* ret num of bin arrays */
		      int *maxEffp,         /* ret max effective list length */
		      int *maxBinCount,     /* ret max count for regular bin */
		      int *totUnbinned,     /* ret tot num of e's not binned */
		      int *maxDepth)        /* ret max bin array depth */
{
  int numBins = ba->ba_numBins;

  /* initial statistics */
  unsigned int mem = 0;
  int tot = 0;
  int bins = 0;
  int emptys = 0;
  int binArrays = 1;
  int maxCount = 0;
  int maxEff = 0;
  int maxEffSub = 0;
  int unbinned = 0;
  int depth = 1;
  int maxDepthSub = 0;
  int i;

  /* add bins in this array */
  bins += numBins;

  /* add memory usage for this array (sub arrays already tabulated) */
  if(ba) mem += sizeof(BinArray) + numBins*sizeof(void*);

  /* gather stats bin by bin */
  for(i=0;i<numBins;i++)
  {
    if(bpBinType(ba,i) != BT_ARRAY)
    {
      /* simple bin */
      int count = bpCount(bpBinList(ba,i));
      tot += count;
      if(count>maxCount) maxCount = count;
      if(count==0) emptys++;
    }
    else
    {
      /* arrayed, recurse */
      int sMem, sTot, sBins, sEmptys, sBinArrays;
      int sMaxEff, sMaxCount, sUnbinned, sDepth;

      sMem = bpStatBA(bpSubArray(ba,i),
		      &sTot,        /* total number of elements */
		      &sBins,       /* ret tot num of bins */
		      &sEmptys,     /* ret num of empty bins */
		      &sBinArrays,  /* ret num bin arrays */
		      &sMaxEff,     /* ret max effective list length */
		      &sMaxCount,   /* ret max count for regular bin */
		      &sUnbinned,    /* ret tot num of e's not binned */
		      &sDepth);       /* ret max bin array depth */

      mem += sMem;
      tot += sTot;
      bins += sBins;
      emptys += sEmptys;
      binArrays += sBinArrays;
      if(sMaxEff > maxEffSub) maxEffSub = sMaxEff;
      if(sMaxCount > maxCount) maxCount = sMaxCount;
      if(sUnbinned > maxCount) maxCount = sUnbinned;
      if(sDepth > maxDepthSub) maxDepthSub = sDepth;
    }
  }

  maxEff += MAX(maxCount,maxEffSub);
  depth += maxDepthSub;

  /* oversized */
  if(bpBinType(ba,numBins) != BT_ARRAY)
  {
    /* oversized unbinned */
    int over = bpCount(bpBinList(ba,numBins));
    tot += over;
    unbinned += over;
    maxEff += over;
  }
  else
  {
    /* oversized is arrayed, recurse */
    int sMem, sTot, sBins, sEmptys, sBinArrays;
    int sMaxEff, sMaxCount, sUnbinned, sDepth;

    sMem = bpStatBA(bpSubArray(ba,numBins),
		    &sTot,        /* total number of elements */
		    &sBins,       /* ret tot num of bins */
		    &sEmptys,     /* ret num of empty bins */
		    &sBinArrays,  /* ret num bin arrays */
		    &sMaxEff,     /* ret max effective list length */
		    &sMaxCount,   /* ret max count for regular bin */
		    &sUnbinned,    /* ret tot num of e's not binned */
		    &sDepth);       /* ret max bin array depth */

    mem += sMem;
    tot += sTot;
    bins += sBins;
    emptys += sEmptys;
    binArrays += sBinArrays;
    maxEff += sMaxEff;
    if(sMaxCount > maxCount) maxCount = sMaxCount;
    unbinned += sUnbinned;
    depth += sDepth;
  }

  /* set results */
  if(totCount) *totCount = tot;
  if(totBins) *totBins = bins;
  if(emptyBins) *emptyBins = emptys;
  if(binArraysp) *binArraysp = binArrays;
  if(maxEffp) *maxEffp = maxEff;
  if(maxBinCount) *maxBinCount = maxCount;
  if(totUnbinned) *totUnbinned = unbinned;
  if(maxDepth) *maxDepth = depth;

  return mem;
}

/*
 * ----------------------------------------------------------------------------
 * BPStat --
 *
 * compute bplane statistics.
 *
 * Returns memory used by bplane (excluding elements)
 *
 * ----------------------------------------------------------------------------
 */
unsigned int BPStat(BPlane *bp,
		      int *totCount,        /* total number of elements */
		      int *inBox,           /* ret num of elements in inBox */
		      int *totBins,         /* ret tot num of bins */
		      int *emptyBins,       /* ret num of empty bins */
		      int *binArraysp,      /* ret tot num of bin arrays */
		      int *maxEffp,         /* ret max effective list length */
		      int *maxBinCount,     /* ret max count for regular bin */
		      int *totUnbinned,     /* ret tot num of e's not binned */
		      int *maxDepth)        /* ret max bin array depth */
{
  BinArray *ba = bp->bp_rootNode;
  unsigned int mem = 0;
  int tot = 0;
  int bins = 0;
  int emptys = 0;
  int binArrays = 0;
  int maxEff = 0;
  int maxCount = 0;
  int unbinned = 0;
  int depth = 0;
  int in;

  /* bin arrays */
  if(ba)
  {
    mem += bpStatBA(bp->bp_rootNode,
		    &tot,        /* total number of elements */
		    &bins,       /* ret tot num of bins */
		    &emptys,     /* ret tot num of empty bins */
		    &binArrays,  /* ret tot num of bin arrays */
		    &maxEff,     /* ret max effective list length */
		    &maxCount,   /* ret max count for regular bin */
		    &unbinned,    /* ret tot num of e's not binned */
		    &depth);       /* ret max bin array depth */
  }

  /* inbox */
  in = bpCount(bp->bp_inBox);
  tot += in;
  maxEff += in;
  unbinned += in;

  /* add in memory usage for bplane */
  mem += sizeof(BPlane);
  mem += IHashStats2(bp->bp_hashTable,NULL,NULL);

  /* set results */
  if(totCount) *totCount = tot;
  if(inBox) *inBox = in;
  if(totBins) *totBins = bins;
  if(emptyBins) *emptyBins = emptys;
  if(binArraysp) *binArraysp = binArrays;
  if(maxEffp) *maxEffp = maxEff;
  if(maxBinCount) *maxBinCount = maxCount;
  if(totUnbinned) *totUnbinned = unbinned;
  if(maxDepth) *maxDepth = depth;

  return mem;
}


/*
 * ----------------------------------------------------------------------------
 * BPStatMemory --
 *
 * returns memory usage of BPlane in bytes
 * (exclusive of elements contained by the BPlane)
 *
 * ----------------------------------------------------------------------------
 */
unsigned int BPStatMemory(BPlane *bp)
{
  return BPStat(bp,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL);
}

