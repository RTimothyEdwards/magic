/*
 * lefRead.c --      
 *
 * This module incorporates the LEF/DEF format for standard-cell place and
 * route.
 *
 * Version 0.1 (September 26, 2003):  LEF input handling.  Includes creation
 * of cells from macro statements, handling of pins, ports, obstructions, and
 * associated geometry.
 *
 * Note that techfile compatibility requires that each layer name appearing
 * in the LEF file should be present as an alias for the appropriate magic
 * tile type in the technology file.  Layer names are not case sensitive.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/lef/lefRead.c,v 1.2 2008/12/17 18:40:04 tim Exp $";            
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/time.h>
#include <math.h>		/* for roundf() function, if std=c99 */

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/undo.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "cif/cif.h"
#include "cif/CIFint.h"		/* Access to CIFCurStyle. . . */
#include "lef/lefInt.h"

/* ---------------------------------------------------------------------*/

/* Current line number for reading */
int lefCurrentLine;

/* Cell reading hash tables */
HashTable LefCellTable;
HashTable lefDefInitHash;

/*
 *------------------------------------------------------------
 *
 * LefEstimate --
 *
 *	Estimate the time to completion based on the time,
 *	the total number of items to process, and the number
 *	of items processed so far.  Attempts to report about
 *	every 5 seconds or so.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	May print to output.
 *
 *------------------------------------------------------------
 */

#define PRINT_INTERVAL 5	/* print status at 4 second intervals */

void
LefEstimate(processed, total, item_name)
   int processed;
   int total;
   char *item_name;
{
    static int check_interval, partition;
    static struct timeval tv_start;
    static float last_time;
    struct timeval tv;
    struct timezone tz;
    float cur_time, time_left;

    if (!total) return;

    if (processed == 0)		/* Initialization */
    {
	gettimeofday(&tv_start, &tz);
	GrDisplayStatus = DISPLAY_IN_PROGRESS;
	SigSetTimer(PRINT_INTERVAL);
    }
    else if (processed == total - 1)
    {
	GrDisplayStatus = DISPLAY_IDLE;
	SigRemoveTimer();
    }
    else if (GrDisplayStatus == DISPLAY_BREAK_PENDING)
    {
	gettimeofday(&tv, &tz);
	cur_time = (float)(tv.tv_sec - tv_start.tv_sec)
		+ ((float)(tv.tv_usec - tv_start.tv_usec) / 1.0e6);
	time_left = (((float)total / (float)processed) - 1) * cur_time;

	/* not likely to happen, but we don't want a divide-by-0 error */
	if (cur_time == 0.0) cur_time = 1.0e-6;

	TxPrintf("  Processed %d of %d %s (%2.1f%%).", processed, total,
			item_name, (float)(100 * processed) / (float)total);
	TxPrintf("  Est. time remaining: %2.1fs\n", time_left);
	TxFlushOut();

#ifdef MAGIC_WRAPPER
	/* We need to let Tk paint the console display */
	while (Tcl_DoOneEvent(TCL_DONT_WAIT) != 0);
#endif

	GrDisplayStatus = DISPLAY_IN_PROGRESS;
	SigSetTimer(PRINT_INTERVAL);
    }
}

/* This is the original version, which doesn't use the system itimer	*/
/* and which is not very good at maintaining constant intervals.	*/

#if 0
void
LefEstimate(processed, total, item_name)
   int processed;
   int total;
   char *item_name;
{
    static int check_interval, partition;
    static struct timeval tv_start;
    static float last_time;
    struct timeval tv;
    struct timezone tz;
    float cur_time, time_left;

    if (!total) return;

    if (processed == 0)		/* Initialization */
    {
	GrDisplayStatus = DISPLAY_IN_PROGRESS;

	check_interval = 100;
	gettimeofday(&tv_start, &tz);
	last_time = 0.0;
    }
	
    if (processed > check_interval)
    {
	gettimeofday(&tv, &tz);
	cur_time = (float)(tv.tv_sec - tv_start.tv_sec)
		+ ((float)(tv.tv_usec - tv_start.tv_usec) / 1.0e6);
	time_left = (((float)total / (float)processed) - 1) * cur_time;

	/* not likely to happen, but we don't want a divide-by-0 error */
	if (cur_time == 0.0) cur_time = 1.0e-6;

	partition = (int)((float)processed * (float)PRINT_INTERVAL / cur_time);

	/* partition is never less than 1 nor greater than 5% of the total */
	if (partition == 0) partition = 1;
	if (partition > (total / 20)) partition = (total / 20);

	check_interval += partition;

	/* Don't print anything in intervals faster than 1 second */
	if ((cur_time - last_time) < 1.0) return;
	last_time = cur_time;

	TxPrintf("  Processed %d of %d %s (%2.1f%%).", processed, total,
			item_name, (float)(100 * processed) / (float)total);
	TxPrintf("  Est. time remaining: %2.1fs\n", time_left);
	TxFlushOut();
    }
}

#endif /* 0 */


/*
 *------------------------------------------------------------
 *
 * LefNextToken --
 *
 *	Move to the next token in the stream input.
 *	If "ignore_eol" is FALSE, then the end-of-line character
 *	"\n" will be returned as a token when encountered.
 *	Otherwise, end-of-line will be ignored.
 *
 * Results:
 *	Pointer to next token to parse
 *
 * Side Effects:
 *	May read a new line from the specified file.
 *
 * Warnings:
 *	The return result of LefNextToken will be overwritten by
 *	subsequent calls to LefNextToken if more than one line of
 *	input is parsed.
 *
 *------------------------------------------------------------
 */

char *
LefNextToken(f, ignore_eol)
    FILE *f;
    bool ignore_eol;
{
    static char line[LEF_LINE_MAX + 2];	/* input buffer */
    static char *nexttoken = NULL;	/* pointer to next token */
    static char *curtoken;		/* pointer to current token */
    static char eol_token='\n';

    /* Read a new line if necessary */

    if (nexttoken == NULL)
    {
	for(;;)
	{
	    if (fgets(line, LEF_LINE_MAX + 1, f) == NULL) return NULL;
	    lefCurrentLine++;
	    curtoken = line;
	    while (isspace(*curtoken) && (*curtoken != '\n') && (*curtoken != '\0'))
		curtoken++;		/* skip leading whitespace */

	    if ((*curtoken != '#') && (*curtoken != '\n') && (*curtoken != '\0'))
	    {
		nexttoken = curtoken;
		break;
	    }
	}
	if (!ignore_eol)
	    return &eol_token;
    }
    else
	curtoken = nexttoken;

    /* Find the next token; set to NULL if none (end-of-line). */
    /* Treat quoted material as a single token. */

    if (*nexttoken == '\"') {
	nexttoken++;
	while (((*nexttoken != '\"') || (*(nexttoken - 1) == '\\')) &&
		(*nexttoken != '\0')) {
	    if (*nexttoken == '\n') {
		if (fgets(nexttoken + 1, LEF_LINE_MAX -
				(size_t)(nexttoken - line), f) == NULL)
		    return NULL;
	    }
	    nexttoken++;
	}
	if (*nexttoken == '\"')
	    nexttoken++;
    }
    else {
	while (!isspace(*nexttoken) && (*nexttoken != '\0') && (*nexttoken != '\n'))
	    nexttoken++;	/* skip non-whitespace (move past current token) */
    }

    /* Terminate the current token */
    if (*nexttoken != '\0') *nexttoken++ = '\0';

    while (isspace(*nexttoken) && (*nexttoken != '\0') && (*nexttoken != '\n'))
	nexttoken++;	/* skip any whitespace */

    if ((*nexttoken == '#') || (*nexttoken == '\n') || (*nexttoken == '\0'))
	nexttoken = NULL;

    return curtoken;
}

