/*
 * PlowRandom.c --
 *
 * Plowing.
 * Random testing: generate a whole bunch of random plows
 * and make sure each one produces a design-rule-correct
 * result that has the same extracted circuit (minus the
 * parasitics) as the initial cell.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1985, 1990 Regents of the University of California. * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowRandom.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#ifdef	SYSV
#include <fcntl.h>
#endif

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "plow/plowInt.h"
#include "textio/textio.h"
#include "utils/undo.h"
#include "utils/signals.h"
#include "extract/extract.h"

/* Imports from PlowMain.c */
extern CellDef *plowYankDef;

/* Forward declarations */
int plowFindFirstError();
void plowGenRect();


/*
 * ----------------------------------------------------------------------------
 *
 * PlowRandomTest --
 *
 * Random testing: generate a whole bunch of random plows
 * and make sure each one produces a design-rule-correct
 * result that has the same extracted circuit (minus the
 * parasitics) as the initial cell.
 *
 * Output the coordinates and direction of each plow as
 * we go, flagging it as one producing a DRC violation
 * or a connectivity violation as we go.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes a whole bunch of information via TxPrintf(),
 *	as described above.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowRandomTest(def)
    CellDef *def;
{
#ifdef	notdef
    static char *tempgood = "/tmp/PlowGoodaXXXXX";
    static char *temptemp = "/tmp/PlowTempaXXXXX";
    char goodName[64], tempName[64], goodExt[64], tempExt[64];
    char command[256];
#endif	/* notdef */
    static int dirs[] = { GEO_NORTH, GEO_SOUTH, GEO_EAST, GEO_WEST };
    static char *dirnames[] = { "up", "down", "right", "left" };
    Rect plowRect;
    int dir, plowDir;

#ifdef	notdef
    strcpy(goodName, tempgood);
    strcpy(tempName, temptemp);
    mkstemp(goodName);
    mkstemp(tempName);
    sprintf(goodExt, "%s.ext", goodName);
    sprintf(tempExt, "%s.ext", tempName);

    /* Generate "good" extracted file */
    ExtCell(def, goodName, FALSE);
    (void) sprintf(command, "sedplow %s", goodExt);
    system(command);
#endif	/* notdef */

    /* Repeatedly try to break plowing */
    while (!SigInterruptPending)
    {
	/* Generate a random plow direction and rectangle */
	dir = plowGenRandom(0, 3);
	plowDir = dirs[dir];
	plowGenRect(&def->cd_bbox, &plowRect);
	(void) Plow(def, &plowRect, DBAllTypeBits, plowDir);
	TxPrintf("%s %d %d %d %d\n", dirnames[dir],
			plowRect.r_xbot, plowRect.r_ybot,
			plowRect.r_xtop, plowRect.r_ytop);
	TxFlush();

	/* Finish the design-rule check and count any errors */
	DRCCatchUp();
	if (DBSrPaintArea((Tile *) NULL, def->cd_planes[PL_DRC_ERROR],
		    &def->cd_bbox, &DBAllButSpaceBits,
		    plowFindFirstError, (ClientData) NULL))
	{
	    TxPrintf("%s %d %d %d %d: DRC error\n", dirnames[dir],
			plowRect.r_xbot, plowRect.r_ybot,
			plowRect.r_xtop, plowRect.r_ytop);
	    TxFlush();
	}

	/* Turn off the modified bit so we get the same timestamp */
	def->cd_flags &= ~CDMODIFIED;

#ifdef	notdef
	/* Extract to the temp file */
	ExtCell(def, tempName, FALSE);
	(void) sprintf(command, "sedplow %s", tempExt);
	system(command);

	/* Check for any differences */
	if (plowFileDiff(goodExt, tempExt))
	{
	    TxPrintf("%s %d %d %d %d: EXT error\n", dirnames[dir],
			plowRect.r_xbot, plowRect.r_ybot,
			plowRect.r_xtop, plowRect.r_ytop);
	    TxFlush();
	}
#endif	/* notdef */

	/* Make sure there's always something to undo */
	DBPutLabel(def, &def->cd_bbox, -1, "dummylabel", TT_SPACE, 0);

	/* Undo */
	UndoBackward(1);
    }

#ifdef	notdef
    (void) unlink(goodExt);
    (void) unlink(tempExt);
#endif	/* notdef */
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowFindFirstError --
 *
 * Filter procedure called via DBSrPaintArea by PlowRandomTest()
 * above, on the PL_DRC_ERROR plane, looking for non-space tiles.
 *
 * Results:
 *	Returns 1 always, to force DBSrPaintArea to abort and
 *	return 1.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowFindFirstError()
{
    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowGenRect --
 *
 * Generate a random plow rectangle.
 * This rectangle is guaranteed to lie within the bounding box of
 * the cell.  The four coordinates are chosen randomly from a uniform
 * distribution, and then the rectangle is flipped to insure that its
 * upper-right is above and to the right of its lower-left.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in the rectangle pointed to by 'r'.
 *
 * ----------------------------------------------------------------------------
 */

void
plowGenRect(bbox, r)
    Rect *bbox;	/* Bounding box of the cell being plowed */
    Rect *r;		/* Fill in this rectangle */
{
    int temp;

    r->r_xbot = plowGenRandom(bbox->r_xbot, bbox->r_xtop);
    r->r_xtop = plowGenRandom(bbox->r_xbot, bbox->r_xtop);
    r->r_ybot = plowGenRandom(bbox->r_ybot, bbox->r_ytop);
    r->r_ytop = plowGenRandom(bbox->r_ybot, bbox->r_ytop);
    if (r->r_xbot == r->r_xtop) r->r_xtop = r->r_xbot + 1;
    if (r->r_ybot == r->r_ytop) r->r_ytop = r->r_ybot + 1;
    if (r->r_xbot > r->r_xtop)
    {
	temp = r->r_xtop;
	r->r_xtop = r->r_xbot;
	r->r_xbot = temp;
    }
    if (r->r_ybot > r->r_ytop)
    {
	temp = r->r_ytop;
	r->r_ytop = r->r_ybot;
	r->r_ybot = temp;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowGenRandom --
 *
 * Generate a random integer chosen from the integers lo, lo+1, ..., hi,
 * with each integer having equal probability.
 *
 * Results:
 *	Returns the integer described above.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowGenRandom(lo, hi)
    int lo, hi;		/* Inclusive bounds for the integer we'll generate */
{
    int range = hi - lo + 1;
#ifdef	SYSV
    int r = rand();
#else
    int r = random();
#endif	/* SYSV */

    return ((r % range) + lo);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowFileDiff --
 *
 * Compare two files for equality.
 *
 * Results:
 *	Returns TRUE if they are identical and the same length,
 *	FALSE if they differ.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowFileDiff(file1, file2)
    char *file1;
    char *file2;
{
    char b1[BUFSIZ], b2[BUFSIZ];
    int f1, f2;
    int n1, n2;
    bool ret = FALSE;

    if ((f1 = open(file1, O_RDONLY, 0)) < 0) goto done;
    if ((f2 = open(file2, O_RDONLY, 0)) < 0) goto done;

    while ((n1 = read(f1, b1, BUFSIZ)) > 0)
    {
	n2 = read(f2, b2, BUFSIZ);
	if (n1 != n2 || bcmp(b1, b2, n1) != 0)
	    goto done;
    }
    ret = TRUE;

done:
    (void) close(f1);
    (void) close(f2);
    return (ret);
}
