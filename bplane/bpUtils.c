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




/* bpUtils.c --
 *
 * shared low-level routines for this module.
 *
 */

#include <stdio.h>
#include "utils/utils.h"
#include "database/database.h"
#include "utils/geometry.h"
#include "bplane/bplaneInt.h"


/*
 * ----------------------------------------------------------------------------
 * bpRectDim --
 *
 * return dimension of rectangle in xDir
 *
 * ----------------------------------------------------------------------------
 */		 
int bpRectDim(Rect *r, bool xDir)
{
  return xDir ? r->r_xtop - r->r_xbot : r->r_ytop - r->r_ybot;
}

/*
 * ----------------------------------------------------------------------------
 * bpBinIndices --
 *
 * compute bin indices corresponding to area.
 *
 * ----------------------------------------------------------------------------
 */		 
static __inline__ void 
bpBinIndices(Rect area,        /* area */
	     Rect binArea,     /* lower left corner of bin system */
	     int indexBits,
	     int dim,
	     bool  xDir,       /* TRUE for x bin, FALSE for y bin */
	     int *min,         /* results go here */
	     int *max)
{
  int ref, coord; 
  int index;

}