/*
 *------------------------------------------------------------
 *
 * LefError --
 *
 *	Print an error message (via TxError) giving the line
 *	number of the input file on which the error occurred.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Prints to the output (stderr).
 *
 *------------------------------------------------------------
 */

void
LefError(char *fmt, ...)
{  
    static int errors = 0;
    va_list args;

    if (fmt == NULL)  /* Special case:  report any errors and reset */
    {
	if (errors)
	{
	    TxPrintf("LEF Read: encountered %d error%s total.\n", errors,
			(errors == 1) ? "" : "s");
	    errors = 0;
	}
	return;
    }

    if (errors < LEF_MAX_ERRORS)
    {
	TxError("LEF Read, Line %d: ", lefCurrentLine);
	va_start(args, fmt);
	Vfprintf(stderr, fmt, args);
	va_end(args);
	TxFlushErr();
    }
    else if (errors == LEF_MAX_ERRORS)
	TxError("LEF Read:  Further errors will not be reported.\n");

    errors++;
}

/*
 *------------------------------------------------------------
 *
 * LefParseEndStatement --
 *
 *	Check if the string passed in "lineptr" contains the
 *	appropriate matching keyword.  Sections in LEF files
 *	should end with "END (keyword)" or "END".  To check
 *	against the latter case, "match" should be NULL.
 *
 * Results:
 *	1 if the line matches the expected end statement,
 *	0 if not.  Return -1 if the last token read was
 *	"END", indicating to the caller that either an
 *	error has occurred or, in the case of a section
 *	skip, the routine needs to be called again.
 *
 * Side effects:
 *	None.
 *
 *------------------------------------------------------------
 */

int
LefParseEndStatement(f, match)
    FILE *f;
    char *match;
{
    char *token;
    int keyword, words;
    char *match_name[2];

    static char *end_section[] = {
	"END",
	"ENDEXT",
	NULL
    };

    match_name[0] = match;
    match_name[1] = NULL;

    token = LefNextToken(f, (match == NULL) ? FALSE : TRUE);
    if (token == NULL)
    {
	LefError("Bad file read while looking for END statement\n");
	return 0;
    }

    /* END or ENDEXT */
    if ((*token == '\n') && (match == NULL)) return 1;

    /* END <section_name> */
    else {
	keyword = LookupFull(token, match_name);
	if (keyword == 0)
	    return 1;
	else
	{
	    /* Check for END followed by END */
	    keyword = LookupFull(token, end_section);
	    if (keyword == 0)
		return -1;
	    else
		return 0;
	}
    }
}

/*
 *------------------------------------------------------------
 *
 * LefSkipSection --
 *
 *	Skip to the "END" record of a LEF input section
 *	String "section" must follow the "END" statement in
 *	the file to be considered a match;  however, if
 *	section = NULL, then "END" must have no strings
 *	following.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Reads input from the specified file.  Prints an
 *	error message if the expected END record cannot
 *	be found.
 *
 *------------------------------------------------------------
 */

void
LefSkipSection(f, section)
    FILE *f;
    char *section;
{
    char *token;
    int keyword, result;
    static char *end_section[] = {
	"END",
	"ENDEXT",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	if ((keyword = Lookup(token, end_section)) == 0)
	{
	    result = -1;
	    while (result == -1)
	    {
		result = LefParseEndStatement(f, section);
		if (result == 1) return;
	    }
	}
	else if (keyword == 1)
	{
	    if (!strcmp(section, "BEGINEXT"))
		return;
	}
    }

    LefError("Section %s has no END record!\n", section);
    return;
}

/*
 *------------------------------------------------------------
 *
 * lefFindCell --
 *
 *	Search for an existing cell of the given name.  If
 *	it exists, return a pointer to the cell def.  If not,
 *	create a new cell def with the given name and return
 *	a pointer to it.
 *
 *------------------------------------------------------------
 */

CellDef *
lefFindCell(name)
    char *name;		/* Name of the cell to search for */
{
    HashEntry *h;
    CellDef *def;

    h = HashFind(&LefCellTable, name);
    if (HashGetValue(h) == 0)
    {
	def = DBCellLookDef(name);
	if (def == NULL)
	{
	    def = DBCellNewDef(name, (char *)NULL);
	    DBReComputeBbox(def);
	}
	HashSetValue(h, def);
    }
    return (CellDef *) HashGetValue(h);
}

/*
 *------------------------------------------------------------
 *
 * LefLower --
 *
 *	Convert a token in a LEF or DEF file to all-lowercase.
 *
 *------------------------------------------------------------
 */

char *
LefLower(token)
    char *token;
{
    char *tptr;

    for (tptr = token; *tptr != '\0'; tptr++)
	*tptr = tolower(*tptr);

    return token;
}

/*
 *------------------------------------------------------------
 * LefRedefined --
 *
 *	In preparation for redefining a LEF layer, we need
 *	to first check if there are multiple names associated
 *	with the lefLayer entry.  If so, split the entry into
 *	two copies, so that the redefinition doesn't affect
 *	the other LEF names.
 *
 * Results:
 *	Pointer to a lefLayer, which may or may not be the
 *	same one presented to the subroutine.
 *	
 * Side Effects:
 *	May add an entry to the list of LEF layers.
 *
 *------------------------------------------------------------
 */

lefLayer *
LefRedefined(lefl, redefname)
    lefLayer *lefl;
    char *redefname;
{
    lefLayer *slef, *newlefl;
    char *altName;
    LinkedRect *viaLR;
    HashSearch hs;
    HashEntry *he;
    int records;

    /* check if more than one entry points to the same	*/
    /* lefLayer record.	 If so, we will also record the	*/
    /* name of the first type that is not the same as	*/
    /* "redefname".					*/

    records = 0;
    altName = NULL;
    HashStartSearch(&hs);
    while (he = HashNext(&LefInfo, &hs))
    {
	slef = (lefLayer *)HashGetValue(he);
	if (slef == lefl)
	    records++;
	if (altName == NULL)
	    if (strcmp((char *)he->h_key.h_name, redefname))
		altName = (char *)he->h_key.h_name;
    }
    if (records == 1)
    {
	/* Only one name associated with the record, so	*/
	/* just clear all the allocated information.	*/

	for (viaLR = lefl->info.via.lr; viaLR != NULL; viaLR = viaLR->r_next)
	    freeMagic(viaLR);
	newlefl = lefl;
    }
    else
    {
	he = HashFind(&LefInfo, redefname);
	newlefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
	newlefl->refCnt = 1;
	newlefl->canonName = (char *)he->h_key.h_name;
	HashSetValue(he, newlefl);

	/* If the canonical name of the original entry	*/
	/* is "redefname", then change it.		*/

	if (!strcmp(lefl->canonName, redefname))
	    if (altName != NULL)
		lefl->canonName = altName;
    }
    newlefl->type = -1;
    newlefl->obsType = -1;
    newlefl->info.via.area = GeoNullRect;
    newlefl->info.via.cell = (CellDef *)NULL;
    newlefl->info.via.lr = (LinkedRect *)NULL;

    return newlefl;
}

