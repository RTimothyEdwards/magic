/*
 * ExtUnique.c --
 *
 * Circuit extraction.
 * Generation of unique names.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtUnique.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "utils/stack.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "utils/undo.h"

/*
 * ----------------------------------------------------------------------------
 *
 * extUniqueCell --
 *
 * For the cell 'def', look for the same label appearing in two or more
 * distinct nodes.  For each such label found:
 * 
 *	If option is 0, then generate unique names for all
 *	    but one of the nodes by appending a unique numeric
 *	    suffix to the offending labels.
 *	If option is 1, then generate unique names only if
 *	    the label ends in '#'.  Leave feedback for all other
 *	    names that don't end in '!'.
 *	If option is 2, then generate unique names as for
 *	    option 0 only if the label is not a port.
 *
 * Results:
 *	Returns the number of warnings generated.
 *
 * Side effects:
 *	May modify the label list of def, and mark def as CDMODIFIED.
 *	May also leave feedback.
 *
 * ----------------------------------------------------------------------------
 */

int
extUniqueCell(def, option)
    CellDef *def;
    int option;
{
    LabRegion *lregList, *lastreg, processedLabel;
    LabRegion *lp;
    LabelList *ll;
    HashEntry *he;
    HashTable labelHash;
    Label *lab;
    char *text;
    int nwarn;

    nwarn = 0;
    HashInit(&labelHash, 32, HT_STRINGKEYS);
    TxPrintf("Processing %s\n", def->cd_name);
    TxFlush();

    /* Build up a list of nodes and assign them to tiles */
    lregList = (LabRegion *) ExtFindRegions(def, &TiPlaneRect,
				&ExtCurStyle->exts_activeTypes,
				ExtCurStyle->exts_nodeConn,
				extUnInit, extHierLabFirst, (int (*)()) NULL);

    /* Assign the labels to their associated regions */
    ExtLabelRegions(def, ExtCurStyle->exts_nodeConn, &lregList, &TiPlaneRect);

    /*
     * First pass:
     * Place all node labels in the cell in the hash table.
     */
    for (lab = def->cd_labels; lab; lab = lab->lab_next)
	if (extLabType(lab->lab_text, LABTYPE_NAME))
	    (void) HashFind(&labelHash, lab->lab_text);

    /* Fix up labels as necessary */
    for (lp = lregList; lp; lp = lp->lreg_next)
    {
	for (ll = lp->lreg_labels; ll; ll = ll->ll_next)
	{
	    /*
	     * We might have set ll->ll_label to NULL if we changed it
	     * to make it unique.  Also ignore if the label is not a
	     * node label type (e.g, it is an attribute).
	     */
	    if (ll->ll_label == (Label *) NULL)
		continue;
	    text = ll->ll_label->lab_text;
	    if (!extLabType(text, LABTYPE_NAME))
		continue;

	    /*
	     * See if this label has been seen before in this cell.
	     * If not, remember the current LabRegion as the first
	     * one seen with this label.  If it has been seen, but
	     * with this same region, or it has already been made
	     * unique (lastreg == &processedLabel), skip it.
	     */
	    he = HashFind(&labelHash, text);
	    lastreg = (LabRegion *) HashGetValue(he);
	    if (lastreg == (LabRegion *) NULL)
	    {
		HashSetValue(he, (ClientData) lp);
		continue;
	    }
	    if (lastreg != lp && lastreg != &processedLabel)
	    {
		nwarn += extMakeUnique(def, ll, lp, lregList,
				&labelHash, option);
		HashSetValue(he, (ClientData) &processedLabel);
	    }
	}
    }

    HashKill(&labelHash);
    ExtFreeLabRegions((LabRegion *) lregList);
    ExtResetTiles(def, extUnInit);
    if (nwarn)
	TxError("%s: %d warnings\n", def->cd_name, nwarn);
    return (nwarn);
}

