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
static const char rcsid[] __attribute__ ((unused)) ="$Header: /usr/cvsroot/magic-8.0/calma/CalmaWrite.c,v 1.8 2010/12/22 16:29:06 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>	/* for random() */
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/types.h>
#include <arpa/inet.h>	/* for htons() */
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/magic_zlib.h"
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
#include "extract/extractInt.h"	/* for LabelList */
#include "utils/main.h"		/* for Path and CellLibPath */
#include "utils/stack.h"

/* C99 compat */
#include "utils/undo.h"
#include "calma/calma.h"

    /* Exports */
bool CalmaDoLibrary = FALSE;	  /* If TRUE, do not output the top level */
bool CalmaDoLabels = TRUE;	  /* If FALSE, don't output labels with GDS-II */
bool CalmaDoLower = TRUE;	  /* If TRUE, allow lowercase labels. */
bool CalmaFlattenArrays = FALSE;  /* If TRUE, output arrays as individual uses */
bool CalmaAddendum = FALSE;	  /* If TRUE, do not output readonly cell defs */
time_t *CalmaDateStamp = NULL;	  /* If non-NULL, output this for creation date stamp */
bool CalmaAllowUndefined = FALSE; /* If TRUE, allow calls to undefined cells */
bool CalmaAllowAbstract = FALSE;  /* If TRUE, allow abstract views to be written */

    /* Experimental stuff---not thoroughly tested.  In particular, the 	*/
    /* array generation of CalmaContactArrays is different from that	*/
    /* used by the cifSquareFunc(), and should be using the 		*/
    /* cifFillSquaresArea() function directly, and include slots.	*/

bool CalmaContactArrays = FALSE; /* If TRUE, output contacts as subcell arrays */
bool CalmaMergeTiles = FALSE;	 /* If TRUE, merge tiles into polygons in output. */

#ifdef HAVE_ZLIB
int  CalmaCompression = 0;	/* Output file compression level (0 = uncompressed) */
#endif

/* Structure used by calmaWritePaintFunc() */

typedef struct {
   FILE *f;		/* File stream for output		*/
   const Rect *area;	/* Clipping area, in GDS coordinates	*/
   int type;		/* Layer index				*/
} calmaOutputStruct;

    /* Forward declarations */
extern int calmaWriteInitFunc(CellDef *def);
extern int calmaWritePaintFunc(Tile *tile, calmaOutputStruct *cos);
extern int calmaMergePaintFunc(Tile *tile, calmaOutputStruct *cos);
extern int calmaWriteUseFunc(CellUse *use, FILE *f);
extern int calmaPaintLabelFunc(Tile *tile, calmaOutputStruct *cos);
extern void calmaWriteContacts(FILE *f);
extern void calmaDelContacts(void);
extern void calmaOutFunc(CellDef *def, FILE *f, const Rect *cliprect);
extern void calmaOutStructName(int type, CellDef *def, FILE *f);
extern void calmaWriteLabelFunc(Label *lab, int ltype, int type, FILE *f);
extern void calmaOutHeader(CellDef *rootDef, FILE *f);
extern void calmaOutDate(time_t t, FILE *f);
extern void calmaOutStringRecord(int type, char *str, FILE *f);
extern void calmaOut8(const char *str, FILE *f);
extern void calmaOutR8(double d, FILE *f);
extern void calmaProcessBoundary(BoundaryTop *blist, calmaOutputStruct *cos);
extern void calmaRemoveColinear(BoundaryTop *blist);
extern void calmaRemoveDegenerate(BoundaryTop *blist);

/*--------------------------------------------------------------*/
/* Structures used by the tile merging algorithm 		*/
/*--------------------------------------------------------------*/

#define GDS_PENDING	0
#define GDS_UNPROCESSED CLIENTDEFAULT
#define GDS_PROCESSED	1

#define PUSHTILEC(tp) \
    if (TiGetClient(tp) == GDS_UNPROCESSED) { \
	TiSetClientINT(tp, GDS_PENDING); \
	STACKPUSH((ClientData) (tp), SegStack); \
    }

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

/*
 * Hash table used to determine which GDS libraries have been output
 */
HashTable calmaLibHash;
HashTable calmaPrefixHash;
HashTable calmaUndefHash;


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

static const char calmaMapTableStrict[] =
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

static const char calmaMapTablePermissive[] =
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
CalmaWrite(
    CellDef *rootDef,	/* Pointer to CellDef to be written */
    FILE *f)		/* Open output file */
{
    int oldCount = DBWFeedbackCount, problems, nerr;
    bool good;
    CellDef *err_def;
    CellUse dummy;
    HashEntry *he;
    HashSearch hs;

    /*
     * Do not attempt to write anything if a CIF/GDS output style
     * has not been specified in the technology file.
     */
    if (!CIFCurStyle)
    {
	TxError("No CIF/GDS output style set!\n");
	return FALSE;
    }

    HashInit(&calmaLibHash, 32, 0);
    HashInit(&calmaPrefixHash, 32, 0);
    HashInit(&calmaUndefHash, 32, 0);

    /*
     * Make sure that the entire hierarchy rooted at rootDef is
     * read into memory and that timestamp mismatches are resolved
     * (this is needed so that we know that bounding boxes are OK).
     */

    dummy.cu_def = rootDef;
    err_def = DBCellReadArea(&dummy, &rootDef->cd_bbox, !CalmaAllowUndefined);
    if (err_def != NULL)
    {
	TxError("Failure to read entire subtree of the cell.\n");
	TxError("Failed on cell %s.\n", err_def->cd_name);
	return FALSE;
    }

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
    calmaProcessDef(rootDef, f, CalmaDoLibrary);

    /*
     * Check for any cells that were instanced in the output definition
     * (by dumping a GDS file from a read-only view) but were never
     * defined (because the dumped GDS contained undefined references).
     * If these are in the database but were not part of the tree of
     * rootDef, then output them at the end.
     */
    HashStartSearch(&hs);
    while ((he = HashNext(&calmaUndefHash, &hs)) != NULL)
    {
	char *refname = (char *)HashGetValue(he);
        if (refname && (refname[0] == '0'))
	{
	    CellDef *extraDef;

	    extraDef = DBCellLookDef((char *)he->h_key.h_name);
	    if (extraDef != NULL)
		calmaProcessDef(extraDef, f, FALSE);
	    else
	    	TxError("Error:  Cell %s is not defined in the output file!\n",
				refname + 1);
	}
    }

    /* Finish up by outputting the end-of-library marker */
    calmaOutRH(4, CALMA_ENDLIB, CALMA_NODATA, f);
    fflush(f);
    good = !ferror(f);

    /* See if any problems occurred */
    if ((problems = (DBWFeedbackCount - oldCount)))
	TxPrintf("%d problems occurred.  See feedback entries.\n", problems);

    /*
     * Destroy all contact cell definitions
     */
    if (CalmaContactArrays) calmaDelContacts();

    HashFreeKill(&calmaLibHash);
    HashKill(&calmaPrefixHash);
    HashFreeKill(&calmaUndefHash);
    return (good);
}