/*
 *------------------------------------------------------------
 * LefReadLayers --
 *
 *	Read a LEF "LAYER" record from the file, and
 *	match the LEF layer to a magic tile type.
 *	If "obstruct" is TRUE, returns the layer mapping
 *	for obstruction geometry as defined in the
 *	technology file (if it exists), and up to two
 *	types are returned (the second in the 3rd argument
 *	pointer).
 *
 * Results:
 *	Returns a magic TileType or -1 if no matching
 *	tile type is found.
 *
 * Side Effects:
 *	Reads input from file f;
 *
 *------------------------------------------------------------
 */

TileType
LefReadLayers(f, obstruct, lreturn, rreturn)
    FILE *f;
    bool obstruct;
    TileType *lreturn;
    Rect **rreturn;
{
    char *token;
    TileType curlayer = -1;
    lefLayer *lefl = NULL;

    token = LefNextToken(f, TRUE);
    if (*token == ';')
    {
	LefError("Bad Layer statement\n");
	return -1;
    }
    else
    {
	HashEntry *he = HashLookOnly(&LefInfo, token);
	if (he)
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (lefl && obstruct)
	    {
		/* Use the obstruction type, if it is defined */
		curlayer = lefl->obsType;
		if ((curlayer < 0) && (lefl->lefClass != CLASS_IGNORE))
		    curlayer = lefl->type;
		else if (lefl->lefClass == CLASS_VIA)
		    if (lreturn) *lreturn = lefl->info.via.obsType;
	    }
	    else if (lefl)
	    {
		if (lefl->lefClass != CLASS_IGNORE)
		    curlayer = lefl->type;
		if (lefl->lefClass == CLASS_VIA)
		    if (rreturn) *rreturn = &(lefl->info.via.area);
	    }

	    if (rreturn)
	    {
		if (lefl->lefClass == CLASS_VIA)
		    *rreturn = &(lefl->info.via.area);
		else
		    *rreturn = &GeoNullRect;
	    }
	}
	else
	{
	    /* fallback: compare name to magic types */

	    curlayer = DBTechNameType(token);
	    if (curlayer < 0)
	    {
		(void) LefLower(token);
		curlayer = DBTechNameType(token);
	    }
	}
	if ((curlayer < 0) && ((!lefl) || (lefl->lefClass != CLASS_IGNORE)))
	{
	    LefError("Don't know how to parse layer \"%s\"\n", token);
	    LefError("Try adding this name to the LEF techfile section\n");
	}
    }
    return curlayer;
}

/*
 *------------------------------------------------------------
 * LefReadLayer --
 *
 *	Read a LEF "LAYER" record from the file, and
 *	match the LEF layer to a magic tile type.
 *	If "obstruct" is TRUE, returns the layer mapping
 *	for obstruction geometry as defined in the
 *	technology file (if it exists).
 *
 * Results:
 *	Returns a magic TileType or -1 if no matching
 *	tile type is found.
 *
 * Side Effects:
 *	Reads input from file f;
 *
 *------------------------------------------------------------
 */

TileType
LefReadLayer(f, obstruct)
    FILE *f;
    bool obstruct;
{
    return LefReadLayers(f, obstruct, (TileType *)NULL, (Rect **)NULL);
}

/*
 *------------------------------------------------------------
 * LefReadRect --
 *
 *	Read a LEF "RECT" record from the file, and
 *	return a Rect in magic coordinates.
 *
 * Results:
 *	Returns a pointer to a Rect containing the magic
 *	coordinates, or NULL if an error occurred.
 *
 * Side Effects:
 *	Reads input from file f;
 *
 * Note:
 *	LEF/DEF does NOT define a RECT record as having (...)
 *	pairs, only routes.  However, at least one DEF file
 *	contains this syntax, so it is checked.
 *
 *------------------------------------------------------------
 */

Rect *
LefReadRect(f, curlayer, oscale)
    FILE *f;
    TileType curlayer;
    float oscale;
{
    char *token;
    float llx, lly, urx, ury;
    static Rect paintrect;
    bool needMatch = FALSE;

    token = LefNextToken(f, TRUE);
    if (*token == '(')
    {
	token = LefNextToken(f, TRUE);
	needMatch = TRUE;
    }
    if (!token || sscanf(token, "%f", &llx) != 1) goto parse_error;
    token = LefNextToken(f, TRUE);
    if (!token || sscanf(token, "%f", &lly) != 1) goto parse_error;
    token = LefNextToken(f, TRUE);
    if (needMatch)
    {
	if (*token != ')') goto parse_error;
	else token = LefNextToken(f, TRUE);
	needMatch = FALSE;
    }
    if (*token == '(')
    {
	token = LefNextToken(f, TRUE);
	needMatch = TRUE;
    }
    if (!token || sscanf(token, "%f", &urx) != 1) goto parse_error;
    token = LefNextToken(f, TRUE);
    if (!token || sscanf(token, "%f", &ury) != 1) goto parse_error;
    if (needMatch)
    {
	token = LefNextToken(f, TRUE);
	if (*token != ')') goto parse_error;
    }
    if (curlayer < 0)
	LefError("No layer defined for RECT.\n");
    else
    {
	/* Scale coordinates (microns to magic internal units)	*/
	/* Need to scale grid if necessary!			*/
		
	paintrect.r_xbot = (int)roundf(llx / oscale);
	paintrect.r_ybot = (int)roundf(lly / oscale);
	paintrect.r_xtop = (int)roundf(urx / oscale);
	paintrect.r_ytop = (int)roundf(ury / oscale);

	/* Diagnostic */
	/*
	TxPrintf("   Painting %s at (%d, %d), (%d, %d) in cell %s.\n",
		DBTypeLongNameTbl[curlayer],
		paintrect.r_xbot, paintrect.r_ybot,
		paintrect.r_xtop, paintrect.r_ytop,
		lefMacro->cd_name);
	*/
    }
    return (&paintrect);

parse_error:
    LefError("Bad port geometry: RECT requires 4 values.\n");
    return (Rect *)NULL;
}

/*
 *------------------------------------------------------------
 * LefReadPolygon --
 *
 *	Read a LEF "POLYGON" record from the file, and
 *	return a linked point structure.  
 *
 * Results:
 *	Returns a pointer to a Rect containing the magic
 *	coordinates, or NULL if an error occurred.
 *
 * Side Effects:
 *	Reads input from file f;
 *
 * Note:
 *	LEF/DEF does NOT define a RECT record as having (...)
 *	pairs, only routes.  However, at least one DEF file
 *	contains this syntax, so it is checked.
 *
 *------------------------------------------------------------
 */