int
extMakeUnique(def, ll, lreg, lregList, labelHash, option)
    CellDef *def;
    LabelList *ll;
    LabRegion *lreg, *lregList;
    HashTable *labelHash;
    int option;
{
    static char *badmesg =
    "Non-global label \"%s\" attached to more than one unconnected node: %s";
    char *cpend, *text, name[1024], name2[1024], message[1024];
    LabRegion *lp2;
    LabelList *ll2;
    int nsuffix, nwarn;
    Label saveLab, *lab;
    Rect r;

    /*
     * Make a pass through all labels for all nodes.
     * Replace labels as appropriate.  This loop
     * sets ll_label pointers to NULL whenever it
     * changes a label to make it unique.
     */
    text = ll->ll_label->lab_text;
    if (option == 0)
	goto makeUnique;
    else if ((option == 2) && !(ll->ll_label->lab_flags & PORT_DIR_MASK))
	goto makeUnique;

    cpend = strchr(text, '\0');
    if (cpend > text) cpend--;
    if (*cpend == '#') goto makeUnique;
    if (*cpend == '!') return 0;
    if ((option == 2) && (ll->ll_label->lab_flags & PORT_DIR_MASK)) return 0;

    /* Generate a warning for each occurrence of this label */
    nwarn = 0;
    for (lp2 = lregList; lp2; lp2 = lp2->lreg_next)
    {
	for (ll2 = lp2->lreg_labels; ll2; ll2 = ll2->ll_next)
	{
	    if (ll2->ll_label && strcmp(ll2->ll_label->lab_text, text) == 0)
	    {
		nwarn++;
		r.r_ll = r.r_ur = ll2->ll_label->lab_rect.r_ll;
		GEO_EXPAND(&r, 1, &r);
		extMakeNodeNumPrint(name, lp2->lreg_pnum, lp2->lreg_ll);
		(void) sprintf(message, badmesg, text, name);
		DBWFeedbackAdd(&r, message, def, 1, STYLE_MEDIUMHIGHLIGHTS);
	    }
	}
    }
    return nwarn;

    /*
     * For each occurrence of this label in all nodes but lreg,
     * replace the label by one with a unique suffix.  If the
     * label is replaced, we mark it in the label list of the
     * node by NULLing-out the ll_label field of the LabelList
     * pointing to it.
     */
makeUnique:
    nsuffix = 0;
    (void) strcpy(name, text);
    for (lp2 = lregList; lp2; lp2 = lp2->lreg_next)
    {
	/* Skip lreg -- its labels will be unchanged */
	if (lp2 == lreg)
	    continue;

	lab = (Label *) NULL;
	for (ll2 = lp2->lreg_labels; ll2; ll2 = ll2->ll_next)
	{
	    if (ll2->ll_label == (Label *) NULL)
		continue;
	    if (strcmp(ll2->ll_label->lab_text, name) != 0)
		continue;

	    /*
	     * Keep looking for a name not already in this cell.
	     * This is a bit conservative, since names that might not have
	     * been attached to nodes were also added to the table in
	     * extUniqueCell() above, but we ensure that we don't generate
	     * a name that might conflict with an existing one (e.g, turning
	     * Phi into Phi1 when there's already a Phi1 in the cell).
	     * It's not necessary to add the final name to labelHash
	     * because nsuffix increases monotonically -- we can never
	     * generate a label identical to one previously generated.
	     */
	    for (;;)
	    {
		(void) sprintf(name2, "%s_uq%d", name, nsuffix);
		if (HashLookOnly(labelHash, name2) == NULL)
		    break;
		nsuffix++;
	    }

	    lab = ll2->ll_label;
	    saveLab = *lab;
	    DBEraseLabelsByContent(def, &lab->lab_rect, lab->lab_type, lab->lab_text);
	    (void) DBPutFontLabel(def, &saveLab.lab_rect,
		 	saveLab.lab_font, saveLab.lab_size, saveLab.lab_rotate,
			&saveLab.lab_offset, saveLab.lab_just, name2,
			saveLab.lab_type, saveLab.lab_flags);
	    ll2->ll_label = (Label *) NULL;
	}

	/* Bump the suffix if we replaced any labels */
	if (lab) nsuffix++;
    }

    return 0;
}
