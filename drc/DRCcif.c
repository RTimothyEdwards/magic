/*
 * DRCcif.c --
 *
 ******************************************************************************
 * Copyright (C) 1989 Digital Equipment Corporation                           
 * Permission to use, copy, modify, and distribute this              
 * software and its documentation for any purpose and without       
 * fee is hereby granted, provided that the above copyright        
 * notice appear in all copies.  Digital Equipment Corporation    
 * makes no representations about the suitability of this        
 * software for any purpose.  It is provided "as is" without    
 * express or implied warranty.  
 *                                                               
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL  
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT         
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL 
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR    
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS 
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS 
 * SOFTWARE. 
 ****************************************************************************
 *
 */

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCcif.c,v 1.5 2010/10/20 20:34:20 tim Exp $";
#endif	/* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "dbwind/dbwtech.h"
#include "drc/drc.h"
#include "cif/cif.h"
#include "cif/CIFint.h"
#include "utils/signals.h"
#include "utils/stack.h"
#include "utils/malloc.h"
#include "utils/utils.h"

extern char *drcWhyDup(); 
extern int  drcCifTile();
extern int  areaCifCheck();
extern void drcCheckCifMaxwidth();
extern void drcCheckCifArea();

extern Stack	*DRCstack;

#define PUSHTILE(tp) \
    if ((tp)->ti_client == (ClientData) DRC_UNPROCESSED) { \
        (tp)->ti_client = (ClientData)  DRC_PENDING; \
        STACKPUSH((ClientData) (tp), DRCstack); \
    }

extern CIFStyle	*drcCifStyle;
extern bool DRCForceReload;
TileTypeBitMask drcCifGenLayers;

DRCCookie *drcCifRules[MAXCIFLAYERS][2];
DRCCookie *drcCifCur=NULL;
int	drcCifValid = FALSE;
int	beenWarned;

#define DRC_CIF_SPACE		0
#define DRC_CIF_SOLID		1

/*
 * ----------------------------------------------------------------------------
 *
 * drcCifSetStyle --
 *
 * Process a declaration of the cif style.
 * This is of the form:
 *
 *	cifstyle cif_style
 *
 * e.g,
 *
 *	cifstyle pg
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	Updates drcCifStyle.  Do NOT attempt to update the CIF style
 *	in the middle of reading the DRC section.  Instead, if the
 *	reported CIF style is not current, flag a warning.  The DRC
 *	will be re-read with the CIF extensions when the CIF output
 *	style is changed.
 *
 * ----------------------------------------------------------------------------
 */