Point *
LefReadPolygon(f, curlayer, oscale, ppoints)
    FILE *f;
    TileType curlayer;
    float oscale;
    int *ppoints;
{
    LinkedRect *lr = NULL, *newRect;
    Point *plist = NULL;
    char *token;
    float px, py;
    int lpoints = 0;

    while (1)
    {
	token = LefNextToken(f, TRUE);
	if (token == NULL || *token == ';') break;
 	if (sscanf(token, "%f", &px) != 1)
	{
	    LefError("Bad X value in polygon.\n");
	    LefEndStatement(f);
	    break;
	}

	token = LefNextToken(f, TRUE);
	if (token == NULL || *token == ';')
	{
	    LefError("Missing Y value in polygon point!\n");
	    break;
	}
	if (sscanf(token, "%f", &py) != 1)
	{
	    LefError("Bad Y value in polygon.\n");
	    LefEndStatement(f);
	    break;
	}

	/* Use the rect structure for convenience;  we only	*/
	/* use the r_ll point of the rect to store each point	*/
	/* as we read it in.					*/

	newRect = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	newRect->r_r.r_xbot = (int)roundf(px / oscale);
	newRect->r_r.r_ybot = (int)roundf(py / oscale);
	newRect->r_next = lr;
	lr = newRect;
	lpoints++;
    }

    *ppoints = lpoints;
    if (lpoints == 0) return NULL;

    /* Convert LinkedRect structure into a simple point list */

    plist = (Point *)mallocMagic(lpoints * sizeof(Point));
    lpoints = 0;
    while (lr != NULL)
    {
	plist[*ppoints - lpoints - 1].p_x = lr->r_r.r_xbot;
	plist[*ppoints - lpoints - 1].p_y = lr->r_r.r_ybot;
	freeMagic(lr);
	lpoints++;
	lr = lr->r_next;
    }	
    return plist;	
}

/*
 *------------------------------------------------------------
 * LefPaintPolygon --
 *
 *	Paint a polygon into the CellDef indicated by lefMacro.
 * 
 *------------------------------------------------------------
 */

LinkedRect *
LefPaintPolygon(lefMacro, pointList, points, curlayer, keep)
    CellDef *lefMacro;
    Point *pointList;
    int points;
    TileType curlayer;
    bool keep;
{
    int pNum;
    PaintUndoInfo ui;
    LinkedRect *rlist = NULL, *rptr;

    ui.pu_def = lefMacro;
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	if (DBPaintOnPlane(curlayer, pNum))
	{
	    ui.pu_pNum = pNum;
	    rlist = PaintPolygon(pointList, points, lefMacro->cd_planes[pNum],
			DBStdPaintTbl(curlayer, pNum), &ui, keep);

	    /* Annotate each rectangle in the list with the type painted */
	    if (keep)
		for (rptr = rlist; rptr; rptr = rptr->r_next)
		    rptr->r_type = curlayer;
	}
    }
    return rlist;
}

/*
 *------------------------------------------------------------
 * LefReadGeometry --
 *
 *	Read Geometry information from a LEF file.
 *	Used for PORT records and OBS statements.
 *
 * Results:
 *	Returns a linked list of all areas and types
 *	painted.  However, if "do_list" is FALSE, then
 *	LefReadGeometry always returns NULL.
 *
 *	Note that do_list is always TRUE for PORT
 *	records and FALSE for OBS statements, and so in
 *	addition to determining what the return value is,
 *	it also determines what layer is returned by
 *	function LefReadLayer().
 *
 * Side Effects:
 *	Reads input from file f;
 *	Paints into the CellDef lefMacro.
 *
 *------------------------------------------------------------
 */

enum lef_geometry_keys {LEF_LAYER = 0, LEF_WIDTH, LEF_PATH,
	LEF_RECT, LEF_POLYGON, LEF_VIA, LEF_GEOMETRY_END};

LinkedRect *
LefReadGeometry(lefMacro, f, oscale, do_list)
    CellDef *lefMacro;
    FILE *f;
    float oscale;
    bool do_list;
{
    TileType curlayer = -1, otherlayer = -1;

    char *token;
    int keyword;
    LinkedRect *newRect, *rectList;
    Point *pointList;
    int points;
    Rect *paintrect, *contact;

    static char *geometry_keys[] = {
	"LAYER",
	"WIDTH",
	"PATH",
	"RECT",
	"POLYGON",
	"VIA",
	"END",
	NULL
    };

    rectList = NULL;

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, geometry_keys);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in LEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case LEF_LAYER:
		curlayer = LefReadLayers(f, (do_list) ? FALSE : TRUE, &otherlayer,
				&contact);
		LefEndStatement(f);
		break;
	    case LEF_WIDTH:
		LefEndStatement(f);
		break;
	    case LEF_PATH:
		LefEndStatement(f);
		break;
	    case LEF_RECT:
		paintrect = (curlayer < 0) ? NULL : LefReadRect(f, curlayer, oscale);
		if (paintrect)
		{
		    /* Paint the area, if a CellDef is defined */
		    if (lefMacro)
		    {
			// Cut layers defined as contacts use the contact
			// dimensions, not the dimension from the LEF file
			if (DBIsContact(curlayer) && !(GEO_RECTNULL(contact)))
			{
			    paintrect->r_xbot = (paintrect->r_xbot + paintrect->r_xtop);
			    paintrect->r_ybot = (paintrect->r_ybot + paintrect->r_ytop);
			    paintrect->r_xtop = paintrect->r_xbot + contact->r_xtop;
			    paintrect->r_ytop = paintrect->r_ybot + contact->r_ytop;
			    paintrect->r_xbot += contact->r_xbot;
			    paintrect->r_ybot += contact->r_ybot;
			    paintrect->r_xbot >>= 1;
			    paintrect->r_ybot >>= 1;
			    paintrect->r_xtop >>= 1;
			    paintrect->r_ytop >>= 1;
			}
			DBPaint(lefMacro, paintrect, curlayer);

			if ((!do_list) && (otherlayer != -1))
				DBPaint(lefMacro, paintrect, otherlayer);
		    }

		    /* Remember the area and layer */
		    if (do_list)
		    {
			newRect = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
			newRect->r_type = curlayer;
			newRect->r_r = *paintrect;
			newRect->r_next = rectList;
			rectList = newRect;
		    }
		}
		LefEndStatement(f);
		break;
	    case LEF_POLYGON:
		pointList = LefReadPolygon(f, curlayer, oscale, &points);
		if (pointList)
		{
		    if (lefMacro)
		    {
			LinkedRect *rectNew, *rectTest;

			rectNew = LefPaintPolygon(lefMacro, pointList, points,
					curlayer, TRUE);

			// Add the new list of rectangles to the current list
			// of rectangles to be made into port labels.

			if (rectList != NULL)
			{
			    for (rectTest = rectList; rectTest && rectTest->r_next;
					rectTest = rectTest->r_next);
			    rectTest->r_next = rectNew;
			}
			else
			    rectList = rectNew;

			if ((!do_list) && (otherlayer != -1))
			    LefPaintPolygon(lefMacro, pointList, points, otherlayer);
		    }
		    freeMagic(pointList);
		}
		break;
	    case LEF_VIA:
		LefEndStatement(f);
		break;
	    case LEF_GEOMETRY_END:
		if (LefParseEndStatement(f, NULL) == 0)
		{
		    LefError("Geometry (PORT or OBS) END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == LEF_GEOMETRY_END) break;
    }
    return rectList;
}

