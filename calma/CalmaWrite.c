/*
 * CalmaWrite.c --
 *
 * Output of Calma GDS-II stream format.
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
static char rcsid[] __attribute__ ((unused)) ="$Header: /usr/cvsroot/magic-8.0/calma/CalmaWrite.c,v 1.8 2010/12/22 16:29:06 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef	SYSV
#include <time.h>
#else
#include <sys/time.h>
#endif

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/tech.h"
#include "cif/cif.h"
#include "cif/CIFint.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "calma/calmaInt.h"
#include "utils/main.h"		/* for Path and CellLibPath */
#include "utils/stack.h"

    /* Exports */
bool CalmaDoLabels = TRUE;	 /* If FALSE, don't output labels with GDS-II */
bool CalmaDoLower = TRUE;	 /* If TRUE, allow lowercase labels. */
bool CalmaFlattenArrays = FALSE; /* If TRUE, output arrays as individual uses */

    /* Experimental stuff---not thoroughly tested (as of Sept. 2007)! */
bool CalmaContactArrays = FALSE; /* If TRUE, output contacts as subcell arrays */
bool CalmaMergeTiles = FALSE;	 /* If TRUE, merge tiles into polygons in output. */

    /* Forward declarations */
extern int calmaWriteInitFunc();
extern int calmaWriteMarkFunc();
extern int calmaWritePaintFunc();
extern int calmaMergePaintFunc();
extern int calmaWriteUseFunc();
extern void calmaWriteContacts();
extern void calmaDelContacts();
extern void calmaOutFunc();
extern void calmaOutStructName();
extern void calmaWriteLabelFunc();
extern void calmaOutHeader();
extern void calmaOutDate();
extern void calmaOutStringRecord();
extern void calmaOut8();
extern void calmaOutR8();
extern void calmaProcessBoundary();
extern void calmaMergeBoundaries();
extern void calmaRemoveColinear();
extern void calmaRemoveDegenerate();

/* Structure used by calmaWritePaintFunc() */

typedef struct {
   FILE *f;		/* File stream for output		*/
   Rect *area;		/* Clipping area, in GDS coordinates	*/
} calmaOutputStruct;

/*--------------------------------------------------------------*/
/* Structures used by the tile merging algorithm 		*/
/*--------------------------------------------------------------*/

#define GDS_PENDING	0
#define GDS_UNPROCESSED CLIENTDEFAULT
#define GDS_PROCESSED	1

#define PUSHTILE(tp) \
    if ((tp)->ti_client == (ClientData) GDS_UNPROCESSED) { \
	(tp)->ti_client = (ClientData) GDS_PENDING; \
	STACKPUSH((ClientData) (tp), SegStack); \
    }

#define LB_EXTERNAL	0	/* Polygon external edge	*/
#define LB_INTERNAL	1	/* Polygon internal edge	*/
#define LB_INIT		2	/* Data not yet valid		*/

typedef struct LB1 {
    char lb_type;		/* Boundary Type (external or internal) */
    Point lb_start;		/* Start point */
    struct LB1 *lb_next;	/* Next point record */
} LinkedBoundary;

typedef struct BT1 {
    LinkedBoundary *bt_first;   /* Polygon list */
    int bt_points;		/* Number of points in this list */
    struct BT1 *bt_next;	/* Next polygon record */
} BoundaryTop;
    
/*--------------------------------------------------------------*/

/* Number assigned to each cell */
int calmaCellNum;

/* Factor by which to scale Magic coordinates for cells and labels. */
int calmaWriteScale;

/* Scale factor for outputting paint: */
int calmaPaintScale;

/*
 * Current layer number and "type".
 * In GDS-II format, this is output with each rectangle.
 */
int calmaPaintLayerNumber;
int calmaPaintLayerType;

/* Imports */
extern time_t time();


/* -------------------------------------------------------------------- */

/*
 * Macros to output various pieces of Calma information.
 * These are macros for speed.
 */

/* -------------------------------------------------------------------- */

/*
 * calmaOutRH --
 *
 * Output a Calma record header.
 * This consists of a two-byte count of the number of bytes in the
 * record (including the two count bytes), a one-byte record type,
 * and a one-byte data type.
 */
#define	calmaOutRH(count, type, datatype, f) \
    { calmaOutI2(count, f); (void) putc(type, f); (void) putc(datatype, f); }

/*
 * calmaOutI2 --
 *
 * Output a two-byte integer.
 * Calma byte order is the same as the network byte order used
 * by the various network library procedures.
 */
#define	calmaOutI2(n, f) \
    { \
	union { short u_s; char u_c[2]; } u; \
	u.u_s = htons(n); \
	(void) putc(u.u_c[0], f); \
	(void) putc(u.u_c[1], f); \
    }
/*
 * calmaOutI4 --
 *
 * Output a four-byte integer.
 * Calma byte order is the same as the network byte order used
 * by the various network library procedures.
 */
#define calmaOutI4(n, f) \
    { \
	union { long u_i; char u_c[4]; } u; \
	u.u_i = htonl(n); \
	(void) putc(u.u_c[0], f); \
	(void) putc(u.u_c[1], f); \
	(void) putc(u.u_c[2], f); \
	(void) putc(u.u_c[3], f); \
    }

static char calmaMapTableStrict[] =
{
      0,    0,    0,    0,    0,    0,    0,    0,	/* NUL - BEL */
      0,    0,    0,    0,    0,    0,    0,    0,	/* BS  - SI  */
      0,    0,    0,    0,    0,    0,    0,    0,	/* DLE - ETB */
      0,    0,    0,    0,    0,    0,    0,    0,	/* CAN - US  */
    '_',  '_',  '_',  '_',  '$',  '_',  '_',  '_',	/* SP  - '   */
    '_',  '_',  '_',  '_',  '_',  '_',  '_',  '_',	/* (   - /   */
    '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',	/* 0   - 7   */
    '8',  '9',  '_',  '_',  '_',  '_',  '_',  '_',	/* 8   - ?   */
    '_',  'A',  'B',  'C',  'D',  'E',  'F',  'G',	/* @   - G   */
    'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',	/* H   - O   */
    'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',	/* P   - W   */
    'X',  'Y',  'Z',  '_',  '_',  '_',  '_',  '_',	/* X   - _   */
    '_',  'a',  'b',  'c',  'd',  'e',  'f',  'g',	/* `   - g   */
    'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',	/* h   - o   */
    'p',  'q',  'r',  's',  't',  'u',  'v',  'w',	/* p   - w   */
    'x',  'y',  'z',  '_',  '_',  '_',  '_',  0,	/* x   - DEL */
};

static char calmaMapTablePermissive[] =
{
      0,    0,    0,    0,    0,    0,    0,    0,	/* NUL - BEL */
      0,    0,    0,    0,    0,    0,    0,    0,	/* BS  - SI  */
      0,    0,    0,    0,    0,    0,    0,    0,	/* DLE - ETB */
      0,    0,    0,    0,    0,    0,    0,    0,	/* CAN - US  */
    ' ',  '!',  '"',  '#',  '$',  '&',  '%', '\'',	/* SP  - '   */
    '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',	/* (   - /   */
    '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',	/* 0   - 7   */
    '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',	/* 8   - ?   */
    '@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',	/* @   - G   */
    'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',	/* H   - O   */
    'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',	/* P   - W   */
    'X',  'Y',  'Z',  '[', '\\',  ']',  '^',  '_',	/* X   - _   */
    '`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',	/* `   - g   */
    'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',	/* h   - o   */
    'p',  'q',  'r',  's',  't',  'u',  'v',  'w',	/* p   - w   */
    'x',  'y',  'z',  '{',  '|',  '}',  '~',    0,	/* x   - DEL */
};


/*
 * ----------------------------------------------------------------------------
 *
 * CalmaWrite --
 *
 * Write out the entire tree rooted at the supplied CellDef in Calma
 * GDS-II stream format, to the specified file.
 *
 * Results:
 *	TRUE if the cell could be written successfully, FALSE otherwise.
 *
 * Side effects:
 *	Writes a file to disk.
 *	In the event of an error while writing out the cell,
 *	the external integer errno is set to the UNIX error
 *	encountered.
 *
 * Algorithm:
 *
 *	Calma names can be strings of up to CALMANAMELENGTH characters.
 *	Because general names won't map into Calma names, we use the
 *	original cell name only if it is legal Calma, and otherwise
 *	generate a unique numeric name for the cell.
 *
 *	We make a depth-first traversal of the entire design tree, outputting
 *	each cell to the Calma file.  If a given cell has not been read in
 *	when we visit it, we read it in ourselves.
 *
 *	No hierarchical design rule checking or bounding box computation
 *	occur during this traversal -- both are explicitly avoided.
 *
 * ----------------------------------------------------------------------------
 */

