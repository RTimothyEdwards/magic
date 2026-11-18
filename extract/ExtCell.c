/*
 * ExtCell.c --
 *
 * Circuit extraction.
 * Extract a single cell.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtCell.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
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

/* ------------------------ Data local to this file ------------------- */

/* Forward declarations */
int extOutputUsesFunc();

Plane* extCellFile();
void extHeader();
Plane *extUpToDateCell(CellDef *def, bool isTop);
void extOutputSortedUses(CellDef *def, FILE *f);

/*
 * ----------------------------------------------------------------------------
 *
 * ExtCell --
 *
 * Extract the cell 'def', plus all its interactions with its subcells.
 * Place the result in the file named 'outName'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates the file 'outName'.ext and writes to it.
 *	May leave feedback information where errors were encountered.
 *	Upon return, extNumErrors contains the number of (likely serious)
 *	errors encountered while extracting 'def', and extNumWarnings
 *	contains the number of warnings.
 *
 * ----------------------------------------------------------------------------
 */

/* When TRUE, extCellFile() only reproduces the side effects that
 * extracting a cell leaves on its CellDef (unique labels, prepared
 * substrate plane, and labels marked as unusable for merges) and skips
 * the hierarchical extraction, which is the expensive part.  See
 * extReplayCellFile(), ExtReplayCell() and extract/ExtParallel.c.
 */
static bool extReplayOnly = FALSE;