int
drcCifSetStyle(argc, argv)
    int argc;
    char *argv[];
{
    CIFKeep     *new;

    for (new = CIFStyleList; new != NULL; new = new->cs_next)
    {
	if (!strcmp(new->cs_name, argv[1]))
	{
	    DRCForceReload = TRUE;
	    if (!strcmp(new->cs_name, CIFCurStyle->cs_name))
		drcCifStyle = CIFCurStyle;
	    else
	    {
		TechError("DRC cif extensions are not enabled.\n\t"
			"Use \"cif ostyle %s\" to enable them.\n",
			new->cs_name);
		drcCifStyle = NULL;
		beenWarned = TRUE;	/* post no more error messages */
	    }
	    return 0;
	}
    }
    TechError("Unknown DRC cifstyle %s\n",argv[1]);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 */

int
drcCifWarning()
{
    if (!beenWarned)
    {
	TechError("Missing cif style for drc\n\t"
		"This message will not be repeated.\n");
	beenWarned = TRUE;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcCifWidth -- same this as drcCifWidth, except that it works on 
 *	cif layers
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcCifWidth(argc, argv)
    int argc;
    char *argv[];
{
    char *layername = argv[1];
    int scalefactor;
    int centidistance = atoi(argv[2]);
    char *why = drcWhyDup(argv[3]);
    TileTypeBitMask set, setC, tmp1;
    int	thislayer = -1;
    DRCCookie *dpnew,*dpnext;
    TileType i;

    if (drcCifStyle == NULL)
	return drcCifWarning();

    for (i = 0; i < drcCifStyle->cs_nLayers;i++)
    {
    	 CIFLayer	*layer = drcCifStyle->cs_layers[i];
	 
	 if (strcmp(layer->cl_name,layername) == 0) 
	 {
	      thislayer = i;
	      break;
	 }
    }
    if (thislayer == -1)
    {
    	 TechError("Unknown cif layer: %s\n",layername);
         return (0);
    }

    scalefactor = drcCifStyle->cs_scaleFactor;
    centidistance *= drcCifStyle->cs_expander;		// BSI

    dpnext = drcCifRules[thislayer][DRC_CIF_SPACE];
    dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
    drcAssign(dpnew, centidistance, dpnext, &CIFSolidBits,
    		&CIFSolidBits, why, centidistance,
		DRC_FORWARD, thislayer, 0);
    drcCifRules[thislayer][DRC_CIF_SPACE] = dpnew;

    return ((centidistance+scalefactor-1)/scalefactor);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcCifSpacing -- same this as drcSpacing, except that it works on cif 
 * 	layers.
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcCifSpacing(argc, argv)
    int argc;
    char *argv[];
{
    char *adjacency = argv[4];
    char *why = drcWhyDup(argv[5]);
    DRCCookie *dpnext, *dpnew;
    int needReverse = FALSE;
    TileType i, j;
    int scalefactor;
    int centidistance = atoi(argv[3]);
    char *layers[2];
    TileType layer[2];
    TileTypeBitMask	cmask;
    int	k;

    layers[0] = argv[1];
    layers[1] = argv[2];
    
    if (drcCifStyle == NULL)
	return drcCifWarning();

    for (k=0; k!= 2;k++)
    {
         for (i = 0; i < drcCifStyle->cs_nLayers;i++)
         {
    	      CIFLayer	*l = drcCifStyle->cs_layers[i];
	 
	      if (strcmp(l->cl_name,layers[k]) == 0)
	      {
	      	   layer[k]=i;
	           break;
	      } 
         }
         if (i == drcCifStyle->cs_nLayers || layer[k] == -1)
         {
    	      TechError("Unknown cif layer: %s",layers[k]);
              return (0);
         }
    }

    if (strcmp (adjacency, "touching_ok") == 0)
    {
	/* If touching is OK, everything must fall in the same plane. */
	if (layer[0] != layer[1])
	{
	    TechError(
		"Spacing check with touching ok must all be in one plane.\n");
	    return (0);
	}
	cmask = DBSpaceBits;
    }
    else if (strcmp (adjacency, "touching_illegal") == 0)
    {
	 cmask = DBAllTypeBits;
	 needReverse = TRUE;
         /* nothing for now */
    }
    else
    {
	TechError("Badly formed drc spacing line\n");
	return (0);
    }
    
    scalefactor = drcCifStyle->cs_scaleFactor;
    centidistance *= drcCifStyle->cs_expander;		// BSI
    dpnext = drcCifRules[layer[0]][DRC_CIF_SOLID];
    dpnew = (DRCCookie *) mallocMagic((unsigned) sizeof (DRCCookie));
    drcAssign(dpnew, centidistance, dpnext, &DBSpaceBits,
    		&cmask, why, centidistance, DRC_FORWARD, layer[1], 0);
    drcCifRules[layer[0]][DRC_CIF_SOLID] = dpnew;
    if (needReverse) dpnew->drcc_flags |= DRC_BOTHCORNERS;

    // Add rule in reverse direction
    dpnext = drcCifRules[layer[0]][DRC_CIF_SPACE];
    dpnew = (DRCCookie *) mallocMagic((unsigned) sizeof (DRCCookie));
    drcAssign(dpnew, centidistance, dpnext, &DBSpaceBits,
    		&cmask, why, centidistance, DRC_REVERSE, layer[1], 0);
    drcCifRules[layer[0]][DRC_CIF_SPACE] = dpnew;
    
    if (needReverse)
    {
	 // This is not so much "reverse" as it is just the
	 // rule for b->a spacing that matches the a->b spacing.

         dpnew->drcc_flags |= DRC_BOTHCORNERS;
         dpnext = drcCifRules[layer[1]][DRC_CIF_SOLID];
         dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
         drcAssign(dpnew, centidistance, dpnext, &DBSpaceBits, &cmask,
		why, centidistance, DRC_FORWARD|DRC_BOTHCORNERS, layer[0], 0);
         drcCifRules[layer[1]][DRC_CIF_SOLID] = dpnew;

	 // Add rule in reverse direction
	 dpnext = drcCifRules[layer[1]][DRC_CIF_SPACE];
	 dpnew = (DRCCookie *) mallocMagic((unsigned) sizeof (DRCCookie));
	 drcAssign(dpnew, centidistance, dpnext, &DBSpaceBits, &cmask,
		why, centidistance, DRC_REVERSE|DRC_BOTHCORNERS, layer[0], 0);
	 drcCifRules[layer[1]][DRC_CIF_SPACE] = dpnew;
    
	 if (layer[0] == layer[1])
	 {
              dpnext = drcCifRules[layer[1]][DRC_CIF_SPACE];
              dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
              drcAssign(dpnew, centidistance, dpnext, &DBSpaceBits,
			&cmask, why, centidistance, DRC_REVERSE | DRC_BOTHCORNERS,
			layer[0], 0);
                 drcCifRules[layer[1]][DRC_CIF_SPACE] = dpnew;
	 
              dpnext = drcCifRules[layer[0]][DRC_CIF_SPACE];
              dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
              drcAssign(dpnew, centidistance, dpnext, &DBSpaceBits, &cmask,
			why, centidistance, DRC_REVERSE | DRC_BOTHCORNERS,
			layer[1], 0);
                 drcCifRules[layer[0]][DRC_CIF_SPACE] = dpnew;
	 }
    }

    if (layer[0] != layer[1]) /* make sure they don't overlap exactly */
    {
         dpnext = drcCifRules[layer[1]][DRC_CIF_SPACE];
         dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
         drcAssign(dpnew, scalefactor, dpnext, &DBSpaceBits, &DBZeroTypeBits,
			why, scalefactor, DRC_FORWARD, layer[0], 0);
         drcCifRules[layer[1]][DRC_CIF_SPACE] = dpnew;

         dpnext = drcCifRules[layer[0]][DRC_CIF_SPACE];
         dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
         drcAssign(dpnew, scalefactor, dpnext, &DBSpaceBits, &DBZeroTypeBits,
			why, scalefactor, DRC_FORWARD, layer[1], 0);
         drcCifRules[layer[0]][DRC_CIF_SPACE] = dpnew;
    }
    
    return ((centidistance+scalefactor-1)/scalefactor);
}

/*
 * ----------------------------------------------------------------------------
 * Scale CIF/DRC rules to match grid scaling.  Scale by factor (n / d)
 * ----------------------------------------------------------------------------
 */

void
drcCifScale(int n, int d)
{
    DRCCookie *dp;
    int i, j;

    if (DRCCurStyle != NULL)
    {
	for (i = 0; i != MAXCIFLAYERS; i++)
	    for (j = 0; j < 2; j++)
		for (dp = drcCifRules[i][j]; dp != NULL; dp = dp->drcc_next)
		{
		    if (dp->drcc_dist != 0)
		    {
			dp->drcc_dist *= n;
			dp->drcc_dist /= d;
		    }
		    if (dp->drcc_cdist != 0)
		    {
			dp->drcc_cdist *= n;
			dp->drcc_cdist /= d;
		    }
		}
    }
}

/*
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 */

void
drcCifFreeStyle()
{
    DRCCookie *dp;
    int i;
    char *old;

    if (DRCCurStyle != NULL)
    {
	for (i = 0; i != MAXCIFLAYERS; i++)
	{
	    dp = drcCifRules[i][DRC_CIF_SPACE];
	    while (dp != NULL)
	    {
		old = (char *)dp;
	 	dp = dp->drcc_next;
		freeMagic(old);
	    }
	    dp = drcCifRules[i][DRC_CIF_SOLID];
	    while (dp != NULL)
	    {
		old = (char *)dp;
	 	dp = dp->drcc_next;
		freeMagic(old);
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 */

void
drcCifInit()
{
    int i;

    if (drcCifValid == TRUE)
	drcCifFreeStyle();

    for (i = 0; i != MAXCIFLAYERS; i++)
    {
	drcCifRules[i][DRC_CIF_SPACE] = NULL;
	drcCifRules[i][DRC_CIF_SOLID] = NULL;
    }
    drcCifValid = FALSE;
    TTMaskZero(&drcCifGenLayers);
    beenWarned = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 */

void
drcCifFinal()
{
    int i;

    for (i = 0; i != MAXCIFLAYERS; i++)
    {
        DRCCookie *dp;
     	  
	for (dp = drcCifRules[i][DRC_CIF_SPACE]; dp; dp = dp->drcc_next)
	{
	    drcCifValid = TRUE;
            TTMaskSetType(&drcCifGenLayers, i);
            TTMaskSetType(&drcCifGenLayers, dp->drcc_plane);
	}
	for (dp = drcCifRules[i][DRC_CIF_SOLID]; dp; dp = dp->drcc_next)
	{
     	    drcCifValid = TRUE;
            TTMaskSetType(&drcCifGenLayers, i);
            TTMaskSetType(&drcCifGenLayers, dp->drcc_plane);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * drcCifCheck---
 *
 * This is the primary routine for design-rule checking on CIF layers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Error paint, CIF layer generation, lots of stuff going on.
 *	
 * ----------------------------------------------------------------------------
 */

void
drcCifCheck(arg)
    struct drcClientData *arg;
{
    Rect	*checkRect = arg->dCD_rect;
    Rect	cifrect;
    int		scale;
    int		i,j;
    int		oldTiles;

    if (drcCifValid == FALSE) return;
    else if (CIFCurStyle != drcCifStyle) return;

    scale = drcCifStyle->cs_scaleFactor;
    cifrect = *checkRect;
    cifrect.r_xbot *= scale;
    cifrect.r_xtop *= scale;
    cifrect.r_ybot *= scale;
    cifrect.r_ytop *= scale;
    arg->dCD_rect = &cifrect;
    oldTiles = DRCstatTiles;

    CIFGen(arg->dCD_celldef, checkRect, CIFPlanes, &DBAllTypeBits, TRUE, TRUE);

    for (i = 0; i < drcCifStyle->cs_nLayers; i++)
    {
        for (j = 0; j != 2; j++)
	{
	    for (drcCifCur = drcCifRules[i][j]; 
	       		drcCifCur; drcCifCur = drcCifCur->drcc_next)
            {
	  	TileTypeBitMask	*mask;
	  
		arg->dCD_plane = i;
	        DBSrPaintArea((Tile *) NULL, CIFPlanes[i], &cifrect,
			(j == DRC_CIF_SOLID) ? &DBSpaceBits : &CIFSolidBits,
	  		drcCifTile, arg);
     	     }
	 }
     }
     arg->dCD_rect = checkRect;
     DRCstatCifTiles += DRCstatTiles - oldTiles;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcCifTile --
 *
 * Results:
 *	Zero (so that the search will continue), unless an interrupt
 *	occurs, in which case 1 is returned to stop the check.
 *
 * Side effects:
 *	Calls the client's error function if errors are found.
 *
 * ----------------------------------------------------------------------------
 */

int
drcCifTile (tile, arg)
    Tile *tile;	/* Tile being examined */
    struct drcClientData *arg;
{
    DRCCookie *cptr;	/* Current design rule on list */
    Tile *tp;		/* Used for corner checks */
    Rect *rect = arg->dCD_rect;	/* Area being checked */
    Rect errRect;		/* Area checked for an individual rule */
    TileTypeBitMask tmpMask;

    arg->dCD_constraint = &errRect;
    arg->dCD_radial = 0;

    /*
     * If we were interrupted, we want to
     * abort the check as quickly as possible.
     */
    if (SigInterruptPending) return 1;
    DRCstatTiles++;

    /*
     * Check design rules along a vertical boundary between two tiles.
     *
     *			      1 | 4
     *				T
     *				|
     *			tpleft	|  tile
     *				|
     *				B
     *			      2 | 3
     *
     * The labels "T" and "B" indicate pointT and pointB respectively.
     *
     * If a rule's direction is FORWARD, then check from left to right.
     *
     *	    * Check the top right corner if the 1x1 lambda square
     *	      on the top left corner (1) of pointT matches the design
     *	      rule's "corner" mask.
     *
     *	    * Check the bottom right corner if the rule says check
     *	      BOTHCORNERS and the 1x1 lambda square on the bottom left
     *	      corner (2) of pointB matches the design rule's "corner" mask.
     *
     * If a rule's direction is REVERSE, then check from right to left.
     *
     *	    * Check the bottom left corner if the 1x1 lambda square
     *	      on the bottom right corner (3) of pointB matches the design
     *	      rule's "corner" mask.
     *
     *	    * Check the top left corner if the rule says check BOTHCORNERS
     *	      and the 1x1 lambda square on the top right corner (4) of
     *	      pointT matches the design rule's "corner" mask.
     */

    if (drcCifCur->drcc_flags & DRC_AREA)
    {
    	 drcCheckCifArea(tile, arg, drcCifCur);
	 return 0;
    }
    if (drcCifCur->drcc_flags & DRC_MAXWIDTH)
    {
    	 drcCheckCifMaxwidth(tile, arg, drcCifCur);
	 return 0;
    }
    if (LEFT(tile) >= rect->r_xbot)		/* check tile against rect */
    {
	Tile *tpleft;
	int edgeTop, edgeBot;
        int top = MIN(TOP(tile), rect->r_ytop);
        int bottom = MAX(BOTTOM(tile), rect->r_ybot);
	int edgeX = LEFT(tile);

        for (tpleft = BL(tile); BOTTOM(tpleft) < top; tpleft = RT(tpleft))
        {
	    /* Don't check synthetic edges, i.e. edges with same type on
             * both sides.  Such "edges" have no physical significance, and
	     * depend on internal-details of how paint is spit into tiles.
	     * Thus checking them just leads to confusion.  (When edge rules
	     * involving such edges are encountered during technology readin
	     * the user is warned that such edges are not checked).
	     */
	    if (TiGetRightType(tpleft) == TiGetLeftType(tile))
	        continue;

	    /*
	     * Go through list of design rules triggered by the
	     * left-to-right edge.
	     */
	    edgeTop = MIN(TOP (tpleft), top);
	    edgeBot = MAX(BOTTOM(tpleft), bottom);
	    if (edgeTop <= edgeBot)
		continue;

	    /* do this more intelligently later XXX */
	    cptr = drcCifCur;
	    {
		errRect.r_ytop = edgeTop;
		errRect.r_ybot = edgeBot;

		if (cptr->drcc_flags & DRC_REVERSE)
		{
		    /*
		     * Determine corner extensions.
		     * Find the point (3) to the bottom right of pointB
		     */
		    for (tp = tile; BOTTOM(tp) >= errRect.r_ybot; tp = LB(tp))
			/* Nothing */;
		    if (TTMaskHasType(&cptr->drcc_corner, TiGetTopType(tp)))
		    {
			errRect.r_ybot -= cptr->drcc_cdist;
			if (DRCEuclidean)
			    arg->dCD_radial |= 0x1000;
		    }

		    if (cptr->drcc_flags & DRC_BOTHCORNERS)
		    {
			/*
			 * Check the other corner by finding the
			 * point (4) to the top right of pointT.
			 */
			if (TOP(tp = tile) <= errRect.r_ytop)
			    for (tp = RT(tp); LEFT(tp) > edgeX; tp = BL(tp))
				/* Nothing */;
			if (TTMaskHasType(&cptr->drcc_corner, TiGetBottomType(tp)))
			{
			    errRect.r_ytop += cptr->drcc_cdist;
			    if (DRCEuclidean)
				arg->dCD_radial |= 0x2000;
			}
		    }

		    /*
		     * Just for grins, see if we could avoid a messy search
		     * by looking only at tpleft.
		     */
		    errRect.r_xbot = edgeX - cptr->drcc_dist;
		    if (LEFT(tpleft) <= errRect.r_xbot
			&& BOTTOM(tpleft) <= errRect.r_ybot
			&& TOP(tpleft) >= errRect.r_ytop
			&& arg->dCD_plane == cptr->drcc_plane
			&& TTMaskHasType(&cptr->drcc_mask, TiGetType(tpleft)))
			    continue;
		    errRect.r_xtop = edgeX;
		    arg->dCD_initial = tile;
		}
		else	/* FORWARD */
		{
		    /*
		     * Determine corner extensions.
		     * Find the point (1) to the top left of pointT
		     */
		    for (tp = tpleft; TOP(tp) <= errRect.r_ytop; tp = RT(tp))
			/* Nothing */;
		    if (TTMaskHasType(&cptr->drcc_corner, TiGetBottomType(tp)))
		    {
			errRect.r_ytop += cptr->drcc_cdist;
			if (DRCEuclidean)
			    arg->dCD_radial |= 0x8000;
		    }

		    if (cptr->drcc_flags & DRC_BOTHCORNERS)
		    {
			/*
			 * Check the other corner by finding the
			 * point (2) to the bottom left of pointB.
			 */
			if (BOTTOM(tp = tpleft) >= errRect.r_ybot)
			    for (tp = LB(tp); RIGHT(tp) < edgeX; tp = TR(tp))
				/* Nothing */;
			if (TTMaskHasType(&cptr->drcc_corner, TiGetTopType(tp)))
			{
			    errRect.r_ybot -= cptr->drcc_cdist;
			    if (DRCEuclidean)
				arg->dCD_radial |= 0x4000;
			}
		    }

		    /*
		     * Just for grins, see if we could avoid a messy search
		     * by looking only at tile.
		     */
		    errRect.r_xtop = edgeX + cptr->drcc_dist;
		    if (RIGHT(tile) >= errRect.r_xtop
			&& BOTTOM(tile) <= errRect.r_ybot
			&& TOP(tile) >= errRect.r_ytop
			&& arg->dCD_plane == cptr->drcc_plane
			&& TTMaskHasType(&cptr->drcc_mask, TiGetLeftType(tile)))
			    continue;
		    errRect.r_xbot = edgeX;
		    arg->dCD_initial= tpleft;
		}
		if (arg->dCD_radial)
		{
		    arg->dCD_radial &= 0xf0000;
		    arg->dCD_radial |= (0xfff & cptr->drcc_cdist);
		}

		DRCstatSlow++;
		arg->dCD_cptr = (DRCCookie *)cptr;
		TTMaskCom2(&tmpMask, &cptr->drcc_mask);
		(void) DBSrPaintArea((Tile *) NULL,
		    CIFPlanes[cptr->drcc_plane],
		    &errRect, &tmpMask, areaCifCheck, (ClientData) arg);
	    }
	    DRCstatEdges++;
        }
    }


    /*
     * Check design rules along a horizontal boundary between two tiles.
     *
     *			 4	tile	    3
     *			--L----------------R--
     *			 1	tpbot	    2
     *
     * The labels "L" and "R" indicate pointL and pointR respectively.
     * If a rule's direction is FORWARD, then check from bottom to top.
     *
     *      * Check the top left corner if the 1x1 lambda square on the bottom
     *        left corner (1) of pointL matches the design rule's "corner" mask.
     *
     *      * Check the top right corner if the rule says check BOTHCORNERS and
     *        the 1x1 lambda square on the bottom right (2) corner of pointR
     *	      matches the design rule's "corner" mask.
     *
     * If a rule's direction is REVERSE, then check from top to bottom.
     *
     *	    * Check the bottom right corner if the 1x1 lambda square on the top
     *	      right corner (3) of pointR matches the design rule's "corner"
     *	      mask.
     *
     *	    * Check the bottom left corner if the rule says check BOTHCORNERS
     *	      and the 1x1 lambda square on the top left corner (4) of pointL
     *	      matches the design rule's "corner" mask.
     */

    if (BOTTOM(tile) >= rect->r_ybot)
    {
	Tile *tpbot;
	int edgeLeft, edgeRight;
        int left = MAX(LEFT(tile), rect->r_xbot);
        int right = MIN(RIGHT(tile), rect->r_xtop);
	int edgeY = BOTTOM(tile);

	/* Go right across bottom of tile */
        for (tpbot = LB(tile); LEFT(tpbot) < right; tpbot = TR(tpbot))
        {

	    /* Don't check synthetic edges, i.e. edges with same type on
             * both sides.  Such "edges" have no physical significance, and
	     * depend on internal-details of how paint is spit into tiles.
	     * Thus checking them just leads to confusion.  (When edge rules
	     * involving such edges are encountered during technology readin
	     * the user is warned that such edges are not checked).
	     */
	    if(TiGetTopType(tpbot) == TiGetBottomType(tile))
	        continue;

	    /*
	     * Check to insure that we are inside the clip area.
	     * Go through list of design rules triggered by the
	     * bottom-to-top edge.
	     */
	    edgeLeft = MAX(LEFT(tpbot), left);
	    edgeRight = MIN(RIGHT(tpbot), right);
	    if (edgeLeft >= edgeRight)
		continue;

	    cptr = drcCifCur;
	    {
		DRCstatRules++;
		errRect.r_xbot = edgeLeft;
		errRect.r_xtop = edgeRight;

		/* top to bottom */
		if (cptr->drcc_flags & DRC_REVERSE)
		{
		    /*
		     * Determine corner extensions.
		     * Find the point (3) to the top right of pointR
		     */
		    if (RIGHT(tp = tile) <= errRect.r_xtop)
			for (tp = TR(tp); BOTTOM(tp) > edgeY; tp = LB(tp))
			    /* Nothing */;
		    if (TTMaskHasType(&cptr->drcc_corner, TiGetLeftType(tp)))
		    {
			errRect.r_xtop += cptr->drcc_cdist; 	
			if (DRCEuclidean)
			    arg->dCD_radial |= 0x4000;
		    }

		    if (cptr->drcc_flags & DRC_BOTHCORNERS)
		    {
			/*
			 * Check the other corner by finding the
			 * point (4) to the top left of pointL.
			 */
			for (tp = tile; LEFT(tp) >= errRect.r_xbot; tp = BL(tp))
			    /* Nothing */;
			if (TTMaskHasType(&cptr->drcc_corner, TiGetRightType(tp)))
			{
			    errRect.r_xbot -= cptr->drcc_cdist; 	
			    if (DRCEuclidean)
				arg->dCD_radial |= 0x1000;
			}
		    }

		    /*
		     * Just for grins, see if we could avoid
		     * a messy search by looking only at tpbot.
		     */
		    errRect.r_ybot = edgeY - cptr->drcc_dist;
		    if (BOTTOM(tpbot) <= errRect.r_ybot
			&& LEFT(tpbot) <= errRect.r_xbot
			&& RIGHT(tpbot) >= errRect.r_xtop
			&& arg->dCD_plane == cptr->drcc_plane
			&& TTMaskHasType(&cptr->drcc_mask, TiGetTopType(tpbot)))
			    continue;
		    errRect.r_ytop = edgeY;
		    arg->dCD_initial = tile;
		}
		else	/* FORWARD */
		{
		    /*
		     * Determine corner extensions.
		     * Find the point (1) to the bottom left of pointL
		     */
		    if (LEFT(tp = tpbot) >= errRect.r_xbot)
			for (tp = BL(tp); TOP(tp) < edgeY; tp = RT(tp))
			    /* Nothing */;

		    if (TTMaskHasType(&cptr->drcc_corner, TiGetRightType(tp)))
		    {
			errRect.r_xbot -= cptr->drcc_cdist;
			if (DRCEuclidean)
			    arg->dCD_radial |= 0x2000;
		    }

		    if (cptr->drcc_flags & DRC_BOTHCORNERS)
		    {
			/*
			 * Check the other corner by finding the
			 * point (2) to the bottom right of pointR.
			 */
			for (tp=tpbot; RIGHT(tp) <= errRect.r_xtop; tp=TR(tp))
			    /* Nothing */;
			if (TTMaskHasType(&cptr->drcc_corner, TiGetLeftType(tp)))
			{
			    errRect.r_xtop += cptr->drcc_cdist;
			    if (DRCEuclidean)
				arg->dCD_radial |= 0x8000;
			}
		    }

		    /*
		     * Just for grins, see if we could avoid
		     * a messy search by looking only at tile.
		     */
		    errRect.r_ytop = edgeY + cptr->drcc_dist;
		    if (TOP(tile) >= errRect.r_ytop
			&& LEFT(tile) <= errRect.r_xbot
			&& RIGHT(tile) >= errRect.r_xtop
			&& arg->dCD_plane == cptr->drcc_plane
			&& TTMaskHasType(&cptr->drcc_mask, TiGetType(tile)))
			    continue;
		    errRect.r_ybot = edgeY;
		    arg->dCD_initial = tpbot;
		}
		if (arg->dCD_radial)
		{
		    arg->dCD_radial &= 0xf000;
		    arg->dCD_radial |= (0xfff & cptr->drcc_cdist);
		}

		DRCstatSlow++;
		arg->dCD_cptr = (DRCCookie *)cptr;
		TTMaskCom2(&tmpMask, &cptr->drcc_mask);
		(void) DBSrPaintArea((Tile *) NULL,
		    CIFPlanes[cptr->drcc_plane],
		    &errRect, &tmpMask, areaCifCheck, (ClientData) arg);
	    }
	    DRCstatEdges++;
        }
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * areaCifCheck -- 
 *
 * Call the function passed down from DRCBasicCheck() if the current tile
 * violates the rule in the given DRCCookie.  If the rule's connectivity
 * flag is set, then make sure the violating material isn't connected
 * to what's on the initial side of the edge before calling the client
 * error function.
 *
 * This function is called from DBSrPaintArea().
 *
 * Results:
 *	Zero (so that the search will continue).
 *
 * Side effects:
 *      Applies the function passed as an argument.
 *
 * ----------------------------------------------------------------------------
 */

int
areaCifCheck(tile, arg) 
    Tile *tile;
    struct drcClientData *arg;
{
    Rect rect;		/* Area where error is to be recorded. */
    int			scale = drcCifStyle->cs_scaleFactor;

    /* If the tile has a legal type, then return. */

    if (TTMaskHasType(&arg->dCD_cptr->drcc_mask, TiGetType(tile))) return 0;

    /* Only consider the portion of the suspicious tile that overlaps
     * the clip area for errors.
     */

    TiToRect(tile, &rect);
    GeoClip(&rect, arg->dCD_constraint);
    if ((rect.r_xbot >= rect.r_xtop) || (rect.r_ybot >= rect.r_ytop))
	return 0;
    rect.r_xbot /= scale;
    rect.r_xtop /= scale;
    if (rect.r_xbot == rect.r_xtop)
    {
    	 if (rect.r_xbot < 0) rect.r_xbot--; else rect.r_xtop++;
    }
    rect.r_ybot /= scale;
    rect.r_ytop /= scale;
    if (rect.r_ybot == rect.r_ytop)
    {
    	 if (rect.r_ybot < 0) rect.r_ybot--; else rect.r_ytop++;
    }
    GeoClip(&rect, arg->dCD_clip);
    if ((rect.r_xbot >= rect.r_xtop) || (rect.r_ybot >= rect.r_ytop))
	return 0;

    (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
	arg->dCD_cptr, arg->dCD_clientData);
    (*(arg->dCD_errors))++;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcCifArea --
 *
 * Process an area rule.
 * This is of the form:
 *
 *	cifarea layers distance why
 *
 * e.g,
 *
 *	cifarea VIA 4 "via area must be at least 4"
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcCifArea(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int centiarea = atoi(argv[2]);
    int	centihorizon = atoi(argv[3]);
    char *why = drcWhyDup(argv[4]);
    TileTypeBitMask set, setC, tmp1;
    DRCCookie *dpnext, *dpnew;
    TileType i, j;
    int plane;
    int	thislayer;
    int scalefactor;

    if (drcCifStyle == NULL)
	return drcCifWarning();

    for (i = 0; i < drcCifStyle->cs_nLayers;i++)
    {
    	 CIFLayer	*layer = drcCifStyle->cs_layers[i];
	 
	 if (strcmp(layer->cl_name,layers) == 0) 
	 {
	      thislayer = i;
	      break;
	 }
    }
    if (thislayer == -1)
    {
    	 TechError("Unknown cif layer: %s\n",layers);
         return (0);
    }

    scalefactor = drcCifStyle->cs_scaleFactor;
    centiarea *= (drcCifStyle->cs_expander * drcCifStyle->cs_expander);
    dpnext = drcCifRules[thislayer][DRC_CIF_SPACE];
    dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
    drcAssign(dpnew, centihorizon, dpnext, &CIFSolidBits, &CIFSolidBits,
		why, centiarea, DRC_AREA | DRC_FORWARD, thislayer, 0);
    drcCifRules[thislayer][DRC_CIF_SPACE] = dpnew;


    return ((centihorizon+scalefactor-1)/scalefactor);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcCifMaxwidth --  cif version of drc list.
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcCifMaxwidth(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int centidistance = atoi(argv[2]);
    char *bends = argv[3];
    char *why = drcWhyDup(argv[4]);
    TileTypeBitMask set, setC, tmp1;
    DRCCookie *dpnext, *dpnew;
    TileType i, j;
    int plane;
    int bend;
    int thislayer;
    int scalefactor;

    if (drcCifStyle == NULL)
	return drcCifWarning();

    for (i = 0; i < drcCifStyle->cs_nLayers;i++)
    {
    	 CIFLayer	*layer = drcCifStyle->cs_layers[i];
	 
	 if (strcmp(layer->cl_name,layers) == 0) 
	 {
	      thislayer = i;
	      break;
	 }
    }
    if (thislayer == -1)
    {
    	 TechError("Unknown cif layer: %s\n",layers);
         return (0);
    }

    if (strcmp(bends,"bend_illegal") == 0) bend =0;
    else if (strcmp(bends,"bend_ok") == 0) bend =DRC_BENDS;
    else
    {
    	 TechError("unknown bend option %s\n",bends);
	 return (0);
    }
    
    scalefactor = drcCifStyle->cs_scaleFactor;
    centidistance *= drcCifStyle->cs_expander;		// BSI
    dpnext = drcCifRules[thislayer][DRC_CIF_SPACE];
    dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
    drcAssign(dpnew, centidistance, dpnext, &CIFSolidBits, &CIFSolidBits,
		why, centidistance, DRC_MAXWIDTH | bend, thislayer, 0);
    drcCifRules[thislayer][DRC_CIF_SPACE] = dpnew;


    return ((centidistance+scalefactor-1)/scalefactor);
}

/*
 *-------------------------------------------------------------------------
 *
 * drcCifCheckArea--
 *
 *	checks to see that a collection of cif tiles 
 *	have more than a minimum area.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	May cause errors to be painted.
 *
 *-------------------------------------------------------------------------
 */

void
drcCheckCifArea(starttile, arg, cptr)
    Tile		 *starttile;
    struct drcClientData *arg;
    DRCCookie	 *cptr;
{
     int		arealimit = cptr->drcc_cdist;
     long		area = 0L;
     TileTypeBitMask	*oktypes = &cptr->drcc_mask;
     Tile		*tile,*tp;
     Rect		*cliprect = arg->dCD_rect;
     int 		scale = drcCifStyle->cs_scaleFactor;
     
    arg->dCD_cptr = (DRCCookie *)cptr;
    if (DRCstack == (Stack *) NULL)
	DRCstack = StackNew(64);

    /* Mark this tile as pending and push it */
    PUSHTILE(starttile);

    while (!StackEmpty(DRCstack))
    {
	tile = (Tile *) STACKPOP(DRCstack);
	if (tile->ti_client != (ClientData)DRC_PENDING) continue;
	area += (long)(RIGHT(tile)-LEFT(tile))*(TOP(tile)-BOTTOM(tile));
	tile->ti_client = (ClientData)DRC_PROCESSED;
	/* are we at the clip boundary? If so, skip to the end */
	if (RIGHT(tile) == cliprect->r_xtop ||
	    LEFT(tile) == cliprect->r_xbot ||
	    BOTTOM(tile) == cliprect->r_ybot ||
	    TOP(tile) == cliprect->r_ytop) goto forgetit;

         if (area >= (long)arealimit) goto forgetit;

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp)))	PUSHTILE(tp);

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp))) PUSHTILE(tp);

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp))) PUSHTILE(tp);

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp))) PUSHTILE(tp);
     }
     if (area < (long)arealimit)
     {
	 Rect	rect;
	 TiToRect(starttile,&rect);
         rect.r_xbot /= scale;
         rect.r_xtop /= scale;
         rect.r_ybot /= scale;
         rect.r_ytop /= scale;
	 GeoClip(&rect, arg->dCD_clip);
	 if (!GEO_RECTNULL(&rect)) {
	     (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
		 arg->dCD_cptr, arg->dCD_clientData);
	     (*(arg->dCD_errors))++;
	 }
	 
     }
forgetit:
     /* reset the tiles */
     starttile->ti_client = (ClientData)DRC_UNPROCESSED;
     STACKPUSH(starttile, DRCstack);
     while (!StackEmpty(DRCstack))
     {
	tile = (Tile *) STACKPOP(DRCstack);

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

     }
}

/*
 *-------------------------------------------------------------------------
 *
 * drcCheckCifMaxwidth --
 *
 *	Checks to see that at least one dimension of a region
 *	does not exceed some amount.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	May cause errors to be painted.
 *
 *-------------------------------------------------------------------------
 */

void
drcCheckCifMaxwidth(starttile,arg,cptr)
    Tile	*starttile;
    struct drcClientData *arg;
    DRCCookie	*cptr;

{
    int			edgelimit = cptr->drcc_dist;
    Rect		boundrect;
    TileTypeBitMask	*oktypes = &cptr->drcc_mask;
    Tile		*tile,*tp;
    int 		scale = drcCifStyle->cs_scaleFactor;
     
    arg->dCD_cptr = (DRCCookie *)cptr;
    if (DRCstack == (Stack *) NULL)
	DRCstack = StackNew(64);

    /* if bends are allowed, just check on a tile-by-tile basis that
        one dimension is the max.  This is pretty stupid, but it correctly
	calculates the trench width rule. dcs 12.06.89 */
    if (cptr->drcc_flags & DRC_BENDS)
    {
	 Rect	rect;
	 TiToRect(starttile,&rect);
	 if (rect.r_xtop-rect.r_xbot > edgelimit &&
	    rect.r_ytop-rect.r_ybot > edgelimit)
	 {
              rect.r_xbot /= scale;
              rect.r_xtop /= scale;
              rect.r_ybot /= scale;
              rect.r_ytop /= scale;
	      GeoClip(&rect, arg->dCD_clip);
	      if (!GEO_RECTNULL(&rect)) {
		  (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
			arg->dCD_cptr, arg->dCD_clientData);
		  (*(arg->dCD_errors))++;
	     }
	 }
	 return;
    }
    /* Mark this tile as pending and push it */
    PUSHTILE(starttile);
    TiToRect(starttile,&boundrect);

    while (!StackEmpty(DRCstack))
    {
	tile = (Tile *) STACKPOP(DRCstack);
	if (tile->ti_client != (ClientData)DRC_PENDING) continue;
	
	if (boundrect.r_xbot > LEFT(tile)) boundrect.r_xbot = LEFT(tile);
	if (boundrect.r_xtop < RIGHT(tile)) boundrect.r_xtop = RIGHT(tile);
	if (boundrect.r_ybot > BOTTOM(tile)) boundrect.r_ybot = BOTTOM(tile);
	if (boundrect.r_ytop < TOP(tile)) boundrect.r_ytop = TOP(tile);
	tile->ti_client = (ClientData)DRC_PROCESSED;

         if (boundrect.r_xtop - boundrect.r_xbot > edgelimit &&
             boundrect.r_ytop - boundrect.r_ybot > edgelimit) break;

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp)))	PUSHTILE(tp);

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp))) PUSHTILE(tp);

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp))) PUSHTILE(tp);

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (TTMaskHasType(oktypes, TiGetType(tp))) PUSHTILE(tp);
     }

     if (boundrect.r_xtop - boundrect.r_xbot > edgelimit &&
             boundrect.r_ytop - boundrect.r_ybot > edgelimit) 
     {
	 Rect	rect;
	 TiToRect(starttile,&rect);
	 {
              rect.r_xbot /= scale;
              rect.r_xtop /= scale;
              rect.r_ybot /= scale;
              rect.r_ytop /= scale;
	      GeoClip(&rect, arg->dCD_clip);
	      if (!GEO_RECTNULL(&rect)) {
		  (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
			arg->dCD_cptr, arg->dCD_clientData);
		  (*(arg->dCD_errors))++;
	      }
	 }
	 
     }
     /* reset the tiles */
     starttile->ti_client = (ClientData)DRC_UNPROCESSED;
     STACKPUSH(starttile, DRCstack);
     while (!StackEmpty(DRCstack))
     {
	tile = (Tile *) STACKPOP(DRCstack);

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

     }
}