bool
CalmaWrite(rootDef, f)
    CellDef *rootDef;	/* Pointer to CellDef to be written */
    FILE *f;		/* Open output file */
{
    int oldCount = DBWFeedbackCount, problems;
    bool good;
    CellUse dummy;

    /*
     * Do not attempt to write anything if a CIF/GDS output style
     * has not been specified in the technology file.
     */
    if (!CIFCurStyle)
    {
	TxError("No CIF/GDS output style set!\n");
	return FALSE;
    }

    /*
     * Make sure that the entire hierarchy rooted at rootDef is
     * read into memory and that timestamp mismatches are resolved
     * (this is needed so that we know that bounding boxes are OK).
     */

    dummy.cu_def = rootDef;
    DBCellReadArea(&dummy, &rootDef->cd_bbox);
    DBFixMismatch();

    /*
     * Go through all cells currently having CellDefs in the
     * def symbol table and mark them with negative numbers
     * to show that they should be output, but haven't yet
     * been.
     */
    (void) DBCellSrDefs(0, calmaWriteInitFunc, (ClientData) NULL);
    rootDef->cd_client = (ClientData) -1;
    calmaCellNum = -2;

    /* Output the header, identifying this file */
    calmaOutHeader(rootDef, f);

    /*
     * Write all contact cell definitions first
     */
    if (CalmaContactArrays) calmaWriteContacts(f);

    /*
     * We perform a post-order traversal of the tree rooted at 'rootDef',
     * to insure that each child cell is output before it is used.  The
     * root cell is output last.
     */
    (void) calmaProcessDef(rootDef, f);

    /* Finish up by outputting the end-of-library marker */
    calmaOutRH(4, CALMA_ENDLIB, CALMA_NODATA, f);
    fflush(f);
    good = !ferror(f);

    /* See if any problems occurred */
    if (problems = (DBWFeedbackCount - oldCount))
	TxPrintf("%d problems occurred.  See feedback entries.\n", problems);

    /*
     * Destroy all contact cell definitions 
     */
    if (CalmaContactArrays) calmaDelContacts();

    return (good);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaWriteInitFunc --
 *
 * Filter function called on behalf of CalmaWrite() above.
 * Responsible for setting the cif number of each cell to zero.
 *
 * Results:
 *	Returns 0 to indicate that the search should continue.
 *
 * Side effects:
 *	Modify the calma numbers of the cells they are passed.
 *
 * ----------------------------------------------------------------------------
 */

int
calmaWriteInitFunc(def)
    CellDef *def;
{
    def->cd_client = (ClientData) 0;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaProcessUse --
 * calmaProcessDef --
 *
 * Main loop of Calma generation.  Performs a post-order, depth-first
 * traversal of the tree rooted at 'def'.  Only cells that have not
 * already been output are processed.
 *
 * The procedure calmaProcessDef() is called initially; calmaProcessUse()
 * is called internally by DBCellEnum().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Causes Calma GDS-II stream-format to be output.
 *	Returns when the stack is empty.
 *
 * ----------------------------------------------------------------------------
 */

int
calmaProcessUse(use, outf)
    CellUse *use;	/* Process use->cu_def */
    FILE *outf;		/* Stream file */
{
    return (calmaProcessDef(use->cu_def, outf));
}

int
calmaProcessDef(def, outf)
    CellDef *def;	/* Output this def's children, then the def itself */
    FILE *outf;		/* Stream file */
{
    char *filename;
    bool isReadOnly, oldStyle, hasContent;

    /* Skip if already output */
    if ((int) def->cd_client > 0)
	return (0);

    /* Assign it a (negative) number if it doesn't have one yet */
    if ((int) def->cd_client == 0)
	def->cd_client = (ClientData) calmaCellNum--;

    /* Mark this cell */
    def->cd_client = (ClientData) (- (int) def->cd_client);

    /* Read the cell in if it is not already available. */
    if ((def->cd_flags & CDAVAILABLE) == 0)
	if (!DBCellRead(def, (char *) NULL, TRUE, NULL))
	    return (0);

    /*
     * Output the definitions for any of our descendants that have
     * not already been output.  Numbers are assigned to the subcells
     * as they are output.
     */
    (void) DBCellEnum(def, calmaProcessUse, (ClientData) outf);

    /*
     * Check if this is a read-only file that is supposed to be copied
     * verbatim from input to output.  If so, do the direct copy.  If
     * not, or if there is any problem obtaining the original cell
     * definition, resort to writing out magic's version of the def,
     * and print a warning message.
     * 
     * Treat the lack of a GDS_START property as an indication
     * that we should treat this cell like a reference-only
     * cell.  That is, the instance will be called but no
     * definition will appear in the output.
     */

    DBPropGet(def, "GDS_START", &hasContent);
    filename = (char *)DBPropGet(def, "GDS_FILE", &isReadOnly);

    if (isReadOnly && hasContent)
    {
	char *buffer, *offptr;
	size_t defsize, numbytes;
	off_t cellstart, cellend;
 	dlong cval;
	FILE *fi;

	/* Use PaOpen() so the paths searched are the same as were	*/
	/* searched to find the .mag file that indicated this GDS file.	*/

	fi = PaOpen(filename, "r", "", Path, CellLibPath, (char **)NULL);
	if (fi == NULL)
	{
	    /* This is a rare error, but if the subcell is inside	*/
	    /* another vendor GDS, it would not normally be output.	*/

	    DBPropGet(def->cd_parents->cu_parent, "GDS_FILE", &isReadOnly);
	    if (!isReadOnly)
		TxError("Calma output error:  Can't find GDS file \"%s\" "
				"for vendor cell \"%s\".  Using magic's "
				"internal definition\n", filename,
				def->cd_name);
	    else 
		def->cd_flags |= CDVENDORGDS;
	}
	else
	{
	    offptr = (char *)DBPropGet(def, "GDS_END", NULL);
	    sscanf(offptr, "%"DLONG_PREFIX"d", &cval);
	    cellend = (off_t)cval;
	    offptr = (char *)DBPropGet(def, "GDS_BEGIN", &oldStyle);
	    if (!oldStyle)
	    {
		offptr = (char *)DBPropGet(def, "GDS_START", NULL);

		/* Write our own header and string name, to ensure	*/
		/* that the magic cell name and GDS name match.		*/

		/* Output structure header */
		calmaOutRH(28, CALMA_BGNSTR, CALMA_I2, outf);
		calmaOutDate(def->cd_timestamp, outf);
		calmaOutDate(time((time_t *) 0), outf);

		/* Name structure the same as the magic cellname */
		calmaOutStructName(CALMA_STRNAME, def, outf);
	    }

	    sscanf(offptr, "%"DLONG_PREFIX"d", &cval);
	    cellstart = (off_t)cval;
	    fseek(fi, cellstart, SEEK_SET);

	    if (cellend < cellstart)	/* Sanity check */
	    {
		TxError("Calma output error:  Bad vendor GDS file reference!\n");
		isReadOnly = FALSE;
	    }
	    else
	    {
		defsize = (size_t)(cellend - cellstart);

		buffer = (char *)mallocMagic(defsize);
		numbytes = fread(buffer, sizeof(char), (size_t)defsize, fi);
		if (numbytes == defsize)
		{
		    numbytes = fwrite(buffer, sizeof(char), (size_t)defsize, outf);
		    if (numbytes <= 0)
		    {
			TxError("Calma output error:  Can't write cell from vendor GDS."
				"  Using magic's internal definition\n");
			isReadOnly = FALSE;
		    }
		}
		else
		{
		    TxError("Calma output error:  Can't read cell from vendor GDS."
				"  Using magic's internal definition\n");
		    isReadOnly = FALSE;
		}
		freeMagic(buffer);
	    }
	    fclose(fi);

	    /* Mark the definition as vendor GDS so that magic doesn't	*/
	    /* try to generate subcell interaction or array interaction	*/
	    /* paint for it.						*/

	    def->cd_flags |= CDVENDORGDS;
	}
    }

    /* Output this cell definition from the Magic database */
    if (!isReadOnly)
	calmaOutFunc(def, outf, &TiPlaneRect);

    return (0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * calmaOutFunc --
 *
 * Write out the definition for a single cell as a GDS-II stream format
 * structure.  We try to preserve the original cell's name if it is legal
 * in GDS-II; otherwise, we generate a unique name.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends to the open Calma output file.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOutFunc(def, f, cliprect)
    CellDef *def;	/* Pointer to cell def to be written */
    FILE *f;		/* Open output file */
    Rect *cliprect;	/* Area to clip to (used for contact cells),
			 * in CIF/GDS coordinates.
			 */
{
    Label *lab;
    CIFLayer *layer;
    Rect bigArea;
    int type;
    int dbunits;
    calmaOutputStruct cos;

    cos.f = f;
    cos.area = (cliprect == &TiPlaneRect) ? NULL : cliprect;

    /* Output structure begin */
    calmaOutRH(28, CALMA_BGNSTR, CALMA_I2, f);
    calmaOutDate(def->cd_timestamp, f);
    calmaOutDate(time((time_t *) 0), f);

    /* Output structure name */
    calmaOutStructName(CALMA_STRNAME, def, f);

    /* Since Calma database units are nanometers, multiply all units by 10,
     * modified by the scale multiplier.
     */

    dbunits = (CIFCurStyle->cs_flags & CWF_ANGSTROMS) ? 100 : 10;
    if ((dbunits % CIFCurStyle->cs_expander) == 0)
    {
	calmaWriteScale = CIFCurStyle->cs_scaleFactor * dbunits
		/ CIFCurStyle->cs_expander;
	calmaPaintScale = dbunits / CIFCurStyle->cs_expander;
    }
    else
    {
	TxError("Calma output error:  Output scale units are %2.1f nanometers.\n",
		(float)dbunits / (float)CIFCurStyle->cs_expander);
	TxError("Magic Calma output will be scaled incorrectly!\n");
	if ((dbunits == 10) && ((100 % CIFCurStyle->cs_expander) == 0))
	{
	    TxError("Please add \"units angstroms\" to the cifoutput section"
			" of the techfile.\n");
	}
	else
	{
	    TxError("Magic GDS output is limited to a minimum dimension of"
			" 1 angstrom.\n");
	}
	/* Set expander to 10 so output scales are not zero. */
	calmaWriteScale = CIFCurStyle->cs_scaleFactor;
	calmaPaintScale = 1;
    }

    /*
     * Output the calls that the child makes to its children.  For
     * arrays we output a single call, unlike CIF, since Calma
     * supports the notion of arrays.
     */
    (void) DBCellEnum(def, calmaWriteUseFunc, (ClientData) f);

    /* Output all the tiles associated with this cell; skip temporary layers */
    GEO_EXPAND(&def->cd_bbox, CIFCurStyle->cs_radius, &bigArea);
    CIFErrorDef = def;
    CIFGen(def, &bigArea, CIFPlanes, &DBAllTypeBits, TRUE, TRUE, (ClientData) f);
    if (!CIFHierWriteDisable)
	CIFGenSubcells(def, &bigArea, CIFPlanes);
    if (!CIFArrayWriteDisable)
	CIFGenArrays(def, &bigArea, CIFPlanes);

    for (type = 0; type < CIFCurStyle->cs_nLayers; type++)
    {
	layer = CIFCurStyle->cs_layers[type];
	if (layer->cl_flags & CIF_TEMP) continue;
	if (!CalmaIsValidLayer(layer->cl_calmanum)) continue;
	calmaPaintLayerNumber = layer->cl_calmanum;
	calmaPaintLayerType = layer->cl_calmatype;

	DBSrPaintArea((Tile *) NULL, CIFPlanes[type],
		cliprect, &CIFSolidBits, (CalmaMergeTiles) ?
		calmaMergePaintFunc : calmaWritePaintFunc,
		(ClientData) &cos);
    }

    /* Output labels */
    if (CalmaDoLabels)
	for (lab = def->cd_labels; lab; lab = lab->lab_next)
	    calmaWriteLabelFunc(lab,
			    CIFCurStyle->cs_labelLayer[lab->lab_type], f);

    /* End of structure */
    calmaOutRH(4, CALMA_ENDSTR, CALMA_NODATA, f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaIsUseNameDefault --
 *
 * Determine if this use name is not default; that is, it is not the name
 * of the cell def followed by an underscore and a use index number.  If
 * it is not default, then we want to write out the use name as a property
 * in the GDS stream file so that we can recover the name when the file is
 * read back into magic.
 *
 * Results:
 *	TRUE if the cell use ID is a default name; FALSE if not.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaIsUseNameDefault(defName, useName)
    char *defName;
    char *useName;
{
    int idx, slen;
    char *sptr;

    if (useName == NULL) return TRUE;
    slen = strlen(defName);
    if (!strncmp(defName, useName, slen))
    {
	sptr = useName + slen;
	if (*sptr != '_') return FALSE;
	else sptr++;
	if (sscanf(sptr, "%d", &idx) != 1) return FALSE;
	else return TRUE;
    }
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaWriteUseFunc --
 *
 * Filter function, called by DBCellEnum on behalf of calmaOutFunc above,
 * to write out each CellUse called by the CellDef being output.  If the
 * CellUse is an array, we output it as a single array instead of as
 * individual uses like CIF.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends to the open Calma output file.
 *
 * ----------------------------------------------------------------------------
 */

int
calmaWriteUseFunc(use, f)
    CellUse *use;
    FILE *f;
{
    /*
     * r90, r180, and r270 are Calma 8-byte real representations
     * of the angles 90, 180, and 270 degrees.  Because there are
     * only 4 possible values, it is faster to have them pre-computed
     * than to format with calmaOutR8().
     */
    static unsigned char r90[] = { 0x42, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static unsigned char r180[] = { 0x42, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static unsigned char r270[] = { 0x43, 0x10, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00 };
    unsigned char *whichangle;
    int x, y, topx, topy, rows, cols, xxlate, yxlate, hdrsize;
    int rectype, stransflags;
    Transform *t;
    bool isArray = FALSE;
    Point p, p2;

    topx = use->cu_xhi - use->cu_xlo;
    if (topx < 0) topx = -topx;
    topy = use->cu_yhi - use->cu_ylo;
    if (topy < 0) topy = -topy;

    /*
     * The following translates from the abcdef transforms that
     * we use internally to the rotation and mirroring specification
     * used in Calma stream files.  It only works because orientations
     * are orthogonal in magic, and no scaling is allowed in cell use
     * transforms.  Thus the elements a, b, d, and e always have one
     * of the following forms:
     *
     *		a  d
     *		b  e
     *
     * (counterclockwise rotations of 0, 90, 180, 270 degrees)
     *
     *	1  0	0  1	-1  0	 0 -1
     *	0  1   -1  0	 0 -1	 1  0
     *
     * (mirrored across the x-axis before counterclockwise rotation
     * by 0, 90, 180, 270 degrees):
     *
     *	1  0    0  1    -1  0    0 -1
     *	0 -1    1  0     0  1   -1  0
     *
     * Note that mirroring must be done if either a != e, or
     * a == 0 and b == d.
     *
     */
    t = &use->cu_transform;
    stransflags = 0;
    whichangle = (t->t_a == -1) ? r180 : (unsigned char *) NULL;
    if (t->t_a != t->t_e || (t->t_a == 0 && t->t_b == t->t_d))
    {
	stransflags |= CALMA_STRANS_UPSIDEDOWN;
	if (t->t_a == 0)
	{
	    if (t->t_b == 1) whichangle = r90;
	    else whichangle = r270;
	}
    }
    else if (t->t_a == 0)
    {
	if (t->t_b == -1) whichangle = r90;
	else whichangle = r270;
    }

    if (CalmaFlattenArrays)
    {
	for (x = 0; x <= topx; x++)
	{
	    for (y = 0; y <= topy; y++)
	    {
		/* Structure reference */
		calmaOutRH(4, CALMA_SREF, CALMA_NODATA, f);
		calmaOutStructName(CALMA_SNAME, use->cu_def, f);

		/* Transformation flags */
		calmaOutRH(6, CALMA_STRANS, CALMA_BITARRAY, f);
		calmaOutI2(stransflags, f);

		/* Rotation if there is one */
		if (whichangle)
		{
		    calmaOutRH(12, CALMA_ANGLE, CALMA_R8, f);
		    calmaOut8(whichangle, f);
		}

		/* Translation */
		xxlate = t->t_c + t->t_a*(use->cu_xsep)*x
				+ t->t_b*(use->cu_ysep)*y;
		yxlate = t->t_f + t->t_d*(use->cu_xsep)*x
				+ t->t_e*(use->cu_ysep)*y;
		xxlate *= calmaWriteScale;
		yxlate *= calmaWriteScale;
		calmaOutRH(12, CALMA_XY, CALMA_I4, f);
		calmaOutI4(xxlate, f);
		calmaOutI4(yxlate, f);

		/* End of element */
		calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);
	    }
	}
    }
    else
    {
	/* Is it an array? */
	isArray = (topx > 0 || topy > 0);
	rectype = isArray ? CALMA_AREF : CALMA_SREF;

	/* Structure reference */
	calmaOutRH(4, rectype, CALMA_NODATA, f);
	calmaOutStructName(CALMA_SNAME, use->cu_def, f);

	/* Transformation flags */
	calmaOutRH(6, CALMA_STRANS, CALMA_BITARRAY, f);
	calmaOutI2(stransflags, f);

	/* Rotation if there is one */
	if (whichangle)
	{
	    calmaOutRH(12, CALMA_ANGLE, CALMA_R8, f);
	    calmaOut8(whichangle, f);
	}

	/* If array, number of columns and rows in the array */
	if (isArray)
	{
	    calmaOutRH(8, CALMA_COLROW, CALMA_I2, f);
	    cols = topx + 1;
	    rows = topy + 1;
	    calmaOutI2(cols, f);
	    calmaOutI2(rows, f);
	}

	/* Translation */
	xxlate = t->t_c * calmaWriteScale;
	yxlate = t->t_f * calmaWriteScale;
	hdrsize = isArray ? 28 : 12;
	calmaOutRH(hdrsize, CALMA_XY, CALMA_I4, f);
	calmaOutI4(xxlate, f);
	calmaOutI4(yxlate, f);

	/* Array sizes if an array */
	if (isArray)
	{
	    /* Column reference point */
	    p.p_x = use->cu_xsep * cols;
	    p.p_y = 0;
	    GeoTransPoint(t, &p, &p2);
	    p2.p_x *= calmaWriteScale;
	    p2.p_y *= calmaWriteScale;
	    calmaOutI4(p2.p_x, f);
	    calmaOutI4(p2.p_y, f);

	    /* Row reference point */
	    p.p_x = 0;
	    p.p_y = use->cu_ysep * rows;
	    GeoTransPoint(t, &p, &p2);
	    p2.p_x *= calmaWriteScale;
	    p2.p_y *= calmaWriteScale;
	    calmaOutI4(p2.p_x, f);
	    calmaOutI4(p2.p_y, f);
	}

	/* By NP */
	/* Property attributes/value pairs. */
	/* Add a CellUse ID property, if the CellUse has a non-default name */

	if (!calmaIsUseNameDefault(use->cu_def->cd_name, use->cu_id))
	{
	    calmaOutRH(6, CALMA_PROPATTR, CALMA_I2, f);
	    calmaOutI2(CALMA_PROP_USENAME, f);
	    calmaOutStringRecord(CALMA_PROPVALUE, use->cu_id, f);
	}

	/* Add an array limits property, if the CellUse is an array and */
	/* limits of the array (xlo, ylo) are not zero (the default).	*/

	if ((use->cu_xlo != 0) || (use->cu_ylo != 0))
	{
	    char arraystr[128];
	    sprintf(arraystr, "%d_%d_%d_%d", use->cu_xlo, use->cu_xhi,
			use->cu_ylo, use->cu_yhi);
	    calmaOutRH(6, CALMA_PROPATTR, CALMA_I2, f);
	    calmaOutI2(CALMA_PROP_ARRAY_LIMITS, f);
	    calmaOutStringRecord(CALMA_PROPVALUE, arraystr, f);
	}

	/* End of element */
	calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaOutStructName --
 *
 * Output the name of a cell def.
 * If the name is legal GDS-II, use it; otherwise, generate one
 * that is legal and unique.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the disk file.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOutStructName(type, def, f)
    int type;
    CellDef *def;
    FILE *f;
{
    char defname[CALMANAMELENGTH+1];
    unsigned char c;
    char *cp;
    int calmanum;
    char *table;

    if (CIFCurStyle->cs_flags & CWF_PERMISSIVE_LABELS) 
    {
	table = calmaMapTablePermissive;
    } else {
	table = calmaMapTableStrict;
    }

    /* Is the def name a legal Calma name? */
    for (cp = def->cd_name; c = (unsigned char) *cp; cp++)
    {
	if ((c > 127) || (table[c] == 0))
	    goto bad;
	else if ((unsigned char)table[c] != c)
	{
	    TxError("Warning: character \'%c\' changed to \'%c\' in"
		" name %s\n", (char)c, table[c], def->cd_name);
	}
	/* We really should ensure that the new name is unique. . . */
    }
    if (cp <= def->cd_name + CALMANAMELENGTH)
    {
	/* Yes, it's legal: use it */
	(void) strcpy(defname, def->cd_name);
    }
    else
    {
	/* Bad name: use XXXXXcalmaNum */
bad:
	calmanum = (int) def->cd_client;
	if (calmanum < 0) calmanum = -calmanum;
	(void) sprintf(defname, "XXXXX%d", calmanum);
	TxError("Warning: string in output unprintable; changed to \'%s\'\n",
		 defname);
    }

    calmaOutStringRecord(type, defname, f);
}

/* Added by NP 8/21/2004 */
/*
 * ----------------------------------------------------------------------------
 *
 * calmaGetContactCell --
 *
 *   This routine creates [if it hasn't been created yet] a cell definition
 *   containing the given TileType.  Cellname is "$$" + layer1_name + "_" +
 *   layer2_name... + "$$". Cellname contains the short name of all the
 *   residues of the layer "type".
 *
 * Results:
 *   Returns new celldef it doesn't exist else created one.
 *
 * Side effects:
 *	 New celldef created specially for contact type if it does not exist.
 *
 * ----------------------------------------------------------------------------
 */

CellDef *
calmaGetContactCell(type, lookOnly)
    TileType type;		/* magic contact tile type */
    bool lookOnly;		/* if true, don't generate any new cells */
{
    TileType j;
    char contactCellName[100];
    TileTypeBitMask *rMask = DBResidueMask(type);
    CellDef *def;
    bool first = TRUE;

    strcpy(contactCellName, "$$");
    for (j = TT_SPACE + 1; j < DBNumUserLayers; j++)
	if (TTMaskHasType(rMask, j))
	{
            /* Cellname starts with "$$" to make it diffrent from
             * other database cells, and to be compatible with a
	     * number of other EDA tools.
             */
	    if (!first)
		strcat(contactCellName, "_");
	    else
		first = FALSE;
            strcat(contactCellName, DBTypeShortName(j));
        }
    strcat(contactCellName, "$$");

    def = DBCellLookDef(contactCellName);
    if ((def == (CellDef *) NULL) && (lookOnly == FALSE))
    {
	def = DBCellNewDef(contactCellName, (char *) NULL);
       	def->cd_flags &= ~(CDMODIFIED|CDGETNEWSTAMP);
        def->cd_flags |= CDAVAILABLE;
    }
    return def;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CalmaGenerateArray --
 *
 *	This routine
 *
 * Results:
 *	TRUE on success, FALSE if no contact cell could be found.
 *
 * Side effects:
 *	Writes an AREF record to the GDS stream output.
 *
 * ----------------------------------------------------------------------------
 */

bool
CalmaGenerateArray(f, type, llx, lly, pitch, cols, rows)
    FILE *f;		/* GDS output file */
    TileType type;	/* Magic tile type of contact */
    int llx, lly;	/* Lower-left hand coordinate of the array
			 * (centered on contact cut)
			 */
    int pitch;		/* Pitch of the array elements */
    int cols, rows;	/* Number of array elements in X and Y */
{
    CellDef *child;	/* Cell definition of the contact cell */
    int xxlate, yxlate;

    child = calmaGetContactCell(type, TRUE);
    if (child == NULL) return FALSE;

    /* Structure reference */
    calmaOutRH(4, CALMA_AREF, CALMA_NODATA, f);
    calmaOutStructName(CALMA_SNAME, child, f);

    /* Transformation flags */
    calmaOutRH(6, CALMA_STRANS, CALMA_BITARRAY, f);
    calmaOutI2(0, f);

    /* Number of columns and rows in the array */
    calmaOutRH(8, CALMA_COLROW, CALMA_I2, f);
    calmaOutI2(cols, f);
    calmaOutI2(rows, f);

    /* Translation */
    xxlate = llx * calmaPaintScale;
    yxlate = lly * calmaPaintScale;
    calmaOutRH(28, CALMA_XY, CALMA_I4, f);
    calmaOutI4(xxlate, f);
    calmaOutI4(yxlate, f);

    /* Column reference point */
    calmaOutI4(xxlate + pitch * cols * calmaPaintScale, f);
    calmaOutI4(yxlate, f);

    /* Row reference point */
    calmaOutI4(xxlate, f);
    calmaOutI4(yxlate + pitch * rows * calmaPaintScale, f);

    /* End of AREF element */
    calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);

    return TRUE;
}

/* Added by NP 8/22/2004 */
/*
 * ----------------------------------------------------------------------------
 *
 * calmaWriteContacts --
 *
 *  This routine creates a new cellDef for each contact type and writes to
 *  the GDS output stream file. It is called before processing all cell
 *  definitions while writing GDS output.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes contact cell definition to the open Calma output file.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaWriteContacts(f)
    FILE *f;
{
    TileType type;
    TileTypeBitMask tMask, *rMask;
    CellDef *def, *cellDef;
    Rect area, cliprect;
    int halfwidth, halfsize;
    CIFOp *op;

    /* Turn off generation of contact arrays for the duration of this	*/
    /* subroutine, so that the contact definitions themselves will get	*/
    /* the proper contact cut drawn.  It is turned on again at the end	*/
    /* of the routine.  Note that this routine is not called unless	*/
    /* CalmaContactArrays is TRUE.					*/
    
    CalmaContactArrays = FALSE;

    DBEnumerateTypes(&tMask);

    /* Decompose stacking types */
    for (type = DBNumUserLayers; type < DBNumTypes; type++)
	if (TTMaskHasType(&tMask, type))
	{
	    rMask = DBResidueMask(type);
	    TTMaskSetMask(&tMask, rMask);
	}

    for (type = TT_SPACE + 1; type < DBNumUserLayers; type++)
    {
	/* We need to create cell array only for contact types */
	if (DBIsContact(type) && TTMaskHasType(&tMask, type))
	{
            /* Write definition of cell to GDS stream.	*/
	    /* Get cell definition for Tiletype type */
            def = calmaGetContactCell(type, FALSE);

            /* Get clip bounds, so that residue surround is	*/
	    /* minimum.  Note that these values are in CIF/GDS	*/
	    /* units, and the clipping rectangle passed to	*/
	    /* calmaOutFunc is also in CIF/GDS units.		*/

	    halfsize = CIFGetContactSize(type, NULL, NULL, NULL) >> 1;

            /* Get minimum width for layer by rounding halfsize	*/
	    /* to the nearest lambda value.			*/
            halfwidth = halfsize / CIFCurStyle->cs_scaleFactor;
	    if ((halfsize % CIFCurStyle->cs_scaleFactor) != 0)
		halfwidth++;

            area.r_xbot = area.r_ybot = -halfwidth;
            area.r_xtop = area.r_ytop = halfwidth;
       	    UndoDisable();
       	    DBPaint(def, &area, type);
       	    DBReComputeBbox(def);
    	    TTMaskSetType(&def->cd_types, type);

	    /* Clip output to the bounds of "cliprect"	*/
	    cliprect.r_xbot = cliprect.r_ybot = -halfsize;
	    cliprect.r_xtop = cliprect.r_ytop = halfsize;

            calmaOutFunc(def, f, &cliprect);
            UndoEnable();
	}
    }
    CalmaContactArrays = TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaDelContacts --
 *
 *  This routine removes all cell definitions generated by
 *  calmaWriteContacts().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes contact cell defs from the database.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaDelContacts()
{
    TileType type;
    CellDef *def;

    for (type = TT_SPACE + 1; type < DBNumUserLayers; type++)
	if (DBIsContact(type))
	{
            def = calmaGetContactCell(type, TRUE);
	    if (def != (CellDef *)NULL)
		 DBCellDeleteDef(def);
	}
}

/*
 * ----------------------------------------------------------------------------
 * calmaAddSegment ---
 *
 * Process a new polygon edge, inserting it into a polygon record as
 * required.  If the edge is between a GDS layer and TT_SPACE, then
 * we insert a point record.  If the edge is between two tiles of the
 * same layer, then we insert a tile record.
 *
 * Results:
 *	Return 1 if an internal segment was generated, 0 if an external
 *	segment was generate.  On error, return -1 (failure to find a
 *	connecting point; this shouldn't happen).
 *
 *	Returns the current segment in the original pointer position (1st
 *	argument).  If segments are added in counterclockwise order, then
 *	this should be most efficient.
 *
 * Side effects:
 *	May allocate memory. 
 * ---------------------------------------------------------------------------
 */

int
calmaAddSegment(lbptr, poly_edge, p1x, p1y, p2x, p2y)
    LinkedBoundary **lbptr;
    bool poly_edge;
    int p1x, p1y, p2x, p2y;
{
    LinkedBoundary *newseg, *curseg, *stopseg;
    bool startmatch = FALSE;
    bool endmatch = FALSE;

    stopseg = NULL;
    for (curseg = *lbptr; curseg != stopseg; curseg = curseg->lb_next)
    {
	stopseg = *lbptr;
	if (curseg->lb_type == LB_INIT)
	{
	    if ((p1x == curseg->lb_start.p_x) && (p1y == curseg->lb_start.p_y))
		startmatch = TRUE;

	    if ((p2x == curseg->lb_next->lb_start.p_x) &&
			(p2y == curseg->lb_next->lb_start.p_y))
		endmatch = TRUE;

	    if (startmatch && endmatch)
	    {
		/* Segment completes this edge */
		curseg->lb_type = (poly_edge) ? LB_EXTERNAL : LB_INTERNAL;
		*lbptr = curseg;
		return (int)curseg->lb_type;
	    }
	    else if (startmatch || endmatch)
	    {
		/* Insert a new segment after curseg */
		newseg = (LinkedBoundary *)mallocMagic(sizeof(LinkedBoundary));
		newseg->lb_next = curseg->lb_next;
		curseg->lb_next = newseg;

		if (startmatch)
		{
		    newseg->lb_type = curseg->lb_type;
		    curseg->lb_type = (poly_edge) ? LB_EXTERNAL : LB_INTERNAL;
		    newseg->lb_start.p_x = p2x;
		    newseg->lb_start.p_y = p2y;
		}
		else
		{
		    newseg->lb_type = (poly_edge) ? LB_EXTERNAL : LB_INTERNAL;
		    newseg->lb_start.p_x = p1x;
		    newseg->lb_start.p_y = p1y;
		}
		curseg = newseg;
		*lbptr = curseg;
		return (int)curseg->lb_type;
	    }
	}
    }
    return -1;		/* This shouldn't happen, but isn't fatal. */
}

/*
 * ----------------------------------------------------------------------------
 * calmaRemoveDegenerate ---
 *
 *    This routine takes lists of polygons and removes any degenerate
 *    segments (those that backtrack on themselves) from each one.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Deallocates memory for any segments that are removed.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaRemoveDegenerate(blist)
    BoundaryTop *blist;
{
    bool segfound;
    LinkedBoundary *stopseg, *curseg, *lastseg;
    BoundaryTop *bounds;

    for (bounds = blist; bounds != NULL; bounds = bounds->bt_next)
    {
	segfound = TRUE;
	while (segfound)
	{
	    segfound = FALSE;
	    stopseg = NULL;
	    for (lastseg = bounds->bt_first; lastseg != stopseg;)
	    {
		stopseg = bounds->bt_first;
		curseg = lastseg->lb_next;

		if (GEO_SAMEPOINT(curseg->lb_start,
			curseg->lb_next->lb_next->lb_start))
		{
		    segfound = TRUE;
		    lastseg->lb_next = curseg->lb_next->lb_next;

		    freeMagic(curseg->lb_next);
		    freeMagic(curseg);
	    
		    /* Make sure record doesn't point to a free'd segment */
		    bounds->bt_first = lastseg;
		    bounds->bt_points -= 2;
		    break;
		}
		else
		    lastseg = lastseg->lb_next;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * calmaRemoveColinear ---
 *
 *    This routine takes lists of polygons and removes any redundant
 *    (colinear) points.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Deallocates memory for any segments that are removed.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaRemoveColinear(blist)
    BoundaryTop *blist;
{
    LinkedBoundary *stopseg, *curseg, *lastseg;
    BoundaryTop *bounds;

    for (bounds = blist; bounds != NULL; bounds = bounds->bt_next)
    {
	stopseg = NULL;
	for (lastseg = bounds->bt_first; lastseg != stopseg;)
	{
	    stopseg = bounds->bt_first;
	    curseg = lastseg->lb_next;

	    if (((lastseg->lb_start.p_x == curseg->lb_start.p_x) &&
		(lastseg->lb_start.p_x == curseg->lb_next->lb_start.p_x)) ||
		((lastseg->lb_start.p_y == curseg->lb_start.p_y) &&
		(lastseg->lb_start.p_y == curseg->lb_next->lb_start.p_y)))
	    {
		lastseg->lb_next = curseg->lb_next;
	    
		/* Make sure record doesn't point to a free'd segment */
		if (curseg == bounds->bt_first) bounds->bt_first = lastseg;

		freeMagic(curseg);
		bounds->bt_points--;
	    }
	    else if ((lastseg->lb_start.p_x != curseg->lb_start.p_x) &&
		(lastseg->lb_start.p_y != curseg->lb_start.p_y) &&
		(curseg->lb_start.p_x != curseg->lb_next->lb_start.p_x) &&
		(curseg->lb_start.p_y != curseg->lb_next->lb_start.p_y))
	    {
		/* Check colinearity of non-Manhattan edges */
		int delx1, dely1, delx2, dely2, gcf;

		delx1 = curseg->lb_start.p_x - lastseg->lb_start.p_x;
		dely1 = curseg->lb_start.p_y - lastseg->lb_start.p_y;
		delx2 = curseg->lb_next->lb_start.p_x - curseg->lb_start.p_x;
		dely2 = curseg->lb_next->lb_start.p_y - curseg->lb_start.p_y;

		if ((delx1 != delx2) || (dely1 != dely2))
		{
		    gcf = FindGCF(delx1, dely1);
		    if (gcf > 1)
		    {
			delx1 /= gcf;
			dely1 /= gcf;
		    }
		}
		if ((delx1 != delx2) || (dely1 != dely2))
		{
		    gcf = FindGCF(delx2, dely2);
		    if (gcf > 1)
		    {
			delx2 /= gcf;
			dely2 /= gcf;
		    }
		}
		if ((delx1 == delx2) && (dely1 == dely2))
	 	{
		    lastseg->lb_next = curseg->lb_next;
		    if (curseg == bounds->bt_first) bounds->bt_first = lastseg;
		    freeMagic(curseg);
		    bounds->bt_points--;
		}
		else
		    lastseg = lastseg->lb_next;
	    }
	    else
		lastseg = lastseg->lb_next;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * calmaMergeSegments ---
 *
 *    Once a tile has been disassembled into segments, and it is not a simple
 *    rectangle (which would have been handled already), then merge it into
 *    the list of boundaries.
 *
 *    Note that this algorithm is O(N^2) and has lots of room for improvement!
 *    Still, each segment is never checked against more than 200 points,
 *    because when a boundary reaches this number (the maximum for GDS
 *    boundary records), the record will tend to be skipped (it should
 *    probably be output here. . .) 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output, memory allocation and deallocation
 *
 * ----------------------------------------------------------------------------
 */

void
calmaMergeSegments(edge, blist, num_points)
    LinkedBoundary *edge;    
    BoundaryTop **blist;
    int num_points;
{
    LinkedBoundary *stopseg, *curseg, *lastseg;
    LinkedBoundary *compstop, *compseg, *complast;
    BoundaryTop *bounds, *newbounds;

    if (*blist == NULL) goto make_new_bound;

    /* Check each internal edge for an antiparallel match with	*/
    /* an internal edge in the boundary lists.			*/

    stopseg = NULL;
    for (lastseg = edge; lastseg != stopseg; lastseg = lastseg->lb_next)
    {
	stopseg = edge;
	curseg = lastseg->lb_next;
	if (curseg->lb_type == LB_EXTERNAL) continue;

	for (bounds = *blist; bounds != NULL; bounds = bounds->bt_next)
	{
	    /* Avoid overflow on GDS boundary point limit.  Note	*/
	    /* that a merge will remove 2 points, but GDS requires	*/
	    /* that we add the 1st point to the end of the list.	*/

	    if (bounds->bt_points + num_points > 201) continue;

	    compstop = NULL;
	    for (complast = bounds->bt_first; complast != compstop;
			complast = complast->lb_next)
	    {
	        compstop = bounds->bt_first;
		compseg = complast->lb_next;
		if (compseg->lb_type == LB_EXTERNAL) continue;

		/* Edges match antiparallel only. Rect points are *not*	*/
		/* canonical. r_ll and p1 are both 1st points traveling	*/
		/* in a counterclockwise direction along the perimeter.	*/

		if (GEO_SAMEPOINT(compseg->lb_start, curseg->lb_next->lb_start) &&
		   GEO_SAMEPOINT(compseg->lb_next->lb_start, curseg->lb_start))
		{
		    lastseg->lb_next = compseg->lb_next;
		    complast->lb_next = curseg->lb_next;

		    freeMagic(compseg);
		    freeMagic(curseg);
	
		    /* Make sure the record doesn't point to the free'd segment */
		    if (compseg == bounds->bt_first) bounds->bt_first = complast;
		    bounds->bt_points += num_points - 2;
		    return;
		}
	    }
	}
    }

    /* If still no connecting edge was found, or if we overflowed the GDS max	*/
    /* number of records for a boundary, then start a new entry.		*/ 

make_new_bound:

    newbounds = (BoundaryTop *)mallocMagic(sizeof(BoundaryTop));
    newbounds->bt_first = edge;
    newbounds->bt_next = *blist;
    newbounds->bt_points = num_points;
    *blist = newbounds;
}

/*
 * ----------------------------------------------------------------------------
 * Process a LinkedBoundary list into a polygon and generate GDS output.
 * Free the linked list when done.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output, memory deallocation.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaProcessBoundary(blist, cos)
    BoundaryTop *blist;
    calmaOutputStruct *cos;
{
    FILE *f = cos->f;
    LinkedBoundary *listtop, *lbref, *lbstop, *lbfree;
    BoundaryTop *bounds;
    int sval;
    int chkcount;	/* diagnostic */

    for (bounds = blist; bounds != NULL; bounds = bounds->bt_next)
    {
	/* Boundary */
	calmaOutRH(4, CALMA_BOUNDARY, CALMA_NODATA, f);

	/* Layer */
	calmaOutRH(6, CALMA_LAYER, CALMA_I2, f);
	calmaOutI2(calmaPaintLayerNumber, f);

	/* Data type */
	calmaOutRH(6, CALMA_DATATYPE, CALMA_I2, f);
	calmaOutI2(calmaPaintLayerType, f);

	/* Record length = ((#points + 1) * 2 values * 4 bytes) + 4 bytes header */
	calmaOutRH(4 + (bounds->bt_points + 1) * 8, CALMA_XY, CALMA_I4, f);

	/* Coordinates (repeat 1st point) */

	listtop = bounds->bt_first;
	lbstop = NULL;
	chkcount = 0;
	for (lbref = listtop; lbref != lbstop; lbref = lbref->lb_next)
	{
	    lbstop = listtop;
	    calmaOutI4(lbref->lb_start.p_x * calmaPaintScale, f);
	    calmaOutI4(lbref->lb_start.p_y * calmaPaintScale, f);
	    chkcount++;
	}
	calmaOutI4(listtop->lb_start.p_x * calmaPaintScale, f);
	calmaOutI4(listtop->lb_start.p_y * calmaPaintScale, f);

	if (chkcount != bounds->bt_points)
	    TxError("Points recorded=%d;  Points written=%d\n",
			bounds->bt_points, chkcount);

	/* End of element */
	calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);

#ifdef DEBUG
	/* Diagnostic: report the contents of the list */
	TxPrintf("Polygon path (%d points):\n", bounds->bt_points);

	listtop = bounds->bt_first;
	lbstop = NULL;

	for (lbref = listtop; lbref != lbstop; lbref = lbref->lb_next)
	{
	    if (lbref != listtop)
		TxPrintf("->");
	    else 
		lbstop = listtop;

	    switch(lbref->lb_type)
	    {
		case LB_EXTERNAL:
		    TxPrintf("(%d %d)", lbref->lb_start.p_x, lbref->lb_start.p_y);
		    break;
		case LB_INTERNAL:
		    TxPrintf("[[%d %d]]", lbref->lb_start.p_x, lbref->lb_start.p_y);
		    break;
		case LB_INIT:
		    TxPrintf("XXXXX");
		    break;
	    }
	}
	TxPrintf("\n\n");
#endif

	/* Free the LinkedBoundary list */

	lbref = listtop;
	while (lbref->lb_next != listtop)
	{
	    freeMagic(lbref);
	    lbref = lbref->lb_next;
	}
	freeMagic(lbref);
    }

    /* Free the BoundaryTop list */

    for (bounds = blist; bounds != NULL; bounds = bounds->bt_next)
	freeMagic(bounds);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaMergePaintFunc --
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the disk file.
 *
 * ----------------------------------------------------------------------------
 */

int
calmaMergePaintFunc(tile, cos)
    Tile *tile;			/* Tile to be written out. */
    calmaOutputStruct *cos;	/* Information needed by algorithm */
{
    FILE *f = cos->f;
    Rect *clipArea = cos->area;
    Tile *t, *tp;
    TileType ttype;
    int i, llx, lly, urx, ury, intedges, num_points, split_type;
    bool is_ext;
    static Stack *SegStack = (Stack *)NULL;

    static LinkedBoundary *edge;
    LinkedBoundary *lb;
    BoundaryTop *bounds = NULL;

    /* Quick check for tiles that have already been processed */
    if (tile->ti_client == (ClientData)GDS_PROCESSED) return 0;

    if (SegStack == (Stack *)NULL)
	SegStack = StackNew(64);

    PUSHTILE(tile);
    while (!StackEmpty(SegStack))
    {
	t = (Tile *) STACKPOP(SegStack);
	if (t->ti_client != (ClientData)GDS_PENDING) continue;
	t->ti_client = (ClientData)GDS_PROCESSED;

	split_type = -1;
	if (IsSplit(t))
	{
	    /* If we use SplitSide, then we need to set it when	the	*/
	    /* tile is pushed.  Since these are one-or-zero mask layers	*/
	    /* I assume it is okay to just check which side is TT_SPACE	*/

	    /* split_type = (SplitSide(t) << 1) | SplitDirection(t); */
	    split_type = SplitDirection(t);
	    if (TiGetLeftType(t) == TT_SPACE) split_type |= 2;
	    num_points = 2;
	    if (edge != NULL)
	    {
		/* Remove one point from the edge record for rectangles */
		/* and relink the last entry back to the new head.	*/

		lb = edge;
		while (lb->lb_next != edge) lb = lb->lb_next;
		lb->lb_next = edge->lb_next;
	 	freeMagic(edge);
		edge = edge->lb_next;
	    }
	}
	else
	    num_points = 3;

	/* Create a new linked boundary structure with 4 unknown edges.	*/
	/* This structure is reused when we encounter isolated tiles,	*/
	/* so we avoid unnecessary overhead in the case of, for		*/
	/* example, large contact cut arrays.				*/

	if (edge == NULL)
	{
	    edge = (LinkedBoundary *)mallocMagic(sizeof(LinkedBoundary));
	    lb = edge;

	    for (i = 0; i < num_points; i++)
	    {
		lb->lb_type = LB_INIT;
		lb->lb_next = (LinkedBoundary *)mallocMagic(sizeof(LinkedBoundary));
		lb = lb->lb_next;
	    }
	    lb->lb_type = LB_INIT;
	    lb->lb_next = edge;
	}

	lb = edge;
	llx = LEFT(t);
	lly = BOTTOM(t);
	urx = RIGHT(t);
	ury = TOP(t);
	intedges = 0;

	/* Initialize the "edge" record with the corner points of the	*/
	/* tile.							*/

	if (IsSplit(t))
	{
	    switch (split_type)
	    {
		case 0x0:
		    lb->lb_start.p_x = urx;
		    lb->lb_start.p_y = ury;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = llx;
		    lb->lb_start.p_y = ury;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = llx;
		    lb->lb_start.p_y = lly;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    break;
		case 0x1:
		    lb->lb_start.p_x = llx;
		    lb->lb_start.p_y = ury;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = llx;
		    lb->lb_start.p_y = lly;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = urx;
		    lb->lb_start.p_y = lly;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    break;
		case 0x2:
		    lb->lb_start.p_x = urx;
		    lb->lb_start.p_y = ury;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = llx;
		    lb->lb_start.p_y = lly;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = urx;
		    lb->lb_start.p_y = lly;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    break;
		case 0x3:
		    lb->lb_start.p_x = urx;
		    lb->lb_start.p_y = ury;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = llx;
		    lb->lb_start.p_y = ury;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    lb->lb_start.p_x = urx;
		    lb->lb_start.p_y = lly;
		    lb->lb_type = LB_INIT;
		    lb = lb->lb_next;
		    break;
	    }
	    num_points = 1;
	}
	else
	{
	    lb->lb_start.p_x = urx;
	    lb->lb_start.p_y = ury;
	    lb->lb_type = LB_INIT;
	    lb = lb->lb_next;
	    lb->lb_start.p_x = llx;
	    lb->lb_start.p_y = ury;
	    lb->lb_type = LB_INIT;
	    lb = lb->lb_next;
	    lb->lb_start.p_x = llx;
	    lb->lb_start.p_y = lly;
	    lb->lb_type = LB_INIT;
	    lb = lb->lb_next;
	    lb->lb_start.p_x = urx;
	    lb->lb_start.p_y = lly;
	    lb->lb_type = LB_INIT;
	    lb = lb->lb_next;

	    num_points = 0;
	}
	if (split_type == 0x1) goto left_search;

	/* Search the tile boundary for connected and unconnected tiles.	*/
	/* Generate segments in a counterclockwise cycle.			*/

	if (split_type == 0x2)
	{
	    intedges += calmaAddSegment(&lb, TRUE, RIGHT(t), TOP(t),
			LEFT(t), BOTTOM(t));
	    goto bottom_search;
	}

	/* Search top */

	ttype = TiGetTopType(t);
	for (tp = RT(t); RIGHT(tp) > LEFT(t); tp = BL(tp), num_points++)
	{
	    is_ext = (TiGetBottomType(tp) != ttype) ? TRUE : FALSE;
	    intedges += calmaAddSegment(&lb, is_ext,
			MIN(RIGHT(t), RIGHT(tp)), TOP(t),
			MAX(LEFT(t), LEFT(tp)), TOP(t));
	    if (!is_ext) PUSHTILE(tp);
	}

	if (split_type == 0x3)
	{
	    intedges += calmaAddSegment(&lb, TRUE, LEFT(t), TOP(t),
			RIGHT(t), BOTTOM(t));
	    goto right_search;
	}

	/* Search left */

left_search:
	ttype = TiGetLeftType(t);
	for (tp = BL(t); BOTTOM(tp) < TOP(t); tp = RT(tp), num_points++)
	{
	    is_ext = (TiGetRightType(tp) != ttype) ? TRUE : FALSE;
	    intedges += calmaAddSegment(&lb, is_ext,
			LEFT(t), MIN(TOP(t), TOP(tp)),
			LEFT(t), MAX(BOTTOM(t), BOTTOM(tp)));
	    if (!is_ext) PUSHTILE(tp);
	}

	if (split_type == 0x0)
	{
	    intedges += calmaAddSegment(&lb, TRUE, LEFT(t), BOTTOM(t),
			RIGHT(t), TOP(t));
	    goto done_searches;
	}

	/* Search bottom */

bottom_search:
	ttype = TiGetBottomType(t);
	for (tp = LB(t); LEFT(tp) < RIGHT(t); tp = TR(tp), num_points++)
	{
	    is_ext = (TiGetTopType(tp) != ttype) ? TRUE : FALSE;
	    intedges += calmaAddSegment(&lb, is_ext,
			MAX(LEFT(t), LEFT(tp)), BOTTOM(t),
			MIN(RIGHT(t), RIGHT(tp)), BOTTOM(t));
	    if (!is_ext) PUSHTILE(tp);
	}

	if (split_type == 0x1)
	{
	    intedges += calmaAddSegment(&lb, TRUE, RIGHT(t), BOTTOM(t),
			LEFT(t), TOP(t));
	    goto done_searches;
	}
	/* Search right */

right_search:
	ttype = TiGetRightType(t);
	for (tp = TR(t); TOP(tp) > BOTTOM(t); tp = LB(tp), num_points++)
	{
	    is_ext = (TiGetLeftType(tp) != ttype) ? TRUE : FALSE;
	    intedges += calmaAddSegment(&lb, is_ext,
			RIGHT(t), MAX(BOTTOM(t), BOTTOM(tp)),
			RIGHT(t), MIN(TOP(t), TOP(tp)));
	    if (!is_ext) PUSHTILE(tp);
	}

	/* If tile is isolated, process it now and we're done */

done_searches:
	if (intedges == 0)
	{
	    calmaWritePaintFunc(t, cos);

	    /* Although calmaWritePaintFunc is called only on isolated	*/
	    /* tiles, we may have expanded it.  This could use a LOT of	*/
	    /* optimizing.  1) remove colinear points in calmaAddSegment */
	    /* when both subsegments are external paths, and 2) here,	*/
	    /* take the shortest path to making "edge" exactly 4 points.*/	
	    /* Note that in non-Manhattan mode, num_points may be 3.	*/

	    if (num_points != 4)
	    {
		for (i = 0; i < num_points; i++)
		{
		    freeMagic(edge);
		    edge = edge->lb_next;
		}
		edge = NULL;
	    }
	    if (!StackEmpty(SegStack))
		TxError("ERROR:  Segment stack is supposed to be empty!\n");
	    else
		return 0;
	}
	else
	{
	    /* Merge boundary into existing record */

	    calmaMergeSegments(edge, &bounds, num_points);
	    edge = NULL;
	}
    }

    /* Remove any degenerate points */
    calmaRemoveDegenerate(bounds);

    /* Remove any colinear points */
    calmaRemoveColinear(bounds);

    /* Output the boundary records */
    calmaProcessBoundary(bounds, cos);

    return 0;	/* Keep the search alive. . . */
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaWritePaintFunc --
 *
 * Filter function used to write out a single paint tile.
 *
 *			**** NOTE ****
 * There are loads of Calma systems out in the world that
 * don't understand CALMA_BOX, so we output CALMA_BOUNDARY
 * even though CALMA_BOX is more appropriate.  Bletch.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the disk file.
 *
 * ----------------------------------------------------------------------------
 */

int
calmaWritePaintFunc(tile, cos)
    Tile *tile;			/* Tile to be written out. */
    calmaOutputStruct *cos;	/* File for output and clipping area */
{
    FILE *f = cos->f;
    Rect *clipArea = cos->area;
    Rect r, r2;

    TiToRect(tile, &r);
    if (clipArea != NULL)
	GeoClip(&r, clipArea);

    r.r_xbot *= calmaPaintScale;
    r.r_ybot *= calmaPaintScale;
    r.r_xtop *= calmaPaintScale;
    r.r_ytop *= calmaPaintScale;

    /* Boundary */
    calmaOutRH(4, CALMA_BOUNDARY, CALMA_NODATA, f);

    /* Layer */
    calmaOutRH(6, CALMA_LAYER, CALMA_I2, f);
    calmaOutI2(calmaPaintLayerNumber, f);

    /* Data type */
    calmaOutRH(6, CALMA_DATATYPE, CALMA_I2, f);
    calmaOutI2(calmaPaintLayerType, f);

    /* The inefficient use of CALMA_BOUNDARY for rectangles actually	*/
    /* makes it easy to implement triangles, since they must be defined */
    /* by CALMA_BOUNDARY.						*/

    if (IsSplit(tile))
    {
	/* Coordinates */
	calmaOutRH(36, CALMA_XY, CALMA_I4, f);

	switch ((SplitSide(tile) << 1) | SplitDirection(tile))
	{
	    case 0x0:
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
		calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ytop, f);
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
		break;
	    case 0x1:
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
		calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ybot, f);
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
		break;
	    case 0x2:
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
		calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ybot, f);
		calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ytop, f);
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
		break;
	    case 0x3:
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
		calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ytop, f);
		calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ybot, f);
		calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
		break;
	}
    }
    else
    {
	/* Coordinates */
	calmaOutRH(44, CALMA_XY, CALMA_I4, f);
	calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
	calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ybot, f);
	calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ytop, f);
	calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
	calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
    }

    /* End of element */
    calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaWriteLabelFunc --
 *
 * Output a single label to the stream file 'f'.
 *
 * The CIF type to which this label is attached is 'type'; if this
 * is < 0 then the label is not output.
 *
 * Non-point labels are collapsed to point labels located at the center
 * of the original label.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaWriteLabelFunc(lab, type, f)
    Label *lab;	/* Label to output */
    int type;	/* CIF layer number, or -1 if not attached to a layer */
    FILE *f;	/* Stream file */
{
    Point p;
    int calmanum;

    if (type < 0)
	return;

    calmanum = CIFCurStyle->cs_layers[type]->cl_calmanum;
    if (!CalmaIsValidLayer(calmanum))
	return;

    calmaOutRH(4, CALMA_TEXT, CALMA_NODATA, f);

    calmaOutRH(6, CALMA_LAYER, CALMA_I2, f);
    calmaOutI2(calmanum, f);

    calmaOutRH(6, CALMA_TEXTTYPE, CALMA_I2, f);
    calmaOutI2(CIFCurStyle->cs_layers[type]->cl_calmatype, f);

    if (lab->lab_font >= 0)
    {
	unsigned short textpres = 0;

	/* A bit of a hack here.  Magic can have any number of fonts,	*/
	/* but GDS only allows four of them.  So we just crop the font	*/
	/* index to two bits.  We provide no other font information, so	*/
	/* this is highly implementation-dependent.  But it allows us	*/
	/* to retain font information when reading and writing our own	*/
	/* GDS files.							*/

	textpres = (lab->lab_font & 0x03) << 4;

	switch(lab->lab_just)
	{
	    case GEO_SOUTH:
		textpres |= 0x0001;
		break;
	    case GEO_SOUTHEAST:
		textpres |= 0x0000;
		break;
	    case GEO_EAST:
		textpres |= 0x0004;
		break;
	    case GEO_NORTHEAST:
		textpres |= 0x0008;
		break;
	    case GEO_NORTH:
		textpres |= 0x0009;
		break;
	    case GEO_NORTHWEST:
		textpres |= 0x000a;
		break;
	    case GEO_WEST:
		textpres |= 0x0006;
		break;
	    case GEO_SOUTHWEST:
		textpres |= 0x0002;
		break;
	    case GEO_CENTER:
		textpres |= 0x0005;
		break;
	}

	calmaOutRH(6, CALMA_PRESENTATION, CALMA_BITARRAY, f);
	calmaOutI2(textpres, f);

	calmaOutRH(6, CALMA_STRANS, CALMA_BITARRAY, f);
	calmaOutI2(0, f);	/* Any need for these bits? */

	calmaOutRH(12, CALMA_MAG, CALMA_R8, f);
	calmaOutR8(((double)lab->lab_size / 800)
		* (double)CIFCurStyle->cs_scaleFactor
		/ (double)CIFCurStyle->cs_expander, f);

	if (lab->lab_rotate != 0)
	{
	    calmaOutRH(12, CALMA_ANGLE, CALMA_R8, f);
	    calmaOutR8((double)lab->lab_rotate, f);
	}
    }

    p.p_x = (lab->lab_rect.r_xbot + lab->lab_rect.r_xtop) * calmaWriteScale / 2;
    p.p_y = (lab->lab_rect.r_ybot + lab->lab_rect.r_ytop) * calmaWriteScale / 2;
    calmaOutRH(12, CALMA_XY, CALMA_I4, f);
    calmaOutI4(p.p_x, f);
    calmaOutI4(p.p_y, f);

    /* Text of label */
    calmaOutStringRecord(CALMA_STRING, lab->lab_text, f);

    /* End of element */
    calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaOutHeader --
 *
 * Output the header description for a Calma file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOutHeader(rootDef, f)
    CellDef *rootDef;
    FILE *f;
{
    static double useru = 0.001;
    static double mum = 1.0e-9;

    /* GDS II version 3.0 */
    calmaOutRH(6, CALMA_HEADER, CALMA_I2, f);
    calmaOutI2(3, f);

    /* Beginning of library */
    calmaOutRH(28, CALMA_BGNLIB, CALMA_I2, f);
    calmaOutDate(rootDef->cd_timestamp, f);
    calmaOutDate(time((time_t *) 0), f);

    /* Library name (name of root cell) */
    calmaOutStructName(CALMA_LIBNAME, rootDef, f);

    /*
     * Units.
     * User units are microns; this is really unimportant.
     *
     * Database units are nanometers, since there are
     * programs that don't understand anything else.  If
     * the database units are *smaller* than nanometers, use
     * the actual database units.  Otherwise, stick with
     * nanometers, because anything larger may not input
     * properly with other software.
     */

    calmaOutRH(20, CALMA_UNITS, CALMA_R8, f);
    if (CIFCurStyle->cs_flags & CWF_ANGSTROMS) useru = 0.0001;
    calmaOutR8(useru, f);	/* User units per database unit */

    if (CIFCurStyle->cs_flags & CWF_ANGSTROMS) mum = 1e-10;
    calmaOutR8(mum, f);		/* Meters per database unit */
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaOutDate --
 *
 * Output a date/time specification to the FILE 'f'.
 * This consists of outputting 6 2-byte quantities,
 * or a total of 12 bytes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOutDate(t, f)
    time_t t;	/* Time (UNIX format) to be output */
    FILE *f;	/* Stream file */
{
    struct tm *datep = localtime(&t);

    calmaOutI2(datep->tm_year, f);
    calmaOutI2(datep->tm_mon+1, f);
    calmaOutI2(datep->tm_mday, f);
    calmaOutI2(datep->tm_hour, f);
    calmaOutI2(datep->tm_min, f);
    calmaOutI2(datep->tm_sec, f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaOutStringRecord --
 *
 * Output a complete string-type record.  The actual record
 * type is given by 'type'.  Up to the first CALMANAMELENGTH characters
 * of the string 'str' are output.  Any characters in 'str'
 * not in the legal Calma stream character set are output as
 * 'X' instead.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOutStringRecord(type, str, f)
    int type;		/* Type of this record (data type is ASCII string) */
    char *str;	/* String to be output (<= CALMANAMELENGTH chars) */
    FILE *f;	/* Stream file */
{
    int len;
    unsigned char c;
    char *table, *locstr, *origstr = NULL;
    char *locstrprv; 	/* Added by BSI */

    if(CIFCurStyle->cs_flags & CWF_PERMISSIVE_LABELS) 
    {
	table = calmaMapTablePermissive;
    } else {
	table = calmaMapTableStrict;
    }

    len = strlen(str);
    locstr = str;

    /*
     * Make sure length is even.
     * Output at most CALMANAMELENGTH characters.
     */
    if (len & 01) len++;
    if (len > CALMANAMELENGTH) len = CALMANAMELENGTH;
    calmaOutI2(len+4, f);	/* Record length */
    (void) putc(type, f);		/* Record type */
    (void) putc(CALMA_ASCII, f);	/* Data type */

    /* Output the string itself */
    while (len--)
    {
	locstrprv = locstr;
	c = (unsigned char) *locstr++;
	if (c == 0) putc('\0', f);
	else
	{
	    if ((c > 127) || (c == 0))
	    {
		TxError("Warning: Unprintable character changed "
			"to \'X\' in label.\n");
		c = 'X';
	    }
	    else
	    {
		if (((unsigned char)table[c] != c) && (origstr == NULL))
		    origstr = StrDup(NULL, str);

		c = table[c];
		locstrprv[0] = c;
	    }
	    if (!CalmaDoLower && islower(c))
		(void) putc(toupper(c), f);
	    else
		(void) putc(c, f);
	}
    }
    if (origstr != NULL)
    {
	TxError("Warning: characters changed in string \'%s\'; "
		"modified string is \'%s\'\n", origstr, str);
	freeMagic(origstr);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaOutR8 --
 *
 * Write an 8-byte Real value in GDS-II format to the output stream
 * The value is passed as a double.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	8-byte value written to output stream FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOutR8(d, f)
    double d;	/* Double value to write to output */
    FILE *f;	/* Stream file */
{
    int c, i, sign, expon;

    /* mantissa must be 64 bits for this routine to work correctly */
    uint64_t mantissa;

    mantissa = 0;
    if (d == 0.0)
    {
	sign = 0;
	expon = 0;
    }
    else
    {
	if (d > 0.0)
	    sign = 0;
	else
	{
	    sign = 1;
	    d = -d;
	}

	expon = 64;
	while (d >= 1.0)
	{
	    d /= 16.0;
	    expon++;
	}
	while (d < 0.0625)
	{
	    d *= 16.0;
	    expon--;
	}

	for (i = 0; i < 64; i++)
	{
	    mantissa <<= 1;
	    if (d >= 0.5)
	    {
		mantissa |= 0x1;
		d -= 0.5;
	    }
	    d *= 2.0;
	}
    }
    c = (sign << 7) | expon;
    (void) putc(c, f);
    for (i = 1; i < 8; i++)
    {
	c = (int)(0xff & (mantissa >> (64 - (8 * i))));
	(void) putc(c, f);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * calmaOut8 --
 *
 * Output 8 bytes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaOut8(str, f)
    char *str;	/* 8-byte string to be output */
    FILE *f;	/* Stream file */
{
    int i;

    for (i = 0; i < 8; i++)
	(void) putc(*str++, f);
}
