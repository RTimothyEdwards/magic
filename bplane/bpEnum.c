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




/* bpEnum.c -
 *
 * Search routines for bplanes
 *
 */

#include <stdio.h>
#include "utils/utils.h"
#include "database/database.h"
#include "utils/geometry.h"
#include "bplane/bplaneInt.h"

/*
 * ----------------------------------------------------------------------------
 * BPEnumInit --
 *
 * set up search.
 *
 * ----------------------------------------------------------------------------
 */		 
void BPEnumInit(BPEnum *bpe,   /* enum to initialize */
		BPlane *bp,    
		Rect *area,    /* search area */
		int match,     
		char *id)      /* for debugging */
{
  bool inside = FALSE;
  bpe->bpe_plane = bp;
  bpe->bpe_id = id;
  bpe->bpe_match = match;
  bpe->bpe_top = bpe->bpe_stack;

  /*
  fprintf(stderr,"DEBUG bpEnumInit, match=%d id=%s\n",
	  match, id);
  */

  /* link enum to bplane */
  bpe->bpe_next = bp->bp_enums;
  bp->bp_enums = bpe;

  switch (match)
  {

  case BPE_EQUAL:
    GeoCanonicalRect(area, &bpe->bpe_srchArea);
    bpe->bpe_nextElement = IHashLookUp(bp->bp_hashTable, &bpe->bpe_srchArea);
    bpe->bpe_top->bps_state = BPS_HASH;
    /* don't need to setup stack, just return */
    return;

  case BPE_ALL:    
    /* If we start 'INSIDE', no match checks will be done */
    bpe->bpe_top->bps_state = BPS_BINS_INSIDE;
    inside = TRUE;
    break;

  case BPE_TOUCH:
    GeoCanonicalRect(area, &bpe->bpe_srchArea);
    inside = GEO_SURROUND(&bpe->bpe_srchArea, &bp->bp_bbox);
    if(inside)
    {
      bpe->bpe_top->bps_state = BPS_BINS_INSIDE;
    }
    else
    {
      bpe->bpe_top->bps_state = BPS_BINS;	  
      bpe->bpe_subBinMinX = GEO_WIDTH(&bpe->bpe_srchArea)/2;
      bpe->bpe_subBinMinY = GEO_HEIGHT(&bpe->bpe_srchArea)/2;
      bpBinsUpdate(bp);
    }
    break;

  case BPE_OVERLAP:
    GeoCanonicalRect(area, &bpe->bpe_srchArea);
    GEO_EXPAND(&bpe->bpe_srchArea, -1, &bpe->bpe_srchArea);
    inside = GEO_SURROUND(&bpe->bpe_srchArea, &bp->bp_bbox);
    if(inside)
    {
      bpe->bpe_top->bps_state = BPS_BINS_INSIDE;
    }
    else
    {
      bpe->bpe_top->bps_state = BPS_BINS;	  
      bpe->bpe_subBinMinX = GEO_WIDTH(&bpe->bpe_srchArea)/2;
      bpe->bpe_subBinMinY = GEO_HEIGHT(&bpe->bpe_srchArea)/2;
      bpBinsUpdate(bp);
    }
    break;

  default:
    ASSERT(FALSE,"BPEnumInit, bad match value"); 
  }

  /* push rootnode */
  if(bp->bp_rootNode)
  {
    bpEnumPush(bpe, bp->bp_rootNode, inside);
    bpe->bpe_nextElement = NULL; 
  }
  else
  {
    /* no bins, go straight to inbox */
    bpe->bpe_top->bps_state = BPS_INBOX | inside;
    bpe->bpe_nextElement = bp->bp_inBox;
  }
}

/*
 * ----------------------------------------------------------------------------
 * BPEnumTerm --
 *
 * terminate enumeration
 *
 * ----------------------------------------------------------------------------
 */		 
void BPEnumTerm(BPEnum *bpe)
{
  BPEnum **linkp;

  /*
  fprintf(stderr,"DEBUG bpEnumTerm, id=%s\n",
	  bpe->bpe_id);
  */

  /* unlink */

  linkp = &bpe->bpe_plane->bp_enums;
  while(*linkp && *linkp != bpe) linkp = &(*linkp)->bpe_next;
  ASSERT(*linkp==bpe,"BPEnumTerm");
  *linkp = bpe->bpe_next;
}