Plane *
ExtCell(def, outName, isTop)
    CellDef *def;	/* Cell being extracted */
    char *outName;	/* Name of output file; if NULL, derive from def name */
    bool isTop;		/* If TRUE, cell is the top level cell */
{
    char *filename;
    FILE *f = NULL;
    Plane *savePlane;
    bool noextract;

    /* Counts are for this cell only, even if it is not extracted */
    extNumErrors = extNumWarnings = 0;

    /* If marked abstract, then don't extract the cell */
    DBPropGet(def, "noextract", &noextract);
    if (noextract) return extPrepSubstrate(def);

    /* Incremental extraction:  If the cell is marked for no extraction,
     * then its .ext file is up to date.  Only rebuild the state that
     * extracting its parents needs (see extUpToDateCell()).
     */
    if (def->cd_flags & CDNOEXTRACT)
	return extUpToDateCell(def, isTop);

    f = ExtFileOpen(def, outName, "w", &filename);

    TxPrintf("Extracting %s into %s:\n", def->cd_name, filename);

    if (f == NULL)
    {
#ifdef MAGIC_WRAPPER
	TxError("Cannot open output file.\n");
#else
	TxError("Cannot open output file: ");
	perror(filename);
#endif
	return NULL;
    }

    savePlane = extCellFile(def, f, isTop);
    if (f != NULL) fclose(f);

    if (extNumErrors > 0 || extNumWarnings > 0)
    {
	TxPrintf("%s:", def->cd_name);
	if (extNumErrors > 0)
	    TxPrintf(" %d error%s",
		extNumErrors, extNumErrors != 1 ? "s" : "");
	if (extNumWarnings > 0)
	    TxPrintf(" %d warning%s",
		extNumWarnings, extNumWarnings != 1 ? "s" : "");
	TxPrintf("\n");
    }
    return savePlane;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extReplayCellFile --
 *
 * Reproduce the state that extracting 'def' leaves on it, without doing
 * the hierarchical part of the extraction.  Extracting a parent cell
 * reads state left on its children by their own extraction:
 *
 *   - labels made unique ("extract do unique");
 *   - labels that extBasic() marks (lab_port = INFINITY) as not usable
 *     for merges, because they are not written to the child's .ext file
 *     as aliases ("extract no aliases");
 *   - the prepared substrate plane.
 *
 * Everything the replay would print, its feedback entries, and its error
 * and warning counts are discarded:  they were already reported when the
 * cell was extracted.
 *
 * Results:
 *	The saved substrate plane, exactly as ExtCell() would return it.
 *
 * Side effects:
 *	Same changes to 'def' as ExtCell(), apart from writing a .ext file.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
extReplayCellFile(
    CellDef *def,	/* Cell whose extraction side effects are replayed */
    bool isTop)		/* TRUE if the cell is the top level cell */
{
    static FILE *nullf = NULL;
    FILE *saveRedirect = TxRedirectFile;
    int fbCount = DBWFeedbackCount;
    Plane *savePlane;

    if (nullf == NULL)
    {
#ifdef _WIN32
	nullf = fopen("NUL", "w");
#else
	nullf = fopen("/dev/null", "w");
#endif
	if (nullf == NULL) nullf = tmpfile();
	if (nullf == NULL)
	{
	    TxError("Cannot open a null output file;  names in cells that"
		    " use %s may be wrong.\n", def->cd_name);
	    return extPrepSubstrate(def);
	}
    }

    TxRedirectFile = nullf;
    extReplayOnly = TRUE;
    savePlane = extCellFile(def, nullf, isTop);
    extReplayOnly = FALSE;
    TxRedirectFile = saveRedirect;

    DBWFeedbackTruncate(fbCount);
    extNumErrors = extNumWarnings = 0;
    return savePlane;
}

/* TRUE if extracting a cell can leave label state that its parents
 * read:  unique names ("do unique") or labels marked as unusable for
 * merges ("no aliases").  See extReplayCellFile().
 */
#define extNeedLabelState() \
	(!(ExtOptions & EXT_DOALIASES) || (ExtOptions & EXT_DOUNIQUE))

/* Remove marks left on 'def' by an earlier extraction with "no aliases",
 * as extCellFile() does before extracting the cell.
 */
void
extClearLabelMarks(
    CellDef *def)
{
    Label *lab;

    for (lab = def->cd_labels; lab; lab = lab->lab_next)
	if (lab->lab_port == INFINITY)
	    lab->lab_port = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extUpToDateCell --
 *
 * Called instead of extracting a cell whose .ext file is up to date
 * (incremental extraction).  Its parents are about to be extracted and
 * read the label state that extracting it leaves behind (see
 * extReplayCellFile()).  That state is only needed if labels can be made
 * unique or marked, so in the default case only the substrate plane is
 * prepared.  Without this, a parent extracted in a new session would
 * name nodes in this cell by labels that its .ext file does not define,
 * and the netlist would lose those connections.
 *
 * Results:
 *	The saved substrate plane, exactly as ExtCell() would return it.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
extUpToDateCell(
    CellDef *def,
    bool isTop)
{
    if (extNeedLabelState())
	return extReplayCellFile(def, isTop);
    extClearLabelMarks(def);
    return extPrepSubstrate(def);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtReplayCell --
 *
 * Reproduce the state that ExtCell() leaves on 'def', without writing a
 * .ext file or doing the hierarchical part of the extraction.  A
 * parallel extraction worker that does not own a child cell must replay
 * that state before extracting the child's parents.
 *
 * Results:
 *	The saved substrate plane, exactly as ExtCell() would return it.
 *
 * Side effects:
 *	Same changes to 'def' as ExtCell(), apart from writing a .ext file.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
ExtReplayCell(
    CellDef *def,	/* Cell whose extraction side effects are replayed */
    bool isTop)		/* TRUE if the cell is the top level cell */
{
    bool noextract;

    DBPropGet(def, "noextract", &noextract);
    if (noextract) return extPrepSubstrate(def);
    if (def->cd_flags & CDNOEXTRACT)
	return extUpToDateCell(def, isTop);
    return extReplayCellFile(def, isTop);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtReplayIsCheap --
 *
 * Results:
 *	TRUE if, with the current options, ExtReplayCell() does not need
 *	extBasic():  it then only makes labels unique and prepares the
 *	substrate plane, which is much cheaper than extracting the cell.
 *
 * ----------------------------------------------------------------------------
 */

bool
ExtReplayIsCheap(void)
{
    return (ExtOptions & EXT_DOALIASES) ? TRUE : FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtFileOpen --
 *
 * Open the .ext file corresponding to a .mag file.
 * If def->cd_file is non-NULL, the .ext file is just def->cd_file with
 * the trailing .mag replaced by .ext.  Otherwise, the .ext file is just
 * def->cd_name followed by .ext.
 *
 * Results:
 *	Return a pointer to an open FILE, or NULL if the .ext
 *	file could not be opened in the specified mode.
 *
 * Side effects:
 *	Opens a file.
 *
 * ----------------------------------------------------------------------------
 */

FILE *
ExtFileOpen(def, file, mode, prealfile)
    CellDef *def;	/* Cell whose .ext file is to be written */
    char *file;		/* If non-NULL, open 'name'.ext; otherwise,
			 * derive filename from 'def' as described
			 * above.
			 */
    char *mode;		/* Either "r" or "w", the mode in which the .ext
			 * file is to be opened.
			 */
    char **prealfile;	/* If this is non-NULL, it gets set to point to
			 * a string holding the name of the .ext file.
			 */
{
    char namebuf[512], *name, *endp, *ends;
    int len;
    FILE *rfile, *testf;

    if (file) name = file;
    else if (ExtLocalPath != NULL)
    {
	if (!strcmp(ExtLocalPath, "."))
	    name = def->cd_name;	/* Save locally */
	else
	{
	    struct stat st = {0};

	    /* Test presence of the directory */
	    if (stat(ExtLocalPath, &st) == -1) {
		TxError("Path \"%s\" does not exist;  attempting to create it.\n",
			ExtLocalPath);
		mkdir(ExtLocalPath, 0755);
		if (stat(ExtLocalPath, &st) == -1) {
		    TxError("Path \"%s\" does not exist;  saving locally.\n",
				ExtLocalPath);
		    name = def->cd_name;	/* Save locally */
		}
		else
		{
		    /* Save locally in specified path, which was just created */
		    name = namebuf;
		    sprintf(namebuf, "%s/%s", ExtLocalPath, def->cd_name);
		}
	    }
	    else
	    {
		/* Save locally in specified path, which is assumed to exist */
		name = namebuf;
		sprintf(namebuf, "%s/%s", ExtLocalPath, def->cd_name);
	    }
	}
    }
    else if (def->cd_file)
    {
	name = def->cd_file;
	ends = strrchr(def->cd_file, '/');
	if (ends == NULL) ends = def->cd_file;
	if ((endp = strrchr(ends + 1, '.')))
	{
	    name = namebuf;
	    len = endp - def->cd_file;
	    if (len > sizeof namebuf - 1) len = sizeof namebuf - 1;
	    (void) strncpy(namebuf, def->cd_file, len);
	    namebuf[len] = '\0';
	}
    }
    else name = def->cd_name;

    /* Try once as-is, and if this fails, try stripping any leading	*/
    /* path information in case cell is in a read-only directory.	*/

    if ((rfile = PaOpen(name, mode, ".ext", Path, CellLibPath, prealfile)) != NULL)
	return rfile;

    if (!strcmp(mode, "r")) return NULL;	/* Not even readable */

    /* Try writing to the cwd IF there is no .mag file by the	*/
    /* same name in the cwd that would conflict.		*/

    name = strrchr(def->cd_name, '/');
    if (name != NULL)
	name++;
    else
	name = def->cd_name;

    if (def->cd_file)
    {
	ends = strrchr(def->cd_file, '/');
	if (ends != NULL)
	{
	    testf = PaOpen(ends + 1, "r", ".mag", ".", ".", NULL);
	    if (testf)
	    {
		fclose(testf);
		return NULL;
	    }
	}
    }
    return (PaOpen(name, mode, ".ext", ".", ".", prealfile));
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPrepSubstrate ---
 *
 * Prepare a replacement plane for the plane representing the substrate, as
 * defined in ExtCurStyle->exts_globSubstratePlane.  The target CellDef is
 * searched for types that shield (i.e., isolate) a section of the layout
 * from the global substrate.  The tile type that represents the substrate
 * is painted into the isolated regions.
 *
 * The purpose of this method is to deal with the common methodology in
 * which the substrate is not represented by any tile type, because no mask
 * is defined for the substrate.  Typically, an entire cell such as a digital
 * standard cell may be placed on the default substrate or in a deep nwell
 * region.  It is therefore necessary to be able to detect what is underneath
 * a cell on the plane representing the substrate to determine if the area is
 * the default substrate or an isolated region.  If an isolated region, it
 * must be painted with a tile type so that the extraction code can tag the
 * tiles with an ExtRegion and assign it a node.  This code creates the substrate
 * paint in the isolated regions for the duration of the extration, then
 * reverts back to the original plane afterward.
 *
 * Results:
 *	Returns a Plane structure that is the original substrate plane from
 *	CellDef "def", with isolated substrate regions filled with the
 *	substrate tile type.  If there are no isolated substrate regions,
 *	or if a substrate plane or substrate type is not defined by the
 *	technology, then the routine returns NULL.
 *
 * Side effects:
 *	All modifications are limited to the returned plane structure.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
extPrepSubstrate(def)
    CellDef *def;
{
    SearchContext scx;
    CellUse dummy;
    TileType subType;
    TileTypeBitMask subMask, notSubMask;
    Plane *subPlane, *savePlane;
    int pNum;

    /* Determine if substrate copying is required. */

    if (ExtCurStyle->exts_globSubstratePlane == -1) return NULL;

    /* Find a type to use for the substrate, and the mask of all types      */
    /* in the same plane as the substrate that are not connected to the     */
    /* substrate.  If there is not a simple type representing the substrate */
    /* then do not attempt to resolve substrate regions.                    */

    if ((subType = ExtCurStyle->exts_globSubstrateDefaultType) == -1) return NULL;

    TTMaskZero(&subMask);
    TTMaskSetMask(&subMask, &ExtCurStyle->exts_globSubstrateTypes);
    TTMaskCom2(&notSubMask, &subMask);
    TTMaskAndMask(&notSubMask, &DBPlaneTypes[ExtCurStyle->exts_globSubstratePlane]);

    /* Generate the full flattened substrate into a new plane structure	*/
    /* (called subPlane).  This adds layer geometry for the substrate	*/
    /* in the typical case where the substrate may be space (implicitly	*/
    /* defined substrate).                                  		*/

    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area = def->cd_bbox;
    scx.scx_use = &dummy;
    dummy.cu_def = def;
    dummy.cu_id = NULL;

    subPlane = DBCellGenerateSubstrate(&scx, subType, &notSubMask,
		&ExtCurStyle->exts_globSubstrateShieldTypes, def);
    if (subPlane != NULL)
    {
	pNum = ExtCurStyle->exts_globSubstratePlane;
	savePlane = def->cd_planes[pNum];
	def->cd_planes[pNum] = subPlane;
	return savePlane;
    }
    else
        return NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extResPrepSubstrate ---
 *
 * This works similarly to extPrepSubstrate above, but is used for the
 * "extresist" command, where the method is to make sure that the whole
 * substrate area of the cell has non-space tiles, so that these can be
 * used to estimate the resistance of the substrate from point to point,
 * and to eliminate isolated substrate regions, since those represent an
 * idealized cutoff of resistance that "extresist" is supposed to be
 * replacing with an accurate resitance network.
 *
 * Results:
 *	Returns a Plane structure that is the original substrate plane from
 *	CellDef "def", with the entire substrate region filled with the
 *	substrate tile type.  If a substrate plane or substrate type is not
 *	defined by the technology, then the routine returns NULL.
 *
 * Side effects:
 *	All modifications are limited to the returned plane structure.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
extResPrepSubstrate(def)
    CellDef *def;
{
    SearchContext scx;
    CellUse dummy;
    TileType subType;
    TileTypeBitMask subMask, notSubMask;
    Plane *subPlane, *savePlane;
    int pNum;

    /* Determine if substrate copying is required. */

    if (ExtCurStyle->exts_globSubstratePlane == -1) return NULL;

    /* Find a type to use for the substrate, and the mask of all types      */
    /* in the same plane as the substrate that are not connected to the     */
    /* substrate.  If there is not a simple type representing the substrate */
    /* then do not attempt to resolve substrate regions.                    */

    if ((subType = ExtCurStyle->exts_globSubstrateDefaultType) == -1) return NULL;

    TTMaskZero(&subMask);
    TTMaskSetMask(&subMask, &ExtCurStyle->exts_globSubstrateTypes);
    TTMaskCom2(&notSubMask, &subMask);
    TTMaskAndMask(&notSubMask, &DBPlaneTypes[ExtCurStyle->exts_globSubstratePlane]);

    /* Generate the full flattened substrate into a new plane structure	*/
    /* (called subPlane).  This adds layer geometry for the substrate	*/
    /* in the typical case where the substrate may be space (implicitly	*/
    /* defined substrate).                                  		*/

    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area = def->cd_bbox;
    scx.scx_use = &dummy;
    dummy.cu_def = def;
    dummy.cu_id = NULL;

    subPlane = DBCellGenerateSimpleSubstrate(&scx, subType, &notSubMask, def);
    if (subPlane != NULL)
    {
	pNum = ExtCurStyle->exts_globSubstratePlane;
	savePlane = def->cd_planes[pNum];
	def->cd_planes[pNum] = subPlane;
	return savePlane;
    }
    else
        return NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtRevertSubstrate ---
 *
 * This routine swaps the substrate plane of CellDef "def" with the plane
 * structure provided in the argument "savePlane".  It should be called at
 * the end of extraction.  "savePlane" should be the pointer to the substrate
 * plane of "def" before it was swapped out for the modified plane created by
 * the routine "extPrepSubstrate", above.  The calling routine is responsible
 * for knowing if extPrepSubstrate returned NULL in which case there is
 * nothing to revert.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	The CellDef "def" has its substrate plane swapped out for "savePlane",
 *	and the original substrate plane and its contents are freed.
 * ----------------------------------------------------------------------------
 */


void
ExtRevertSubstrate(def, savePlane)
    CellDef *def;
    Plane *savePlane;
{
    int pNum;
    Plane *subPlane;

    pNum = ExtCurStyle->exts_globSubstratePlane;
    subPlane = def->cd_planes[pNum];
    def->cd_planes[pNum] = savePlane;
    DBFreePaintPlane(subPlane);
    TiFreePlane(subPlane);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCellFile --
 *
 * Internal interface for extracting a single cell.
 * Extracts it to the open FILE 'f'.  Doesn't print
 * any messages.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May leave feedback information where errors were encountered.
 *	Upon return, extNumErrors has been incremented by the number of
 *	fatal errors encountered while extracting 'def', and extNumWarnings
 *	by the number of warnings.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
extCellFile(def, f, isTop)
    CellDef *def;	/* Def to be extracted */
    FILE *f;		/* Output to this file */
    bool isTop;		/* TRUE if the cell is the top level cell */
{
    NodeRegion *reg;
    Plane *saveSub;
    Label *lab;

    UndoDisable();

    /* Warn about inconsistent port names and indexes, which can cause
     * ports to be silently dropped from the subcircuit in the netlist.
     * This must be done before "extract do unique" assigns new names
     * and indexes.
     */
    DBCheckPortIndices(def, TRUE);

    /* If "extract do unique" was specified, then make labels in the
     * cell unique.
     */

    if (ExtOptions & EXT_DOUNIQUE)
    {
	if (ExtOptions & EXT_DOUNIQNOTOPPORTS)
	{
	    if (isTop)
		extUniqueCell(def, EXT_UNIQ_TEMP_NOPORTS);
	    else
		extUniqueCell(def, EXT_UNIQ_TEMP);
	}
	else
	    extUniqueCell(def, EXT_UNIQ_TEMP);
    }

    /* Prep any isolated substrate areas */
    if (ExtOptions & EXT_DOEXTRESIST)
	saveSub = extResPrepSubstrate(def);
    else
	saveSub = extPrepSubstrate(def);

    /* Remove any label markers that were made by a previous extraction */
    for (lab = def->cd_labels; lab; lab = lab->lab_next)
	if (lab->lab_port == INFINITY)
	    lab->lab_port = 0;

    /* Output the header: timestamp, technology, calls on cell uses */
    if (!SigInterruptPending) extHeader(def, f);

    /* Extract the mask information in this cell */
    reg = (NodeRegion *) NULL;
    /* When only replaying, extBasic() is needed only for the labels it
     * marks as unusable for merges, which happens only without aliases.
     */
    if (!SigInterruptPending &&
		!(extReplayOnly && (ExtOptions & EXT_DOALIASES)))
	reg = extBasic(def, f);

    /* Do hierarchical extraction (not needed when only replaying) */
    extParentUse->cu_def = def;
    if (!SigInterruptPending && !extReplayOnly) extSubtree(extParentUse, reg, f);
    if (!SigInterruptPending && !extReplayOnly) extArray(extParentUse, f);

    /* Clean up from basic extraction */
    if (reg) ExtFreeLabRegions((LabRegion *) reg);
    ExtResetTiles(def, CLIENTDEFAULT);

    /* Final pass: extract length information if desired */
    if (!SigInterruptPending && !extReplayOnly && isTop &&
		(ExtOptions & EXT_DOLENGTH))
	extLength(extParentUse, f);

    UndoEnable();
    return saveSub;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHeader --
 *
 * Output header information to the .ext file for a cell.
 * This information consists of:
 *
 *	timestamp
 *	extractor version number
 *	technology
 *	scale factors for resistance, capacitance, and lambda
 *	calls on all subcells used by this cell (see extOutputUsesFunc)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to (FILE *) 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
extHeader(def, f)
    CellDef *def;	/* Cell being extracted */
    FILE *f;		/* Write to this file */
{
    int n;
    bool propfound;
    char *propvalue;

    ASSERT(DBTechName != NULL, "extHeader");

    /* Output a timestamp (should be first) */
    fprintf(f, "timestamp %d\n", def->cd_timestamp);

    /* Output our version number */
    fprintf(f, "version %s\n", MagicVersion);

    /* Output the technology */
    fprintf(f, "tech %s\n", DBTechName);

    /* Output the extract style name */
    fprintf(f, "style %s\n", ExtCurStyle->exts_name);

    /*
     * Output scaling factors: R C D
     *		R = amount to multiply all resistances in the file by
     *		C = amount to multiply all capacitances by
     *		D = amount to multiply all linear distances by (areas
     *		    should be multiplied by D**2).
     */
    fprintf(f, "scale %d %d %g\n",
		ExtCurStyle->exts_resistScale,
		ExtCurStyle->exts_capScale,
		ExtCurStyle->exts_unitsPerLambda);

    /* Output the sheet resistivity classes */
    fprintf(f, "resistclasses");
    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	fprintf(f, " %d", ExtCurStyle->exts_resistByResistClass[n]);
    fprintf(f, "\n");

    /* Output any parameters defined for this cell that	*/
    /* are to be passed to instances of the cell	*/
    /* (created by defining property "parameter")	*/

    propvalue = DBPropGetString(def, "parameter", &propfound);
    if (propfound)
    {
	// Use device parameter table to store the cell def parameters,
	// but preface name with ":" to avoid any conflict with device
	// names.
	fprintf(f, "parameters :%s %s\n", def->cd_name, propvalue);
    }

    /* Output all calls on subcells, in a fixed order (see extOutputSortedUses) */
    extOutputSortedUses(def, f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputSortedUses --
 *
 * Write the "use" lines for every subcell of 'def', sorted by instance
 * name (then cell name and transform, in case instance names repeat).
 * The order in which DBCellEnum() visits uses depends on whether the
 * bins of the cell plane have been built yet, which happens on the
 * first area search, so it can differ between two extractions of the
 * same layout in one session.  Sorting makes the .ext file depend only
 * on the layout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to 'f'.
 *
 * ----------------------------------------------------------------------------
 */

typedef struct
{
    CellUse **su_uses;
    int su_count;
    int su_size;
} extSortedUses;

int
extCollectUseFunc(
    CellUse *cu,
    ClientData cdata)
{
    extSortedUses *su = (extSortedUses *) cdata;

    if (su->su_count == su->su_size)
    {
	CellUse **newUses;

	su->su_size = (su->su_size == 0) ? 64 : 2 * su->su_size;
	newUses = (CellUse **) mallocMagic(su->su_size * sizeof(CellUse *));
	if (su->su_count > 0)
	{
	    memcpy(newUses, su->su_uses, su->su_count * sizeof(CellUse *));
	    freeMagic(su->su_uses);
	}
	su->su_uses = newUses;
    }
    su->su_uses[su->su_count++] = cu;
    return 0;
}

int
extUseCompare(
    const void *a,
    const void *b)
{
    const CellUse *u1 = *(const CellUse * const *) a;
    const CellUse *u2 = *(const CellUse * const *) b;
    const Transform *t1 = &u1->cu_transform, *t2 = &u2->cu_transform;
    int cmp;

    cmp = strcmp(u1->cu_id ? u1->cu_id : "", u2->cu_id ? u2->cu_id : "");
    if (cmp != 0) return cmp;
    cmp = strcmp(u1->cu_def->cd_name, u2->cu_def->cd_name);
    if (cmp != 0) return cmp;
    if (t1->t_c != t2->t_c) return (t1->t_c < t2->t_c) ? -1 : 1;
    if (t1->t_f != t2->t_f) return (t1->t_f < t2->t_f) ? -1 : 1;
    if (t1->t_a != t2->t_a) return (t1->t_a < t2->t_a) ? -1 : 1;
    if (t1->t_b != t2->t_b) return (t1->t_b < t2->t_b) ? -1 : 1;
    if (t1->t_d != t2->t_d) return (t1->t_d < t2->t_d) ? -1 : 1;
    if (t1->t_e != t2->t_e) return (t1->t_e < t2->t_e) ? -1 : 1;
    return 0;
}

void
extOutputSortedUses(
    CellDef *def,
    FILE *f)
{
    extSortedUses su;
    int i;

    su.su_uses = NULL;
    su.su_count = su.su_size = 0;
    (void) DBCellEnum(def, extCollectUseFunc, (ClientData) &su);
    if (su.su_count == 0) return;

    qsort(su.su_uses, su.su_count, sizeof(CellUse *), extUseCompare);
    for (i = 0; i < su.su_count; i++)
	(void) extOutputUsesFunc(su.su_uses[i], f);
    freeMagic(su.su_uses);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputUsesFunc --
 *
 * Filter function, called via DBCellEnum, that outputs all the
 * cell uses contained in the parent's cell tile planes.
 *
 * Results:
 *	Always returns 0, for DBCellEnum to keep going.
 *
 * Side effects:
 *	Writes a line for each use encountered to 'outf'.
 *	The line is of the following form:
 *
 *		use defname useid Ta ... Tf
 *
 *	where 'defname' is the name of the cell def referenced (cd_name),
 *	'useid' is its use identifier (cu_id), and Ta ... Tf are the six
 *	components of the transform from coordinates of this use up to
 *	its parent.  If the cell is an array, the use id may be followed by:
 *
 *		[xlo,xhi,xsep][ylo,yhi,ysep]
 *
 *	The indices are xlo through xhi inclusive, or ylo through yhi
 *	inclusive.  The separation between adjacent elements is xsep
 *	or ysep; this is used in computing the transform for a particular
 *	array element.  If arraying is not present in a given direction,
 *	the low and high indices are equal and the separation is ignored.
 *
 * ----------------------------------------------------------------------------
 */

int
extOutputUsesFunc(cu, outf)
    CellUse *cu;
    FILE *outf;
{
    Transform *t = &cu->cu_transform;

    if (cu->cu_def->cd_flags & CDDONTUSE) return 0;

    fprintf(outf, "use %s %s", cu->cu_def->cd_name, cu->cu_id);
    if (cu->cu_xlo != cu->cu_xhi || cu->cu_ylo != cu->cu_yhi)
    {
	fprintf(outf, "[%d:%d:%d]",
			cu->cu_xlo, cu->cu_xhi, cu->cu_xsep);
	fprintf(outf, "[%d:%d:%d]",
			cu->cu_ylo, cu->cu_yhi, cu->cu_ysep);
    }

    /* Output transform to parent */
    fprintf(outf, " %d %d %d %d %d %d\n",
			t->t_a, t->t_b, t->t_c, t->t_d, t->t_e, t->t_f);

    return (0);
}