/*
 *------------------------------------------------------------
 * LefReadPort --
 *
 *	A wrapper for LefReadGeometry, which adds a label
 *	to the last rectangle generated by the geometry
 *	parsing.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Reads input from file f;
 *	Paints into the CellDef lefMacro.
 *
 *------------------------------------------------------------
 */

void
LefReadPort(lefMacro, f, pinName, pinNum, pinDir, pinUse, oscale)
    CellDef *lefMacro;
    FILE *f;
    char *pinName;
    int pinNum, pinDir, pinUse;
    float oscale;
{
    Label *newlab;
    LinkedRect *rectList;

    rectList = LefReadGeometry(lefMacro, f, oscale, TRUE);

    while (rectList != NULL)
    {
	if (pinNum >= 0)
	{
	    /* Label this area */
	    DBPutLabel(lefMacro, &rectList->r_r, -1, pinName, rectList->r_type, 0);

	    /* Set this label to be a port */

	    if (lefMacro->cd_labels == NULL)
		LefError("Internal error: No labels in cell!\n");
	    else
	    {
		newlab = lefMacro->cd_lastLabel;
		if (strcmp(newlab->lab_text, pinName))
		    LefError("Internal error:  Can't find the label!\n");
		else /* Make this a port */
		    newlab->lab_flags = pinNum | pinUse | pinDir | PORT_DIR_MASK;
	    }
	    /* DBAdjustLabels(lefMacro, &rectList->area); */
	}

	freeMagic((char *)rectList);
	rectList = rectList->r_next;
    }
}

/*
 *------------------------------------------------------------
 * LefReadPin --
 *
 *	Read a PIN statement from a LEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Reads input from file f;
 *	Paints into the CellDef lefMacro.
 *
 *------------------------------------------------------------
 */

enum lef_pin_keys {LEF_DIRECTION = 0, LEF_USE, LEF_PORT, LEF_CAPACITANCE,
	LEF_PIN_END};