/*
 * ----------------------------------------------------------------------------
 *
 * calmaDumpStructure --
 *
 * Parse a structure (cell) from the GDS file.  Check the name against the
 * existing database and modify the name in case of a collision.  Then dump
 * the entire cell verbatim.  The cell gets prefixed with the name "prefix"
 * to prevent collisions with other unknown GDS files that may be dumped
 * verbatim.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaDumpStructure(
    CellDef *def,
    FILE *outf,
    HashTable *calmaDefHash,
    char *filename)
{
    int nbytes = -1, rtype = 0;
    char *strname = NULL, *newnameptr;
    HashEntry *he, *he2;
    CellDef *edef;
    char *prefix = NULL;

    /* Make sure this is a structure; if not, let the caller know we're done */
    PEEKRH(nbytes, rtype);
    if (nbytes <= 0) return (FALSE);

    if (rtype != CALMA_BGNSTR)
    {
	calmaOutRH(nbytes, rtype, CALMA_I2, outf);
        return (FALSE);
    }

    /* Read the structure name */
    if (!calmaSkipExact(CALMA_BGNSTR)) goto syntaxerror;
    if (!calmaReadStringRecord(CALMA_STRNAME, &strname)) goto syntaxerror;
    TxPrintf("Reading \"%s\".\n", strname);

    /* Output structure begin */
    calmaOutRH(28, CALMA_BGNSTR, CALMA_I2, outf);
    if (CalmaDateStamp != NULL)
    	calmaOutDate(*CalmaDateStamp, outf);
    else
    	calmaOutDate(def->cd_timestamp, outf);
    calmaOutDate(time((time_t *) 0), outf);

    /* Do a quick check of the calmaUndefHash table to see if this cell	*/
    /* was previously used in a GDS file that does not define it (a GDS	*/
    /* addendum library).						*/

    he = HashLookOnly(&calmaUndefHash, strname);
    if (he != NULL)
    {
	HashSearch hs;
	char *undefname = (char *)HashGetValue(he);

    	HashStartSearch(&hs);
    	while ((he2 = HashNext(&calmaLibHash, &hs)) != NULL)
	{
	    prefix = (char *)HashGetValue(he2);
	    if (!strncmp(prefix, undefname + 1, strlen(prefix)))
		break;
	}
	if (he2 == NULL)
	{
	    prefix = (char *)NULL;
	    TxError("Error:  Unreferenced cell %s prefix is unrecorded!\n",
			undefname);
	} 
	else
	{
	    /* Remove this entry from the hash table */
	    freeMagic(undefname);
	    HashRemove(&calmaUndefHash, strname);
	}
    }
    else
    {
    	/* Find the structure's unique prefix, in case structure calls	*/
    	/* subcells that are not yet defined.				*/

    	he2 = HashFind(&calmaLibHash, filename);
    	if (he2 == NULL)
	    TxError("Fatal error:  Library %s not recorded!\n", filename);
    	else
	    prefix = (char *)HashGetValue(he2);
    }

    /* Prefix structure name with def name, and output new structure name */
    he = HashFind(calmaDefHash, strname);
    if ((newnameptr = (char *)HashGetValue(he)) != NULL)
    {
	/* Structure is defined more than once */
	if (*newnameptr != '0')
	    TxError("Structure %s defined redundantly in GDS\n", strname);
	else
	    *newnameptr = '1';
	/* To be considered:  Should the structure be output more than once? */
	calmaOutStringRecord(CALMA_STRNAME, newnameptr + 1, outf);
    }
    else if (!strcmp(strname, def->cd_name))
    {
	/* This is the top level cell being defined.  Its name	*/
	/* does not get modified.				*/

	newnameptr = mallocMagic(strlen(strname) + 2);
	sprintf(newnameptr, "1%s", strname);
	calmaOutStringRecord(CALMA_STRNAME, newnameptr + 1, outf);
	HashSetValue(he, (char *)newnameptr);
    }
    else
    {
	/* Check if the cellname is in the magic cell database.	*/
	/* If so, check if that cell is an abstract view and	*/
	/* calls the same library.  If so, the name does not	*/
	/* get prefixed.  Otherwise, the cell is limited to the	*/
	/* GDS library being read, and so takes the prefix.	*/

	/* Modify the cellname by prefixing with "prefix", which is a	*/
	/* unique identifier for the library.				*/

	/* Check if the cell is defined in the database */
	edef = DBCellLookDef(strname);
	if (edef != NULL)
	{
	    bool isAbstract, isReadOnly;
	    char *chklibname, *filenamesubbed = NULL;

	    /* Is view abstract? */
	    DBPropGet(edef, "LEFview", &isAbstract);
	    chklibname = (char *)DBPropGet(edef, "GDS_FILE", &isReadOnly);

	    if (isAbstract && isReadOnly)
	    {
		filenamesubbed = StrDup(NULL, filename);
		DBPathSubstitute(filename, filenamesubbed, edef);
	    }

	    /* Is the library name the same as the filename? */
	    if (isAbstract && isReadOnly && !strcmp(filenamesubbed, chklibname))
	    {
		/* Same library, so keep the cellname and mark the cell */
		/* as having been written to GDS.			*/

		newnameptr = mallocMagic(strlen(strname) + 2);
		sprintf(newnameptr, "1%s", strname);
		HashSetValue(he, (char *)newnameptr);
	    }
	    else
	    {
		/* Find the unique library prefix and prepend it to the cell name */

		if (prefix == NULL)
		    newnameptr = strname;   /* Should never happen */
		else
		{
		    newnameptr = mallocMagic(strlen(strname) + strlen(prefix) + 8);
		    sprintf(newnameptr, "1%s%s", prefix, strname);
		    HashSetValue(he, (char *)newnameptr);
		}
	    }
	    if (filenamesubbed) freeMagic(filenamesubbed);
	}
	else
	{
	    /* Find the unique library prefix and prepend it to the cell name */

	    if (prefix == NULL)
		newnameptr = strname;	    /* Should never happen */
	    else
	    {
		newnameptr = mallocMagic(strlen(strname) + strlen(prefix) + 8);
		sprintf(newnameptr, "1%s%s", prefix, strname);
		HashSetValue(he, (char *)newnameptr);
	    }
	}
	calmaOutStringRecord(CALMA_STRNAME, newnameptr + 1, outf);
    }
    freeMagic(strname);

    /* Read and output the structure until CALMA_ENDSTR, except */
    /* for handling any AREF or SREF names, which need name	*/
    /* checks.							*/

    while (1)
    {
	int datatype;

	READI2(nbytes);
	if (FEOF(calmaInputFile))
	{
	    /* Unexpected end-of-file */
	    FSEEK(calmaInputFile, -(CALMAHEADERLENGTH), SEEK_END);
	    break;
	}
	rtype = FGETC(calmaInputFile);
	datatype = FGETC(calmaInputFile);
	switch (rtype) {
	    case CALMA_BGNSTR:
		UNREADRH(nbytes, rtype);
		return (TRUE);
	    case CALMA_ENDLIB:
		UNREADRH(nbytes, rtype);
		return (FALSE);

	    case CALMA_SNAME:
		UNREADRH(nbytes, rtype);
		if (!calmaReadStringRecord(CALMA_SNAME, &strname))
		    goto syntaxerror;

		he = HashFind(calmaDefHash, strname);
		newnameptr = (char *)HashGetValue(he);
		if (newnameptr != NULL)
		{
		    calmaOutStringRecord(CALMA_SNAME, newnameptr + 1, outf);
		}
		else
		{
		    TxError("Diagnostic:  %s is a forward reference?\n", strname);

		    /* Could be a forward reference, so do a rename in	*/
		    /* the same way used for structure definitions.	*/

		    newnameptr = (char *)mallocMagic(strlen(strname) +
				strlen(prefix) + 8);
		    sprintf(newnameptr, "0%s%s", prefix, strname);

		    edef = DBCellLookDef(newnameptr + 1);
		    if (edef != NULL)
			sprintf(newnameptr, "0%s%s[[0]]", prefix, strname);
		    HashSetValue(he, (char *)newnameptr);
		    calmaOutStringRecord(CALMA_SNAME, newnameptr + 1, outf);
		}
		break;

	    default:
		calmaOutRH(nbytes, rtype, datatype, outf);
		nbytes -= 4;

		/* Copy nbytes from input to output */
		while (nbytes-- > 0)
		{
		    int byte;
		    if ((byte = FGETC(calmaInputFile)) < 0)
		    {
			TxError("End of file with %d bytes remaining to be read.\n",
				nbytes);
			while (nbytes-- > 0)
			    putc(0, outf);	// zero-pad output
			return (FALSE);
		    }
		    else
			putc(byte, outf);
		}
		break;
	}
    }
    return (FALSE);