void
LefReadPin(lefMacro, f, pinname, pinNum, oscale)
   CellDef *lefMacro;
   FILE *f;
   char *pinname;
   int pinNum;
   float oscale;
{
    char *token;
    int keyword, subkey;
    int pinDir = PORT_CLASS_DEFAULT;
    int pinUse = PORT_USE_DEFAULT;

    static char *pin_keys[] = {
	"DIRECTION",
	"USE",
	"PORT",
	"CAPACITANCE",
	"END",
	NULL
    };

    static char *pin_classes[] = {
	"DEFAULT",
	"INPUT",
	"OUTPUT",
	"OUTPUT TRISTATE",
	"INOUT",
	"FEEDTHRU",
	NULL
    };

    static int lef_class_to_bitmask[] = {
	PORT_CLASS_DEFAULT,
	PORT_CLASS_INPUT,
	PORT_CLASS_OUTPUT,
	PORT_CLASS_TRISTATE,
	PORT_CLASS_BIDIRECTIONAL,
	PORT_CLASS_FEEDTHROUGH
    };

    static char *pin_uses[] = {
	"DEFAULT",
	"SIGNAL",
	"ANALOG",
	"POWER",
	"GROUND",
	"CLOCK",
	NULL
    };

    static int lef_use_to_bitmask[] = {
	PORT_USE_DEFAULT,
	PORT_USE_SIGNAL,
	PORT_USE_ANALOG,
	PORT_USE_POWER,
	PORT_USE_GROUND,
	PORT_USE_CLOCK
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, pin_keys);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in LEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case LEF_DIRECTION:
		token = LefNextToken(f, TRUE);
		subkey = Lookup(token, pin_classes);
		if (subkey < 0)
		    LefError("Improper DIRECTION statement\n");
		else
		    pinDir = lef_class_to_bitmask[subkey];
		LefEndStatement(f);
		break;
	    case LEF_USE:
		token = LefNextToken(f, TRUE);
		subkey = Lookup(token, pin_uses);
		if (subkey < 0)
		    LefError("Improper USE statement\n");
		else
		    pinUse = lef_use_to_bitmask[subkey];
		LefEndStatement(f);
		break;
	    case LEF_PORT:
		LefReadPort(lefMacro, f, pinname, pinNum, pinDir, pinUse, oscale);
		break;
	    case LEF_CAPACITANCE:
		LefEndStatement(f);	/* Ignore. . . */
		break;
	    case LEF_PIN_END:
		if (LefParseEndStatement(f, pinname) == 0)
		{
		    LefError("Pin END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == LEF_PIN_END) break;
    }
}

/*
 *------------------------------------------------------------
 * LefEndStatement --
 *
 *	Read file input to EOF or a ';' token (end-of-statement)
 *
 *------------------------------------------------------------
 */

void
LefEndStatement(f)
    FILE *f;
{
    char *token;

    while ((token = LefNextToken(f, TRUE)) != NULL)
	if (*token == ';') break;
}

/*
 *------------------------------------------------------------
 *
 * LefReadMacro --
 *
 *	Read in a MACRO section from a LEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Creates a new cell definition in the magic database.
 *
 *------------------------------------------------------------
 */

enum lef_macro_keys {LEF_CLASS = 0, LEF_SIZE, LEF_ORIGIN,
	LEF_SYMMETRY, LEF_SOURCE, LEF_SITE, LEF_PIN, LEF_OBS,
	LEF_TIMING, LEF_FOREIGN, LEF_MACRO_END};

void
LefReadMacro(f, mname, oscale, importForeign)
    FILE *f;			/* LEF file being read	*/
    char *mname;		/* name of the macro 	*/
    float oscale;		/* scale factor um->magic units */
    bool importForeign;		/* Whether we should try to read 
				 * in a cell.
				 */
{
    CellDef *lefMacro;
    HashEntry *he;

    char *token, tsave[128];
    int keyword, pinNum;
    float x, y;
    bool has_size, is_imported = FALSE;
    Rect lefBBox;

    static char *macro_keys[] = {
	"CLASS",
	"SIZE",
	"ORIGIN",
	"SYMMETRY",
	"SOURCE",
	"SITE",
	"PIN",
	"OBS",
	"TIMING",
	"FOREIGN",
	"END",
	NULL
    };

    /* Start by creating a new celldef */

    he = HashFind(&lefDefInitHash, mname);
    if (HashGetValue(he))
    {
	int suffix;
	char newname[256];

	for (suffix = 1; HashGetValue(he) != NULL; suffix++)
	{
	    sprintf(newname, "%250s_%d", mname, suffix);
	    he = HashFind(&lefDefInitHash, newname);
	}
	LefError("Cell \"%s\" was already defined in this file.  "
		"Renaming this cell \"%s\"\n", mname, newname);
        lefMacro = DBCellLookDef(newname);
        if (lefMacro == NULL)
	{
	    lefMacro = lefFindCell(newname);
	    DBCellClearDef(lefMacro);
	    DBCellSetAvail(lefMacro);
	    HashSetValue(he, lefMacro);
	    is_imported = FALSE;
	}
	else
	    is_imported = TRUE;
    }
    else
    {
        lefMacro = DBCellLookDef(mname);
	if (lefMacro == NULL)
	{
	    lefMacro = lefFindCell(mname);
	    DBCellClearDef(lefMacro);
	    DBCellSetAvail(lefMacro);
	    HashSetValue(he, lefMacro);
	    is_imported = FALSE;
	}
	else
	    is_imported = TRUE;
    }

    /* Initial values */
    pinNum = 1;
    has_size = FALSE;
    lefBBox.r_xbot = 0;
    lefBBox.r_ybot = 0;

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, macro_keys);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in LEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case LEF_CLASS:
		token = LefNextToken(f, TRUE);
		if (*token != '\n')
		    DBPropPut(lefMacro, "LEFclass", StrDup((char **)NULL, token));
		LefEndStatement(f);
		break;
	    case LEF_SIZE:
		token = LefNextToken(f, TRUE);
		if (!token || sscanf(token, "%f", &x) != 1) goto size_error;
		token = LefNextToken(f, TRUE);		/* skip keyword "BY" */
		if (!token) goto size_error;
		token = LefNextToken(f, TRUE);
		if (!token || sscanf(token, "%f", &y) != 1) goto size_error;

		lefBBox.r_xtop = (int)roundf(x / oscale) + lefBBox.r_xbot;
		lefBBox.r_ytop = (int)roundf(y / oscale) + lefBBox.r_ybot;
		has_size = TRUE;
		LefEndStatement(f);
		break;
size_error:
		LefError("Bad macro SIZE; requires values X BY Y.\n");
		LefEndStatement(f);
		break;
	    case LEF_ORIGIN:
		token = LefNextToken(f, TRUE);
		if (!token || sscanf(token, "%f", &x) != 1) goto origin_error;
		token = LefNextToken(f, TRUE);
		if (!token || sscanf(token, "%f", &y) != 1) goto origin_error;

		lefBBox.r_xbot = -(int)roundf(x / oscale);
		lefBBox.r_ybot = -(int)roundf(y / oscale);
		if (has_size)
		{
		    lefBBox.r_xtop += lefBBox.r_xbot;
		    lefBBox.r_ytop += lefBBox.r_ybot;
		}
		LefEndStatement(f);
		break;
origin_error:
		LefError("Bad macro ORIGIN; requires 2 values.\n");
		LefEndStatement(f);
		break;
	    case LEF_SYMMETRY:
		strcpy(tsave, "");
		token = LefNextToken(f, TRUE);
		while (*token != ';')
		{
		    sprintf(tsave + strlen(tsave), " %s", token);
		    token = LefNextToken(f, TRUE);
		}
		DBPropPut(lefMacro, "LEFsymmetry", StrDup((char **)NULL, tsave + 1));
		break;
	    case LEF_SOURCE:
		token = LefNextToken(f, TRUE);
		if (*token != '\n')
		    DBPropPut(lefMacro, "LEFsource", StrDup((char **)NULL, token));
		LefEndStatement(f);
		break;
	    case LEF_SITE:
		token = LefNextToken(f, TRUE);
		if (*token != '\n')
		    DBPropPut(lefMacro, "LEFsite", StrDup((char **)NULL, token));
		LefEndStatement(f);
		break;
	    case LEF_PIN:
		token = LefNextToken(f, TRUE);
		/* Diagnostic */
		/*
		TxPrintf("   Macro defines pin %s\n", token);
		*/
		sprintf(tsave, "%.127s", token);
		if (is_imported)
		    LefSkipSection(f, tsave);
		else
		    LefReadPin(lefMacro, f, tsave, pinNum++, oscale);
		break;
	    case LEF_OBS:
		/* Diagnostic */
		/*
		TxPrintf("   Macro defines obstruction\n");
		*/
		if (is_imported)
		    LefSkipSection(f, NULL);
		else
		    LefReadGeometry(lefMacro, f, oscale, FALSE);
		break;
	    case LEF_TIMING:
		LefSkipSection(f, macro_keys[LEF_TIMING]);
		break;
	    case LEF_FOREIGN:
		if (importForeign)
		{
		    token = LefNextToken(f, TRUE);
		    sprintf(tsave, "%.127s", token);

		    /* To do:  Read and apply X and Y offsets */
		}
		LefEndStatement(f);
		break;
	    case LEF_MACRO_END:
		if (LefParseEndStatement(f, mname) == 0)
		{
		    LefError("Macro END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == LEF_MACRO_END) break;
    }

    /* Finish up creating the cell */

    if (is_imported)
    {
	/* Redefine cell bounding box to match the LEF macro	*/
	/* Leave "extended" to mark the original bounding box	*/

	if (has_size)
	{
	    lefMacro->cd_bbox = lefBBox;
	    lefMacro->cd_flags |= CDFIXEDBBOX;
	}
    }
    else
    {
	DBAdjustLabelsNew(lefMacro, &TiPlaneRect, 1);

	if (!has_size)
	{
	    LefError("   Macro does not define size:  computing from geometry\n");
	    DBReComputeBbox(lefMacro);
	}
	else
	{
            char *propstr = (char *)mallocMagic(64);
	    int reducer = DBCellFindScale(lefMacro);

	    lefMacro->cd_bbox = lefBBox;
	    lefMacro->cd_extended = lefBBox;
	}

	/* Fix the bounding box and do not allow edits */
	lefMacro->cd_flags |= /* CDNOEDIT | */ CDFIXEDBBOX;

	DRCCheckThis(lefMacro, TT_CHECKPAINT, &lefMacro->cd_bbox);
    }
    DBWAreaChanged(lefMacro, &lefMacro->cd_bbox, DBW_ALLWINDOWS,
		&DBAllButSpaceBits);
}

/*
 *------------------------------------------------------------
 *
 * LefAddViaGeometry --
 *
 *	Read in geometry for a VIA section from a LEF or DEF
 *	file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds to the lefLayer record for a via definition.
 *
 *------------------------------------------------------------
 */

void
LefAddViaGeometry(f, lefl, curlayer, oscale)
    FILE *f;			/* LEF file being read	*/
    lefLayer *lefl;		/* pointer to via info	*/
    TileType curlayer;		/* current tile type	*/
    float oscale;		/* output scaling	*/
{
    Rect *currect;
    LinkedRect *viaLR;

    /* Rectangles for vias are read in units of 1/2 lambda */
    currect = LefReadRect(f, curlayer, (oscale / 2));
    if (currect == NULL) return;

    /* Don't create any geometry for unknown layers! */
    if (curlayer < 0) return;

    /* For LEF contact types matching magic contact types,	*/
    /* size the LEF contact cut to cover the minimum		*/
    /* rectangle in the other layers that satisfies the		*/
    /* CIF/GDS contact generation.				*/

    if (DBIsContact(curlayer) && CIFCurStyle != NULL)
    {
	int edgeSize = 0, contSize, halfSize;
	float fcontSize;

	/* Get the minimum size of a contact (cut + borders) from cifoutput */
	contSize = CIFGetContactSize(curlayer, &edgeSize, NULL, NULL);

	/* All internal LEF via geometry values are doubled */
	contSize <<= 1;	
	edgeSize <<= 1;	

        if (contSize % CIFCurStyle->cs_scaleFactor == 0)
	   contSize /= CIFCurStyle->cs_scaleFactor;
	else
	   contSize = contSize / CIFCurStyle->cs_scaleFactor + 1;

        if (edgeSize % CIFCurStyle->cs_scaleFactor == 0)
	   edgeSize /= CIFCurStyle->cs_scaleFactor;
	else
	   edgeSize = edgeSize / CIFCurStyle->cs_scaleFactor + 1;

	if (edgeSize > 0 && contSize > 0)
	{
	    /* Flag a warning if the cut size is different from what's expected */
	    if ((currect->r_xtop - currect->r_xbot != edgeSize) ||
			(currect->r_ytop - currect->r_ybot != edgeSize))
	    {
		LefError("Warning: Cut size for magic type \"%s\" (%d x %d) does "
			"not match LEF/DEF\n",
			DBTypeLongNameTbl[lefl->type],
			edgeSize, edgeSize);
		LefError("  via cut size (%d x %d).  Magic layer cut size will "
			"be used!\n",
			currect->r_xtop - currect->r_xbot,
			currect->r_ytop - currect->r_ybot);
	    }

	    halfSize = contSize >> 1;
	    currect->r_xbot = ((currect->r_xbot + currect->r_xtop) / 2) - halfSize;
	    currect->r_ybot = ((currect->r_ybot + currect->r_ytop) / 2) - halfSize;
	    currect->r_xtop = currect->r_xbot + contSize;
	    currect->r_ytop = currect->r_ybot + contSize;
	}
    }

    if (GEO_SAMERECT(lefl->info.via.area, GeoNullRect))
    {
	lefl->info.via.area = *currect;
	lefl->type = curlayer;
    }
    else 
    {
	viaLR = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	viaLR->r_next = lefl->info.via.lr;
	lefl->info.via.lr = viaLR;
	viaLR->r_type = curlayer;
	viaLR->r_r = *currect;

	/* Make sure that the primary record is a contact type. */
	if (DBIsContact(curlayer) && !DBIsContact(lefl->type))
	{
	    viaLR->r_type = lefl->type;
	    lefl->type = curlayer;
	    viaLR->r_r = lefl->info.via.area;
	    lefl->info.via.area = *currect;
	}
    }
}

/*
 *------------------------------------------------------------
 *
 * LefReadLayerSection --
 *
 *	Read in a LAYER, VIA, or VIARULE section from a LEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds to the LEF layer info hash table.
 *
 *------------------------------------------------------------
 */

enum lef_layer_keys {LEF_LAYER_TYPE=0, LEF_LAYER_WIDTH, LEF_LAYER_SPACING,
	LEF_LAYER_PITCH, LEF_LAYER_DIRECTION, LEF_LAYER_OFFSET,
	LEF_VIA_DEFAULT, LEF_VIA_LAYER, LEF_VIA_RECT,
	LEF_VIARULE_VIA, LEF_LAYER_END};

void
LefReadLayerSection(f, lname, mode, lefl)
    FILE *f;			/* LEF file being read	  */
    char *lname;		/* name of the layer 	  */
    int mode;			/* layer, via, or viarule */
    lefLayer *lefl;		/* pointer to layer info  */
{
    char *token;
    int keyword, typekey;
    Rect viaArea;
    TileType curlayer = -1;
    float fvalue, oscale;

    /* These are defined in the order of CLASS_* in lefInt.h */
    static char *layer_type_keys[] = {
	"ROUTING",
	"CUT",
	"MASTERSLICE",
	"OVERLAP",
	NULL
    };

    static char *layer_keys[] = {
	"TYPE",
	"WIDTH",
	"SPACING",
	"PITCH",
	"DIRECTION",
	"OFFSET",
	"DEFAULT",
	"LAYER",
	"RECT",
	"VIA",
	"END",
	NULL
    };

    oscale = CIFGetOutputScale(1000);
    viaArea = GeoNullRect;

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, layer_keys);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in LEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case LEF_LAYER_TYPE:
		token = LefNextToken(f, TRUE);
		if (*token != '\n')
		{
		    typekey = Lookup(token, layer_type_keys);
		    if (typekey < 0)
			LefError("Unknown layer type \"%s\" in LEF file; "
				"ignoring.\n", token);
		}
		if (lefl->lefClass != typekey)
		    LefError("Attempt to reclassify layer %s from %s to %s\n",
				lname, layer_type_keys[lefl->lefClass],
				layer_type_keys[typekey]);
		LefEndStatement(f);
		break;
	    case LEF_LAYER_WIDTH:
		token = LefNextToken(f, TRUE);
		sscanf(token, "%f", &fvalue);
		if (lefl->lefClass == CLASS_ROUTE)
		    lefl->info.route.width = (int)roundf(fvalue / oscale);
		LefEndStatement(f);
		break;
	    case LEF_LAYER_SPACING:
		token = LefNextToken(f, TRUE);
		sscanf(token, "%f", &fvalue);
		if (lefl->lefClass == CLASS_ROUTE)
		    lefl->info.route.spacing = (int)roundf(fvalue / oscale);
		LefEndStatement(f);
		break;
	    case LEF_LAYER_PITCH:
		token = LefNextToken(f, TRUE);
		sscanf(token, "%f", &fvalue);
		if (lefl->lefClass == CLASS_ROUTE)
		    lefl->info.route.pitch = (int)roundf(fvalue / oscale);
		LefEndStatement(f);
		break;
	    case LEF_LAYER_DIRECTION:
		token = LefNextToken(f, TRUE);
		LefLower(token);
		if (lefl->lefClass == CLASS_ROUTE)
		    lefl->info.route.hdirection = (token[0] == 'h') ? TRUE : FALSE;
		LefEndStatement(f);
		break;
	    case LEF_LAYER_OFFSET:
		LefEndStatement(f);
		break;
	    case LEF_VIA_DEFAULT:
		/* Do nothing; especially, don't look for end-of-statement! */
		break;
	    case LEF_VIA_LAYER:
		curlayer = LefReadLayer(f, FALSE);
		LefEndStatement(f);
		break;
	    case LEF_VIA_RECT:
		LefAddViaGeometry(f, lefl, curlayer, oscale);
		LefEndStatement(f);
		break;
	    case LEF_VIARULE_VIA:
		LefEndStatement(f);
		break;
	    case LEF_LAYER_END:
		if (LefParseEndStatement(f, lname) == 0)
		{
		    LefError("Layer END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == LEF_LAYER_END) break;
    }
}

/*
 *------------------------------------------------------------
 *
 * LefRead --
 *
 *	Read a .lef file into a magic layout.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Many.  Cell definitions and uses are created and added to
 *	the database.
 *
 *------------------------------------------------------------
 */

enum lef_sections {LEF_VERSION = 0, LEF_NAMESCASESENSITIVE,
	LEF_PROPERTYDEFS, LEF_UNITS, LEF_SECTION_LAYER,
	LEF_SECTION_VIA, LEF_SECTION_VIARULE,
	LEF_SECTION_SPACING, LEF_SECTION_SITE, LEF_PROPERTY,
	LEF_NOISETABLE, LEF_CORRECTIONTABLE, LEF_IRDROP,
	LEF_ARRAY, LEF_SECTION_TIMING, LEF_EXTENSION, LEF_MACRO,
	LEF_END};

void
LefRead(inName, importForeign)
    char *inName;
    bool importForeign;
{
    FILE *f;
    char *filename;
    char *token;
    char tsave[128];
    int keyword;
    float oscale;
    HashEntry *he;
    lefLayer *lefl;

    static char *sections[] = {
	"VERSION",
	"NAMESCASESENSITIVE",
	"PROPERTYDEFINITIONS",
	"UNITS",
	"LAYER",
	"VIA",
	"VIARULE",
	"SPACING",
	"SITE",
	"PROPERTY",
	"NOISETABLE",
	"CORRECTIONTABLE",
	"IRDROP",
	"ARRAY",
	"TIMING",
	"BEGINEXT",
	"MACRO",
	"END",
	NULL
    };

    /* Make sure we have a valid LefInfo hash table, even if it's empty */
    if (LefInfo.ht_table == (HashEntry **) NULL)
	LefTechInit();

    f = lefFileOpen(NULL, inName, ".lef", "r", &filename);

    if (f == NULL)
    {
#ifdef MAGIC_WRAPPER
	TxError("Cannot open input file %s (%s).\n", filename,
		strerror(errno));
#else
	TxError("Cannot open input file: ");
	perror(filename);
#endif
	return;
    }

    TxPrintf("Reading LEF data from file %s.\n", filename);
    TxPrintf("This action cannot be undone.\n");
    UndoDisable();

    /* Initialize */
    HashInit(&LefCellTable, 32, HT_STRINGKEYS);
    HashInit(&lefDefInitHash, 32, HT_STRINGKEYS);
    oscale = CIFGetOutputScale(1000);

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, sections);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in LEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case LEF_VERSION:
		LefEndStatement(f);
		break;
	    case LEF_NAMESCASESENSITIVE:
		LefEndStatement(f);
		break;
	    case LEF_PROPERTYDEFS:
		LefSkipSection(f, sections[LEF_PROPERTYDEFS]);
		break;
	    case LEF_UNITS:
		LefSkipSection(f, sections[LEF_UNITS]);
		break;

	    case LEF_SECTION_VIA:
	    case LEF_SECTION_VIARULE:
		token = LefNextToken(f, TRUE);
		sprintf(tsave, "%.127s", token);
		he = HashFind(&LefInfo, token);
		lefl = (lefLayer *)HashGetValue(he);
		if (lefl == NULL)
		{
		    lefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
		    lefl->type = -1;
		    lefl->obsType = -1;
		    lefl->refCnt = 1;
		    lefl->lefClass = CLASS_VIA;
		    lefl->info.via.area = GeoNullRect;
		    lefl->info.via.cell = (CellDef *)NULL;
		    lefl->info.via.lr = (LinkedRect *)NULL;
		    HashSetValue(he, lefl);
		    LefReadLayerSection(f, tsave, keyword, lefl);
		    lefl->canonName = (char *)he->h_key.h_name;
		}
		else if (keyword == LEF_SECTION_VIARULE)
		    /* If we've already seen this via, don't reprocess. */
		    /* This deals with VIA followed by VIARULE.  We	*/
		    /* really ought to have special processing for the	*/
		    /* VIARULE section. . .				*/
		    LefSkipSection(f, tsave);
		else
		{
		    LefError("Warning:  Cut type \"%s\" redefined.\n", token);
		    lefl = LefRedefined(lefl, token);
		    LefReadLayerSection(f, tsave, keyword, lefl);
		}
		break;

	    case LEF_SECTION_LAYER:
		token = LefNextToken(f, TRUE);
		sprintf(tsave, "%.127s", token);
		he = HashLookOnly(&LefInfo, token);
		if (he == NULL)
		{
		    TileType mtype = DBTechNameType(token);
		    if (mtype < 0)
			mtype = DBTechNameType(LefLower(token));
		    if (mtype < 0)
		    {
			LefError("Layer %s cannot be mapped to any magic layer!\n",
				token);
			LefSkipSection(f, tsave);
			break;
		    }
		    else if (DBIsContact(mtype) && (keyword == LEF_SECTION_LAYER))
		    {
			LefError("Layer %s maps to a magic contact layer; "
				"must be defined in lef section of techfile\n",
				token);
			LefSkipSection(f, tsave);
			break;
		    }
		    else if (!DBIsContact(mtype) && (keyword != LEF_SECTION_LAYER))
		    {
			LefError("Via %s maps to a non-contact magic layer; "
				"must be defined in lef section of techfile\n",
				token);
			LefSkipSection(f, tsave);
			break;
		    }
		    else
		    {
			he = HashFind(&LefInfo, token);
			lefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
			lefl->type = mtype;
			lefl->obsType = -1;
			lefl->refCnt = 1;
			lefl->lefClass = (DBIsContact(mtype)) ? CLASS_VIA : CLASS_ROUTE;
			HashSetValue(he, lefl);
		        lefl->canonName = (char *)he->h_key.h_name;
		    }
		}
		else
		{
		    lefl = (lefLayer *)HashGetValue(he);
		    if (lefl && lefl->type < 0)
		    {
			LefError("Layer %s is only defined for obstructions!\n", token);
			LefSkipSection(f, tsave);
			break;
		    }
		}
		LefReadLayerSection(f, tsave, keyword, lefl);
		break;

	    case LEF_SECTION_SPACING:
		LefSkipSection(f, sections[LEF_SECTION_SPACING]);
		break;
	    case LEF_SECTION_SITE:
		token = LefNextToken(f, TRUE);
		TxPrintf("LEF file:  Defines site %s (ignored)\n", token);
		sprintf(tsave, "%.127s", token);
		LefSkipSection(f, tsave);
		break;
	    case LEF_PROPERTY:
		LefSkipSection(f, NULL);
		break;
	    case LEF_NOISETABLE:
		LefSkipSection(f, sections[LEF_NOISETABLE]);
		break;
	    case LEF_CORRECTIONTABLE:
		LefSkipSection(f, sections[LEF_CORRECTIONTABLE]);
		break;
	    case LEF_IRDROP:
		LefSkipSection(f, sections[LEF_IRDROP]);
		break;
	    case LEF_ARRAY:
		LefSkipSection(f, sections[LEF_ARRAY]);
		break;
	    case LEF_SECTION_TIMING:
		LefSkipSection(f, sections[LEF_SECTION_TIMING]);
		break;
	    case LEF_EXTENSION:
		LefSkipSection(f, sections[LEF_EXTENSION]);
		break;
	    case LEF_MACRO:
		token = LefNextToken(f, TRUE);
		/* Diagnostic */
		/*
		TxPrintf("LEF file:  Defines new cell %s\n", token);
		*/
		sprintf(tsave, "%.127s", token);
		LefReadMacro(f, tsave, oscale, importForeign);
		break;
	    case LEF_END:
		if (LefParseEndStatement(f, "LIBRARY") == 0)
		{
		    LefError("END statement out of context.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == LEF_END) break;
    }
    TxPrintf("LEF read: Processed %d lines.\n", lefCurrentLine);
    LefError(NULL);	/* print statement of errors, if any */

    /* Cleanup */
    HashKill(&LefCellTable);
    HashKill(&lefDefInitHash);
    if (f != NULL) fclose(f);
    UndoEnable();
}