syntaxerror:
    /* Syntax error: skip to CALMA_ENDSTR */
    calmaSkipTo(CALMA_ENDSTR);
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaFullDump --
 *
 * Read in a GDS-II stream format library and dump its contents to
 * file "outf" verbatim except for cell references, which are renamed
 * if there is a conflict with a cell def in memory.
 *
 * Because the dump is inside a larger output, the header and trailer
 * are discarded.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaFullDump(
    CellDef *def,
    FILETYPE fi,
    FILE *outf,
    char *filename)
{
    int version, rval;
    char *libname = NULL, *testlib, uniqlibname[4];
    char *sptr, *viewopts;
    bool isAbstract;
    HashTable calmaDefHash;
    HashSearch hs;
    HashEntry *he, *he2;

    static const int hdrSkip[] = { CALMA_FORMAT, CALMA_MASK, CALMA_ENDMASKS,
		CALMA_REFLIBS, CALMA_FONTS, CALMA_ATTRTABLE,
		CALMA_STYPTABLE, CALMA_GENERATIONS, -1 };
    static const int skipBeforeLib[] = { CALMA_LIBDIRSIZE, CALMA_SRFNAME,
		CALMA_LIBSECUR, -1 };

    HashInit(&calmaDefHash, 32, 0);

    cifReadCellDef = def;
    calmaInputFile = fi;

    /* Read and ignore the GDS-II header */

    if (!calmaReadI2Record(CALMA_HEADER, &version)) goto done;
    if (!calmaSkipExact(CALMA_BGNLIB)) goto done;
    calmaSkipSet(skipBeforeLib);
    if (!calmaReadStringRecord(CALMA_LIBNAME, &libname)) goto done;

    calmaSkipSet(hdrSkip);

    // CALMA_UNITS needs to be parsed to determine if units in the
    // input file are compatible with units being used in the output
    // file.
    if (calmaParseUnits() == FALSE)
    {
	TxError("Error:  Library %s has incompatible database units!\n", libname);
	return;
    }

    // Record the GDS library so it will not be processed again.
    he = HashFind(&calmaLibHash, filename);
    if ((char *)HashGetValue(he) != NULL)
    {
    	TxPrintf("Library %s has already been processed\n", libname);
	return;
    }

    /* If property LEFview is defined as "no_prefix" instead of "TRUE",
     * then do not create a unique prefix for subcells.  This is generally
     * ill-advised, but can be needed if a foundry runs specific DRC checks
     * on specific cell names, in which case adding a prefix can cause DRC
     * errors to appear.  It is then incumbent on the user to ensure that
     * names in the GDS file do not shadow any names in the database.
     */

    viewopts = (char *)DBPropGet(def, "LEFview", &isAbstract);
    if ((!isAbstract) || (strcasecmp(viewopts, "no_prefix")))
    {
	/* Generate a SHORT name for this cell (else it is easy to run into the
	 * GDS 32-character cellname limit).  Save it in the hash record.  The
	 * chance of generating the same prefix for a library that has items
	 * with conflicting names is vanishingly small, but to be pedantic, store
	 * the prefix in a hash table and check to make sure that uniqueness is
	 * ensured.  NOTE:  The first character of a SPICE name cannot be a
	 * number.  Therefore the first character is alphabetical, and the second
	 * is alphanumeric.  There are only 936 possible combinations, but this
	 * is only meant to distinguish cells in large IP blocks of unknown
	 * origin, of which only a limited number would be expected.  Beware
	 * the implications for LVS, as the prefixed names from layout would
	 * need to be compared to un-prefixed names from another netlist.
	 */
	while (TRUE)
	{
	    rval = random() % 26;
	    rval = 'A' + rval;
	    uniqlibname[0] = (char)(rval & 127);
	    rval = random() % 36;
	    rval = (rval < 26) ? ('A' + rval) : ('0' + rval - 26);
	    uniqlibname[1] = (char)(rval & 127);
	    uniqlibname[2] = '_';
	    uniqlibname[3] = '\0';
	    he2 = HashLookOnly(&calmaPrefixHash, uniqlibname);
	    if (he2 == NULL)
	    {
		he2 = HashFind(&calmaPrefixHash, uniqlibname);
		break;
	    }
	}
	HashSetValue(he, StrDup(NULL, uniqlibname));
    }
    else
	HashSetValue(he, StrDup(NULL, ""));

    while (calmaDumpStructure(def, outf, &calmaDefHash, filename))
	if (SigInterruptPending)
	    goto done;
    calmaSkipExact(CALMA_ENDLIB);

done:

    /* Check that all references were resolved.  If not, then it is
     * probably because a library was an "addendum"-type library
     * referencing things in other libraries.  Move those cell
     * references to the calmaUndefHash before killing calmaDefHash.
     */

    HashStartSearch(&hs);
    while ((he = HashNext(&calmaDefHash, &hs)) != NULL)
    {
	char *refname = (char *)HashGetValue(he);
        if (refname[0] == '0')
	{
	    he2 = HashFind(&calmaUndefHash, (char *)he->h_key.h_name);
	    HashSetValue(he2, StrDup(NULL, refname));
	}
    }

    HashFreeKill(&calmaDefHash);
    if (libname != NULL) freeMagic(libname);
    return;
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
calmaWriteInitFunc(
    CellDef *def)
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
calmaProcessUse(
    CellUse *use,	/* Process use->cu_def */
    FILE *outf)		/* Stream file */
{
    return (calmaProcessDef(use->cu_def, outf, FALSE));
}

int
calmaProcessDef(
    CellDef *def,	/* Output this def's children, then the def itself */
    FILE *outf,		/* Stream file */
    bool do_library)	/* If TRUE, output only children of def, but not def */
{
    char *filename;
    int polyidx;
    bool isReadOnly, oldStyle, hasContent, isAbstract, hasGDSEnd, needHier;
    bool hierWrite, arrayWrite;
    HashEntry *he;

    /* Skip if already output */
    if ((int) CD2INT(def->cd_client) > 0)
	return (0);

    /* Assign it a (negative) number if it doesn't have one yet */
    if ((int) CD2INT(def->cd_client) == 0)
	def->cd_client = INT2CD(calmaCellNum--);

    /* Mark this cell */
    def->cd_client = INT2CD(- (int) CD2INT(def->cd_client));

    /* Read the cell in if it is not already available. */
    if ((def->cd_flags & CDAVAILABLE) == 0)
	if (!DBCellRead(def, TRUE, TRUE, NULL))
	    return (0);

    /*
     * Flag an error if attempting to write the default (UNNAMED) cell
     * into GDS.  This is not strictly an error but is almost certainly
     * not what the user intended.
     */

    if (!strcmp(def->cd_name, UNNAMED))
    {
	TxError("Error:  Cell has the default name \"%s\"!\n", UNNAMED);
    }

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

    DBPropGet(def, "LEFview", &isAbstract);
    DBPropGet(def, "GDS_START", &hasContent);
    DBPropGet(def, "GDS_END", &hasGDSEnd);
    DBPropGet(def, "CIFhier", &needHier);

    filename = (char *)DBPropGet(def, "GDS_FILE", &isReadOnly);

    /* When used with "calma addendum true", don't output the read-only	*/
    /* cells.  This makes the library incomplete and dependent on the	*/
    /* vendor libraries, so use with caution.				*/

    if (isReadOnly && hasContent && CalmaAddendum) return (0);

    if (isAbstract && !isReadOnly)
    {
	if (CalmaAllowAbstract)
	    TxError("Warning:  Writing abstract view of \"%s\" to GDS.\n",
			def->cd_name);
	else
	{
	    TxError("Error:  Cell \"%s\" is an abstract view;  cannot write GDS.\n",
			def->cd_name);
	    return 1;
	}
    }
		
    /*
     * Output the definitions for any of our descendants that have
     * not already been output.  Numbers are assigned to the subcells
     * as they are output.  If the cell will get a "full dump" (by
     * having GDS_START but no GDS_END), then do not output any subcells,
     * as they are expected to be in the referenced GDS file.
     */
    if (!hasContent || hasGDSEnd)
    {
	int result;

	if (needHier)
	{
	    hierWrite = CIFHierWriteDisable;
	    arrayWrite = CIFArrayWriteDisable;

	    CIFHierWriteDisable = FALSE;
	    CIFArrayWriteDisable = FALSE;
	}

	result = DBCellEnum(def, calmaProcessUse, (ClientData) outf);

	if (needHier)
	{
	    CIFHierWriteDisable = hierWrite;
	    CIFArrayWriteDisable = arrayWrite;
	}

	if (result != 0)
	    return 1;
    }

    if (isReadOnly && hasContent)
    {
	char *buffer, *offptr, *retfilename;
	size_t defsize, numbytes;
	off_t cellstart, cellend, structstart;
 	dlong cval;
	int namelen;
	FILETYPE fi;

	/* Give some feedback to the user */
	TxPrintf("   Copying output for cell %s from %s\n", def->cd_name, filename);

	/* Use PaZOpen() so the paths searched are the same as were	*/
	/* searched to find the .mag file that indicated this GDS file.	*/

	fi = PaZOpen(filename, "r", "", Path, CellLibPath, &retfilename);

	/* Check if file may have been compressed */
	if (fi == NULL)
	    fi = PaZOpen(filename, "r", ".gz", Path, CellLibPath, &retfilename);

	if (fi == NULL)
	{
	    /* This is a rare error, but if the subcell is inside	*/
	    /* another vendor GDS, it would not normally be output.	*/

	    DBPropGet((def->cd_parents->cu_parent == NULL) ? def :
			def->cd_parents->cu_parent, "GDS_FILE", &isReadOnly);
	    if (isReadOnly)
	    {
		def->cd_flags |= CDVENDORGDS;
		return 0;	/* Ignore without raising an error */
	    }

	    TxError("Calma output error:  Can't find GDS file \"%s\" "
				"for vendor cell \"%s\".  It will not be output.\n",
				filename, def->cd_name);

	    if (CalmaAllowUndefined)
		return 0;
	    else
		return 1;
	}
	else if (isAbstract || (!hasGDSEnd))
	{
	    /* This is the trickiest part.  If the cell view is abstract then	*/
	    /* the cell view has no hierarchy, and there is no way to descend	*/
	    /* into the cell hierarchy and discover and write out all the	*/
	    /* dependencies.  The dependencies are in the file but not in the	*/
	    /* range GDS_START to GDS_END.  Furthermore, the dependencies have	*/
	    /* not been loaded so naming conflicts may exist.  So the file must	*/
	    /* be read end-to-end and parsed carefully.				*/

	    he = HashLookOnly(&calmaLibHash, retfilename);
	    if (he == NULL)
		calmaFullDump(def, fi, outf, retfilename);

	    FCLOSE(fi);
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
    		if (CalmaDateStamp != NULL)
		    calmaOutDate(*CalmaDateStamp, outf);
		else
		    calmaOutDate(def->cd_timestamp, outf);
		calmaOutDate(time((time_t *) 0), outf);

		/* Name structure the same as the magic cellname */
		calmaOutStructName(CALMA_STRNAME, def, outf);
	    }

	    sscanf(offptr, "%"DLONG_PREFIX"d", &cval);
	    cellstart = (off_t)cval;

	    /* GDS_START has been defined as the start of data after the cell	*/
	    /* structure name.  However, a sanity check is wise, so move back	*/
	    /* that far and make sure the structure name is there.		*/
	    structstart = cellstart - (off_t)(strlen(def->cd_name));
	    if (strlen(def->cd_name) & 0x1) structstart--;
	    structstart -= 2;

	    FSEEK(fi, structstart, SEEK_SET);

	    /* Read the structure name and check against the CellDef name */
	    defsize = (size_t)(cellstart - structstart);
	    buffer = (char *)mallocMagic(defsize + 1);
	    numbytes = magicFREAD(buffer, sizeof(char), (size_t)defsize, fi);
	    if (numbytes == defsize)
	    {
		buffer[defsize] = '\0';
		if (buffer[0] != 0x06 || buffer[1] != 0x06)
		{
		    TxError("Calma output error:  Structure name not found"
				" at GDS file position %"DLONG_PREFIX"d\n", cellstart);
		    TxError("Calma output error:  Can't write cell from vendor GDS."
				"  Using magic's internal definition\n");
		    isReadOnly = FALSE;
		}
		else if (strcmp(&buffer[2], def->cd_name))
		{
		    TxError("Calma output warning:  Structure definition has name"
				" %s but cell definition has name %s.\n",
				&buffer[2], def->cd_name);
		    TxError("The structure definition will be given the cell name.\n");
		}
	    }
	    else
	    {
		TxError("Calma output error:  Can't read cell from vendor GDS."
				"  Using magic's internal definition\n");
		isReadOnly = FALSE;
	    }
	    freeMagic(buffer);

	    if (cellend < cellstart)	/* Sanity check */
	    {
		TxError("Calma output error:  Bad vendor GDS file reference!\n");
		isReadOnly = FALSE;
	    }
	    else if (isReadOnly)
	    {
		/* Important note:  mallocMagic() is limited to size integer.	*/
		/* This will fail on a structure larger than ~2GB.		*/

		defsize = (size_t)(cellend - cellstart);
		buffer = (char *)mallocMagic(defsize);

		numbytes = magicFREAD(buffer, sizeof(char), (size_t)defsize, fi);

		if (numbytes == defsize)
		{
		    /* Sanity check:  buffer must end with a structure	*/
		    /* definition end (record 0x07).			*/

		    if (buffer[defsize - 4] != 0x00 ||
				buffer[defsize - 3] != 0x04 ||
				buffer[defsize - 2] != 0x07 ||
				buffer[defsize - 1] != 0x00)
		    {
			TxError("Calma output error:  Structure end definition not found"
				" at GDS file position %"DLONG_PREFIX"d\n", cellend);
			TxError("Calma output error:  Can't write cell from vendor GDS."
				"  Using magic's internal definition\n");
			isReadOnly = FALSE;
		    }
		    else
		    {
		    	numbytes = fwrite(buffer, sizeof(char), (size_t)defsize, outf);
		    	if (numbytes <= 0)
		    	{
			    TxError("Calma output error:  Can't write cell from "
					"vendor GDS.  Using magic's internal "
					"definition\n");
			    isReadOnly = FALSE;
		    	}
		    }
		}
		else
		{
		    TxError("Calma output error:  Can't read cell from vendor GDS."
				"  Using magic's internal definition\n");

		    /* Additional information as to why data did not match */
		    TxError("Size of data requested: %"DLONG_PREFIX"d", defsize);
		    TxError("Length of data read: %"DLONG_PREFIX"d", numbytes);
		    isReadOnly = FALSE;
		}
		freeMagic(buffer);
	    }
	    FCLOSE(fi);

	    /* Mark the definition as vendor GDS so that magic doesn't	*/
	    /* try to generate subcell interaction or array interaction	*/
	    /* paint for it.						*/

	    def->cd_flags |= CDVENDORGDS;
	}
    }
    else
    {
	/* Give some feedback to the user */
	TxPrintf("   Generating output for cell %s\n", def->cd_name);
    }

    /* Quick check on "polygonXXXXX" cells---these are generated by the	*/
    /* "gds polygon subcell" option, and if the parent cell is a vendor	*/
    /* GDS file, then these cells do not actually exist in the layout	*/
    /* and should not be output.					*/

    if (isReadOnly == FALSE)
	if (!strncmp(def->cd_name, "polygon", 7))
            if (sscanf(def->cd_name + 7, "%d", &polyidx) == 1)
        	if (def->cd_parents->cu_parent != NULL)
                    DBPropGet(def->cd_parents->cu_parent, "GDS_FILE", &isReadOnly);

    /* Output this cell definition from the Magic database */
    if (!isReadOnly)
	if (!do_library)
	{
	    if (needHier)
	    {
		hierWrite = CIFHierWriteDisable;
		arrayWrite = CIFArrayWriteDisable;

		CIFHierWriteDisable = FALSE;
		CIFArrayWriteDisable = FALSE;
	    }

	    calmaOutFunc(def, outf, &TiPlaneRect);

	    if (needHier)
	    {
		CIFHierWriteDisable = hierWrite;
		CIFArrayWriteDisable = arrayWrite;
	    }
	}

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * compport ---
 *
 *	Compare two port labels by port index.  Sorting function used with
 *	qsort().
 *
 * ----------------------------------------------------------------------------
 */

int
compport(const void *one, const void *two)
{
    PortLabel *pl1 = (PortLabel *)one;
    PortLabel *pl2 = (PortLabel *)two;

    if (pl1->pl_port < pl2->pl_port)
	return -1;
    if (pl1->pl_port > pl2->pl_port)
	return 1;
    else
	return 0;
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
calmaOutFunc(
    CellDef *def,	/* Pointer to cell def to be written */
    FILE *f,		/* Open output file */
    const Rect *cliprect)/* Area to clip to (used for contact cells),
			 * in CIF/GDS coordinates.
			 */
{
    Label *lab;
    CIFLayer *layer;
    Rect bigArea;
    int type;
    int dbunits;
    calmaOutputStruct cos;
    bool propfound;
    char *propvalue;

    cos.f = f;
    cos.area = (cliprect == &TiPlaneRect) ? NULL : cliprect;
    cos.type = -1;

    /* Output structure begin */
    calmaOutRH(28, CALMA_BGNSTR, CALMA_I2, f);
    if (CalmaDateStamp != NULL)
    	calmaOutDate(*CalmaDateStamp, f);
    else
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

    /* Include any fixed bounding box as part of the area to process,	*/
    /* in case the fixed bounding box is larger than the geometry.	*/
    propvalue = (char *)DBPropGet(def, "FIXED_BBOX", &propfound);
    if (propfound)
    {
	Rect bbox;

        if (sscanf(propvalue, "%d %d %d %d", &bbox.r_xbot, &bbox.r_ybot,
                &bbox.r_xtop, &bbox.r_ytop) == 4)
	    GeoInclude(&bbox, &bigArea);
    }

    CIFErrorDef = def;
    CIFGen(def, def, &bigArea, CIFPlanes, &DBAllTypeBits, TRUE, TRUE, FALSE,
		(ClientData)f);

    if (!CIFHierWriteDisable)
	CIFGenSubcells(def, &bigArea, CIFPlanes);
    if (!CIFArrayWriteDisable)
	CIFGenArrays(def, &bigArea, CIFPlanes);

    for (type = 0; type < CIFCurStyle->cs_nLayers; type++)
    {
	layer = CIFCurStyle->cs_layers[type];
	if (layer->cl_flags & CIF_TEMP) continue;
	if (!CalmaIsValidLayer(layer->cl_calmanum)) continue;
	cos.type = type;
	calmaPaintLayerNumber = layer->cl_calmanum;
	calmaPaintLayerType = layer->cl_calmatype;

	if (layer->cl_flags & CIF_LABEL)
	    DBSrPaintArea((Tile *) NULL, CIFPlanes[type],
		    cliprect, &CIFSolidBits, calmaPaintLabelFunc,
		    (ClientData) &cos);
	else
	    DBSrPaintArea((Tile *) NULL, CIFPlanes[type],
		    cliprect, &CIFSolidBits, (CalmaMergeTiles) ?
		    calmaMergePaintFunc : calmaWritePaintFunc,
		    (ClientData) &cos);
    }

    /* Output labels.  Do this in two passes, first for non-port labels	*/
    /* while finding the highest-numbered port.  Then output the port	*/
    /* labels (if any) in the order of the port index.			*/

    if (CalmaDoLabels)
    {
	int i, ltype, numports = 0;
	LabelList *ll = NULL, *newll;

	for (lab = def->cd_labels; lab; lab = lab->lab_next)
	{
	    if ((lab->lab_flags & PORT_DIR_MASK) == 0)
	    {
	    	ltype = CIFCurStyle->cs_labelLayer[lab->lab_type];
	    	type = ltype;
		calmaWriteLabelFunc(lab, ltype, type, f);
	    }
	    else
	    {
		newll = (LabelList *)mallocMagic(sizeof(LabelList));
		newll->ll_label = lab;
		newll->ll_attr = (unsigned int)lab->lab_port;
		newll->ll_next = ll;
		ll = newll;
		numports++;
	    }
	}
	if (ll != NULL)
	{
	    /* Turn linked list into an array, then run qsort on it	*/
	    /* to sort by port number.					*/

	    PortLabel *pllist = (PortLabel *)mallocMagic(numports * sizeof(PortLabel));
	    i = 0;
	    while (ll != NULL)
	    {
		pllist[i].pl_label = ll->ll_label;
		pllist[i].pl_port = (unsigned int)ll->ll_attr;
		freeMagic(ll);
		ll = ll->ll_next;
		i++;
	    }	

	    qsort(pllist, numports, sizeof(PortLabel), compport);

	    for (i = 0; i < numports; i++)
	    {
		lab = pllist[i].pl_label;
		ltype = CIFCurStyle->cs_portText[lab->lab_type];
		type = CIFCurStyle->cs_portLayer[lab->lab_type];
		if (type >= 0)
		    calmaWriteLabelFunc(lab, ltype, type, f);
	    }
	    freeMagic(pllist);
	}
    }

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
calmaIsUseNameDefault(
    char *defName,
    char *useName)
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
calmaWriteUseFunc(
    CellUse *use,
    FILE *f)
{
    /*
     * r90, r180, and r270 are Calma 8-byte real representations
     * of the angles 90, 180, and 270 degrees.  Because there are
     * only 4 possible values, it is faster to have them pre-computed
     * than to format with calmaOutR8().
     */
    static const unsigned char r90[] = { 0x42, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const unsigned char r180[] = { 0x42, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const unsigned char r270[] = { 0x43, 0x10, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00 };
    const unsigned char *whichangle;
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
		    calmaOut8((char *)whichangle, f);
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
	    calmaOut8((char *)whichangle, f);
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
	/* (Modified 11/11/2022:  Do this always, not just the non-default case) */

	calmaOutRH(6, CALMA_PROPATTR, CALMA_I2, f);
	calmaOutI2(CALMA_PROP_USENAME_STD, f);
	calmaOutStringRecord(CALMA_PROPVALUE, use->cu_id, f);

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
calmaOutStructName(
    int type,
    CellDef *def,
    FILE *f)
{
    char *defname;
    unsigned char c;
    char *cp;
    int calmanum;
    const char *table;

    if (CIFCurStyle->cs_flags & CWF_PERMISSIVE_LABELS)
    {
	table = calmaMapTablePermissive;
    } else {
	table = calmaMapTableStrict;
    }

    /* Is the def name a legal Calma name? */
    for (cp = def->cd_name; (c = (unsigned char) *cp); cp++)
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
    if ((!(CIFCurStyle->cs_flags & CWF_STRING_LIMIT)) ||
	    (cp <= def->cd_name + CALMANAMELENGTH))
    {
	/* Yes, it's legal: use it */
	defname = StrDup(NULL, def->cd_name);
    }
    else
    {
	/* Bad name: use XXXXXcalmaNum */
bad:
	calmanum = (int) CD2INT(def->cd_client);
	if (calmanum < 0) calmanum = -calmanum;
	defname = (char *)mallocMagic(32);
	(void) sprintf(defname, "XXXXX%d", calmanum);
	TxError("Warning: string in output unprintable; changed to \'%s\'\n",
		 defname);
    }

    calmaOutStringRecord(type, defname, f);
    freeMagic(defname);
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
calmaGetContactCell(
    TileType type,		/* magic contact tile type */
    bool lookOnly)		/* if true, don't generate any new cells */
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
	def = DBCellNewDef(contactCellName);
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
CalmaGenerateArray(
    FILE *f,		/* GDS output file */
    TileType type,	/* Magic tile type of contact */
    int llx,
    int lly,		/* Lower-left hand coordinate of the array
			 * (centered on contact cut)
			 */
    int pitch,		/* Pitch of the array elements */
    int cols,
    int rows)		/* Number of array elements in X and Y */
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
calmaWriteContacts(
    FILE *f)
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
calmaDelContacts(void)
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
calmaAddSegment(
    LinkedBoundary **lbptr,
    bool poly_edge,
    int p1x,
    int p1y,
    int p2x,
    int p2y)
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
calmaRemoveDegenerate(
    BoundaryTop *blist)
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
calmaRemoveColinear(
    BoundaryTop *blist)
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
calmaMergeSegments(
    LinkedBoundary *edge,
    BoundaryTop **blist,
    int num_points)
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
calmaProcessBoundary(
    BoundaryTop *blist,
    calmaOutputStruct *cos)
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
	if (listtop != NULL)
	{
	    calmaOutI4(listtop->lb_start.p_x * calmaPaintScale, f);
	    calmaOutI4(listtop->lb_start.p_y * calmaPaintScale, f);
	}

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
calmaMergePaintFunc(
    Tile *tile,			/* Tile to be written out. */
    calmaOutputStruct *cos)	/* Information needed by algorithm */
{
    FILE *f = cos->f;
    const Rect *clipArea = cos->area;
    Tile *t, *tp;
    TileType ttype;
    int i, llx, lly, urx, ury, intedges, num_points, split_type;
    bool is_ext;
    static Stack *SegStack = (Stack *)NULL;

    static LinkedBoundary *edge;
    LinkedBoundary *lb;
    BoundaryTop *bounds = NULL;

    /* Quick check for tiles that have already been processed */
    if (TiGetClientINT(tile) == GDS_PROCESSED) return 0;

    if (SegStack == (Stack *)NULL)
	SegStack = StackNew(64);

    PUSHTILEC(tile);
    while (!StackEmpty(SegStack))
    {
	t = (Tile *) STACKPOP(SegStack);
	if (TiGetClientINT(t) != GDS_PENDING) continue;
	TiSetClientINT(t, GDS_PROCESSED);

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
	    if (!is_ext) PUSHTILEC(tp);
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
	    if (!is_ext) PUSHTILEC(tp);
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
	    if (!is_ext) PUSHTILEC(tp);
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
	    if (!is_ext) PUSHTILEC(tp);
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
calmaWritePaintFunc(
    Tile *tile,			/* Tile to be written out. */
    calmaOutputStruct *cos)	/* File for output and clipping area */
{
    FILE *f = cos->f;
    const Rect *clipArea = cos->area;
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
calmaWriteLabelFunc(
    Label *lab,	/* Label to output */
    int ltype,	/* CIF layer number to use for TEXT record */
    int type,	/* CIF layer number to use for BOUNDARY record,
		 * or -1 if not attached to a layer
		 */
    FILE *f)	/* Stream file */
{
    Point p;
    int calmanum, calmatype;

    if (ltype < 0)
	return;

    calmanum = CIFCurStyle->cs_layers[ltype]->cl_calmanum;
    if (!CalmaIsValidLayer(calmanum))
	return;

    calmaOutRH(4, CALMA_TEXT, CALMA_NODATA, f);

    calmaOutRH(6, CALMA_LAYER, CALMA_I2, f);
    calmaOutI2(calmanum, f);

    calmatype = CIFCurStyle->cs_layers[ltype]->cl_calmatype;
    calmaOutRH(6, CALMA_TEXTTYPE, CALMA_I2, f);
    calmaOutI2(calmatype, f);

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

    /* If the cifoutput layer is for labels only (has no operators),	*/
    /* and the label rectangle is not degenerate, then output the label	*/
    /* rectangle as a boundary with the label's layer:purpose pair.	*/

    if (type < 0)
	return;

    calmanum = CIFCurStyle->cs_layers[type]->cl_calmanum;
    if (!CalmaIsValidLayer(calmanum))
	return;

    calmatype = CIFCurStyle->cs_layers[type]->cl_calmatype;

    /* Note that the check for whether the CIF_LABEL_NOPORT flag has	*/
    /* been set is done outside of this routine.			*/

    if ((CIFCurStyle->cs_layers[type]->cl_ops == NULL) &&
		(lab->lab_rect.r_xtop > lab->lab_rect.r_xbot) &&
		(lab->lab_rect.r_ytop > lab->lab_rect.r_ybot))
    {
	Rect r;

	r = lab->lab_rect;
	r.r_xbot *= calmaWriteScale;
	r.r_ybot *= calmaWriteScale;
	r.r_xtop *= calmaWriteScale;
	r.r_ytop *= calmaWriteScale;

	/* Boundary */
	calmaOutRH(4, CALMA_BOUNDARY, CALMA_NODATA, f);

	/* Layer */
	calmaOutRH(6, CALMA_LAYER, CALMA_I2, f);
	calmaOutI2(calmanum, f);

	/* Data type */
	calmaOutRH(6, CALMA_DATATYPE, CALMA_I2, f);
	calmaOutI2(calmatype, f);

	/* Coordinates */
	calmaOutRH(44, CALMA_XY, CALMA_I4, f);
	calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);
	calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ybot, f);
	calmaOutI4(r.r_xtop, f); calmaOutI4(r.r_ytop, f);
	calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ytop, f);
	calmaOutI4(r.r_xbot, f); calmaOutI4(r.r_ybot, f);

	/* End of element */
	calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaPaintLabelFunc --
 *
 * Filter function used to write out a single label corresponding to the
 * area of a paint tile, and having a text matching the CIF layer name.
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
calmaPaintLabelFunc(
    Tile *tile,			/* Tile contains area for label. */
    calmaOutputStruct *cos)	/* File for output and clipping area */
{
    FILE *f = cos->f;
    const Rect *clipArea = cos->area;
    Rect r, r2;
    Point p;
    int len;
    CIFLayer *layer = CIFCurStyle->cs_layers[cos->type];

    if (IsSplit(tile)) return 0;    /* Ignore non-Manhattan geometry */

    if (!CalmaIsValidLayer(layer->cl_calmanum))
	return 0;

    TiToRect(tile, &r);

    if (clipArea != NULL)
	GeoClip(&r, clipArea);

    calmaOutRH(4, CALMA_TEXT, CALMA_NODATA, f);

    calmaOutRH(6, CALMA_LAYER, CALMA_I2, f);
    calmaOutI2(layer->cl_calmanum, f);

    calmaOutRH(6, CALMA_TEXTTYPE, CALMA_I2, f);
    calmaOutI2(layer->cl_calmatype, f);

    p.p_x = (r.r_xbot + r.r_xtop) * calmaPaintScale / 2;
    p.p_y = (r.r_ybot + r.r_ytop) * calmaPaintScale / 2;
    calmaOutRH(12, CALMA_XY, CALMA_I4, f);
    calmaOutI4(p.p_x, f);
    calmaOutI4(p.p_y, f);

    /* Text of label is the CIF layer name */
    calmaOutStringRecord(CALMA_STRING, layer->cl_name, f);

    /* End of element */
    calmaOutRH(4, CALMA_ENDEL, CALMA_NODATA, f);

    return 0;
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
calmaOutHeader(
    CellDef *rootDef,
    FILE *f)
{
    static double useru = 0.001;
    static double mum = 1.0e-9;

    /* GDS II version 3.0 */
    calmaOutRH(6, CALMA_HEADER, CALMA_I2, f);
    calmaOutI2(3, f);

    /* Beginning of library */
    calmaOutRH(28, CALMA_BGNLIB, CALMA_I2, f);
    if (CalmaDateStamp != NULL)
    	calmaOutDate(*CalmaDateStamp, f);
    else
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
calmaOutDate(
    time_t t,	/* Time (UNIX format) to be output */
    FILE *f)	/* Stream file */
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
calmaOutStringRecord(
    int type,		/* Type of this record (data type is ASCII string) */
    char *str,	/* String to be output */
    FILE *f)	/* Stream file */
{
    int len;
    unsigned char c;
    const char *table;
    char *locstr, *origstr = NULL;
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
     * If the name is longer than CALMANAMELENGTH, then output the
     * last CALMANAMELENGTH characters (since cell names are more
     * likely to be unique in the last characters than in the first
     * characters).
     *
     * NOTE:  GDS format has not used CALMANAMELENGTH restrictions
     * for ages.  Since this is a 2-byte record, then is it not
     * worth checking the 65536 - 4 character limit.  The CALMANAMELENGTH
     * restriction must be enabled in the cifoutput flags.
     */


    if (len & 01) len++;
    if ((CIFCurStyle->cs_flags & CWF_STRING_LIMIT) && (len > CALMANAMELENGTH))
    {
	TxError("Warning:  Cellname %s truncated ", str);
	TxError("to %s (GDS format limit)\n", str + len - CALMANAMELENGTH);
	locstr = str + len - CALMANAMELENGTH;
	len = CALMANAMELENGTH;
    }
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
calmaOutR8(
    double d,	/* Double value to write to output */
    FILE *f)	/* Stream file */
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
calmaOut8(
    const char *str,	/* 8-byte string to be output */
    FILE *f)	/* Stream file */
{
    int i;

    for (i = 0; i < 8; i++)
	(void) putc(*str++, f);
}
