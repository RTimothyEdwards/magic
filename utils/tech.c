/*
 * tech.c --
 *
 * Read in a technology file.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/tech.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "database/database.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "utils/malloc.h"

global int  TechFormatVersion;
global bool TechOverridesDefault;

/* Define a file stack so that "include" calls can be nested */

typedef struct FStack		/* Linked FILE * pointers */
{
    FILE *file;
    struct FStack *next;          /* Pointer to another linked rectangle */
} filestack;

int techLineNumber;
char *TechFileName = NULL;

#define	iseol(c)	((c) == EOF || (c) == '\n')

/*
 * Each client of the technology module must make itself known by
 * a call to TechAddClient().  These calls provide both the names
 * of the sections of the technology file, as well as the procedures
 * to be invoked with lines in these sections.
 *
 * The following table is used to record clients of the technology
 * module.
 */

    typedef struct tC
    {
	bool		(*tc_proc)();	/* Procedure to be called for each
					 * line in section.
					 */
	void		(*tc_init)();	/* Procedure to be called before any
					 * lines in a section are processed.
					 */
	void		(*tc_final)();	/* Procedure to be called after all
					 * lines in section have been processed.
					 */
	struct tC	*tc_next;	/* Next client in section */
    } techClient;

    typedef struct
    {
	char		*ts_name;	/* Name of section */
	char		*ts_alias;	/* Alternative name of section */
	techClient	*ts_clients;	/* Pointer to list of clients */
	bool		 ts_read;	/* Flag: TRUE if section was read */
	bool		 ts_optional;	/* Flag: TRUE if section is optional */
	SectionID	 ts_thisSect;	/* SectionID of this section */
	SectionID	 ts_prevSects;	/* Mask of sections that must be
					 * read in before this one.  The
					 * mask is constructed from the
					 * section identifiers set by
					 * TechAddClient().
					 */
    } techSection;

#define	MAXSECTIONS	(8 * sizeof (int))	/* Not easily changeable */
#define	MAXARGS		30
#define MAXLINESIZE	1024

#define	SectionToMaskBit(s)		(1 << (s))
#define SectionMaskHasSection(m, s)	(m & SectionToMaskBit(s))

int techSectionNum;			/* ID of next new section */
SectionID techSectionMask;		/* Mask of sections already read */

techSection techSectionTable[MAXSECTIONS];
techSection *techSectionFree;		/* Pointer to next free section */
techSection *techCurrentSection;	/* Pointer to current section */

techSection *techFindSection();

/*
 * ----------------------------------------------------------------------------
 *
 * TechSectionGetMask --
 *
 * Get the SectionID mask for a specific section (specified by name).  The
 * returned mask is inverted;  that is, it is a mask containing bits
 * representing all the client sections except for the one sepcified.
 * This return value can be passed to TechLoad to re-read a specific
 * section.
 *
 * Results:
 *	Returns the inverted mask for the selected section ID.
 *
 * Side effects:
 *	If "depend" is non-NULL, the SectionID to which it points will be
 *	set to a mask representing the mask of sections which depend on
 *	the indicated section; that is, those sections which will be
 *	invalidated if the indicated section is altered in any way.
 *
 * ----------------------------------------------------------------------------
 */

SectionID
TechSectionGetMask(sectionName, depend)
    char *sectionName;
    SectionID *depend;
{
    techSection *tsp, *thissect;
    SectionID invid = 0;
    SectionID selected;

    thissect = techFindSection(sectionName);
    if (thissect == NULL) return -1;

    selected = thissect->ts_thisSect;

    for (tsp = techSectionTable; tsp < techSectionFree; tsp++)
    {
	if (tsp != thissect)
	{
	    invid |= tsp->ts_thisSect;
	    if (tsp->ts_prevSects & thissect->ts_thisSect)
		if (depend != NULL) *depend != tsp->ts_thisSect;
	}
    }
    return invid;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TechInit --
 *
 * Initialize the technology module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the technology read-in module.
 *	This function must be called before any other functions in
 *	this module are called.  It is called exactly once at the start
 *	of a magic session.
 *
 * ----------------------------------------------------------------------------
 */

void
TechInit()
{
    techCurrentSection = (techSection *) NULL;
    techSectionFree = techSectionTable;
    techSectionNum = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TechAddAlias --
 *
 *	Add an alternative name (alias) for a technology file section which
 *	may be used in place of the primary name.
 *
 *	This has been added mainly to handle sections which have been
 *	expanded beyond their original definition such that the section
 *	name is no longer appropriate.  Case in point:  the "images"
 *	section is broader in scope than the "contact" section, but
 *	because contacts are a subset of images in version 7.3, it is
 *	preferable to have an "images" section instead of a "contacts"
 *	section, with allowances for backwards compatibility.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates string memory.
 *
 * ----------------------------------------------------------------------------
 */

void
TechAddAlias(primaryName, alias)
    char *primaryName;
    char *alias;
{
    techSection *tsp;

    tsp = techFindSection(primaryName);
    if (tsp == (techSection *) NULL)
    {
	TxError("Unknown technology file section \"%s\" requested.\n",
		primaryName);
    }
    else
    {
	if (tsp->ts_alias != NULL)
	    freeMagic(tsp->ts_alias);
	tsp->ts_alias = StrDup((char **)NULL, alias);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * changePlanesFunc() ---
 *
 * This function hacks the existing layout database in case a tech file
 * is loaded which contains more or fewer planes than the exisiting
 * technology.  This is doing nothing fancy; it is simply making sure
 * that all memory allocation is accounted for.
 *
 * As a note for future implementation, it would be helpful to keep the
 * old plane name definitions around and try to match up the old and new
 * planes, so that it is possible to load a technology file which matches
 * the existing technology except for the addition or subtraction of one
 * or more planes (e.g., extra metal layer option) without completely
 * invalidating an existing layout.
 *
 * As written, this function is inherently dangerous.  It is intended for
 * use when loading a new tech file when there is no layout, just empty
 * tile planes.
 * ----------------------------------------------------------------------------
 */

int
changePlanesFunc(cellDef, arg)
    CellDef *cellDef;
    int *arg;
{
    int oldnumplanes = *arg;
    int pNum;

    if (oldnumplanes < DBNumPlanes)
    {
	/* New planes to be added */
	for (pNum = oldnumplanes; pNum < DBNumPlanes; pNum++)
	{
	    cellDef->cd_planes[pNum] = DBNewPlane((ClientData) TT_SPACE);
	}
    }
    else
    {
	/* Old planes to be subtracted */
	for (pNum = DBNumPlanes; pNum < oldnumplanes; pNum++)
	{
	    DBFreePaintPlane(cellDef->cd_planes[pNum]);
	    TiFreePlane(cellDef->cd_planes[pNum]);
	    cellDef->cd_planes[pNum] = (Plane *) NULL;
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TechAddClient --
 *
 * Add a client to the technology module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Identifies "sectionName" as a valid name for a section of a .tech
 *	file, and specifies that init() is the procedure to be called when
 *	a new technology is loaded, proc() as the procedure to be called
 *	for each line in the given section, and final() as the procedure to
 *      be called after the last line in the given section.
 *
 *	The init() procedure takes no arguments.
 *	The proc() procedure should be of the following form:
 *		bool
 *		proc(sectionName, argc, argv)
 *			char *sectionName;
 *			int argc;
 *			char *argv[];
 *		{
 *		}
 *	The final() procedure takes no arguments.
 *
 *	The argument prevSections should be a mask of the SectionID's
 *	of all sections that must be read in before this one.
 *
 *	If the argument 'pSectionID' is non-NULL, it should point to
 *	an int that will be set to the sectionID of this section.
 *
 *	It is legal for several procedures to be associated with a given
 *	sectionName; this is accomplished through successive calls to
 *	TechAddClient with the same sectionName.  The procedures will
 *	be invoked in the order in which they were handed to TechAddClient().
 *
 *	If the procedure given is NULL for init(), proc(), or final(), no
 *	procedure is invoked.
 *
 * ----------------------------------------------------------------------------
 */

void
TechAddClient(sectionName, init, proc, final, prevSections, pSectionID, opt)
    char *sectionName;
    void (*init)();
    bool (*proc)();
    void (*final)();
    SectionID prevSections;
    SectionID *pSectionID;
    bool opt; /* optional section */
{
    techSection *tsp;
    techClient *tcp, *tcl;

    tsp = techFindSection(sectionName);
    if (tsp == (techSection *) NULL)
    {
	tsp = techSectionFree++;
	ASSERT(tsp < &techSectionTable[MAXSECTIONS], "TechAddClient");
	tsp->ts_name = StrDup((char **) NULL, sectionName);
	tsp->ts_alias = NULL;
	tsp->ts_clients = (techClient *) NULL;
	tsp->ts_thisSect = SectionToMaskBit(techSectionNum);
	tsp->ts_prevSects = (SectionID) 0;
	tsp->ts_optional = opt;
	techSectionNum++;
    }

    tsp->ts_prevSects |= prevSections;
    if (pSectionID)
	*pSectionID = tsp->ts_thisSect;

    tcp = (techClient *) mallocMagic(sizeof (techClient));
    ASSERT(tcp != (techClient *) NULL, "TechAddClient");
    tcp->tc_init = init;
    tcp->tc_proc = proc;
    tcp->tc_final = final;
    tcp->tc_next = (techClient *) NULL;

    if (tsp->ts_clients == (techClient *) NULL)
	tsp->ts_clients = tcp;
    else
    {
	for (tcl = tsp->ts_clients; tcl->tc_next; tcl = tcl->tc_next)
	    /* Nothing */;
	tcl->tc_next = tcp;
    }
}

/*
 * ----------------------------------------------------------------------------
 * TechLoad --
 *
 * Initialize technology description information from a file.
 *
 * Results:
 *	TRUE if technology is successfully initialized (all required
 *	sections present and error free); FALSE otherwise.  Unrecognized
 *	sections cause an error message to be printed, but do not otherwise
 *	affect the result returned by TechLoad().
 *
 * Side effects:
 *	Calls technology initialization routines of other modules
 *	to initialize technology-specific information.
 *
 * ----------------------------------------------------------------------------
 */

bool
TechLoad(filename, initmask)
    char *filename;
    SectionID initmask;
{
    FILE *tf;
    techSection *tsp;
    techClient *tcp;
    char suffix[20], line[MAXLINESIZE], *realname;
    char *argv[MAXARGS];
    SectionID mask, badMask;
    int argc, s;
    bool retval, skip;
    filestack *fstack, *newstack;
    filestack topfile;

    fstack = NULL;
    techLineNumber = 0;
    badMask = (SectionID) 0;
    int saveNumPlanes;

    int changePlanesFunc();	/* forward declaration */
    int checkForPaintFunc();	/* forward declaration */

    if (initmask == -1)
    {
	TxError("Invalid technology file section requested.\n");
	return (FALSE);
    }

    /* If NULL is passed to argument "filename", this is a reload and	*/
    /* we should read TechFileName verbatim.				*/

    if ((filename == NULL) && (TechFileName != NULL))
    {
	tf = PaOpen(TechFileName, "r", (char *)NULL, ".", SysLibPath, &realname);
	if (tf == (FILE *) NULL)
	{
	    TxError("Could not find file '%s' in any of these "
			"directories:\n         %s\n",
			TechFileName, SysLibPath);
	    return (FALSE);
	}
    }
    else
    {
	char *sptr, *dptr;

	/* TECH_VERSION in the filename is deprecated as of magic version	*/
	/* 7.2.27;  TECH_VERSION is no longer defined in the utils/Makefile.	*/
	/* It has been changed to TECH_FORMAT_VERSION, left at version 27,	*/
	/* and placed in utils/tech.h.  It is needed for backward		*/
	/* compatibility with *.tech27 files, of which there are many.	*/

	(void) sprintf(suffix, ".tech");

	/* Added 1/20/2015 to correspond to change to PaLockOpen();	*/
	/* Always strip suffix from filename when suffix is specified.	*/

	sptr = strrchr(filename, '/');
	if (sptr == NULL)
	    sptr = filename;
	else
	    sptr++;

	dptr = strrchr(sptr, '.');
	if ((dptr != NULL) && !strncmp(dptr, suffix, strlen(suffix)))
	    *dptr = '\0';

	tf = PaOpen(filename, "r", suffix, ".", SysLibPath, &realname);
	if (tf == (FILE *) NULL)
	{
	    /* Try looking for tech files from the last version to	*/
	    /* put the version number into the filename itself.	*/

	    (void) sprintf(suffix, ".tech%d", TECH_FORMAT_VERSION);

	    tf = PaOpen(filename, "r", suffix, ".", SysLibPath, &realname);
	    if (tf == (FILE *) NULL)
	    {
		TxError("Could not find file '%s.tech' in any of these "
			"directories:\n         %s\n",
			filename, SysLibPath);
		return (FALSE);
	    }
	}
	StrDup(&TechFileName, realname);

	// In case filename is not a temporary string, put it back the
	// way it was.
	if (dptr != NULL) *dptr = '.';
    }

    topfile.file = tf;
    topfile.next = NULL;
    fstack = &topfile;

    // If TechLoad is called with initmask == -2, test that the file
    // exists and is readable, and that the first non-comment line
    // is the keyword "tech".

    if (initmask == -2)
    {
	argc = techGetTokens(line, sizeof line, &fstack, argv);
	fclose(tf);
	if (argc != 1) return (FALSE);
	if (strcmp(argv[0], "tech")) return (FALSE);
	return (TRUE);
    }

    /*
     * Mark all sections as being unread.
     */
    techSectionMask = initmask;
    for (tsp = techSectionTable; tsp < techSectionFree; tsp++)
    {
	tsp->ts_read = FALSE;
    }

    /*
     * Run section initializations if this is not a reload.
     * CIF istyle, CIF ostyle, and extract sections need calls
     * to the init functions which clean up memory devoted to
     * remembering all the styles.
     */

    if (filename != NULL)
    {
#ifdef CIF_MODULE
	CIFTechInit();
	CIFReadTechInit();
#endif
	ExtTechInit();
	DRCTechInit();
	MZTechInit();	
	
            /* Changing number of planes requires handling on every     */
            /* celldef.  So we need to save the original number of      */
            /* planes to see if it shrinks or expands.                  */


	saveNumPlanes = DBNumPlanes;
    }

    /*
     * Sections in a technology file begin with a single line containing
     * the keyword identifying the section, and end with a single line
     * containing the keyword "end".
     */

    retval = TRUE;
    skip = FALSE;
    while ((argc = techGetTokens(line, sizeof line, &fstack, argv)) >= 0)
    {
	/* Check for file inclusions (can be nested) */
	if ((argc > 1) && (!strcmp(argv[0], "include")))
	{
	    char *sptr;

	    tf = PaOpen(argv[1], "r", suffix, ".", SysLibPath, NULL);
	    if (tf != NULL)
	    {
		newstack = (filestack *)mallocMagic(sizeof(filestack));
		newstack->file = tf;
		newstack->next = fstack;
		fstack = newstack;
		continue;
	    }

	    /* Check the directory from which the tech file	*/
	    /* itself was read.					*/

	    if ((sptr = strrchr(TechFileName, '/')) != NULL)
	    {
		*sptr = '\0';
		tf = PaOpen(argv[1], "r", suffix, TechFileName, NULL, NULL);
		*sptr = '/';
		if (tf != NULL)
		{
		    newstack = (filestack *)mallocMagic(sizeof(filestack));
		    newstack->file = tf;
		    newstack->next = fstack;
		    fstack = newstack;
		    continue;
		}
	    }
	    TechError("Warning: Couldn't find include file %s\n", argv[1]);
	}

	if (!skip && techCurrentSection == NULL)
	{
	    if (argc != 1)
	    {
		TechError("Bad section header line\n");
		goto skipsection;
	    }

	    tsp = techFindSection(argv[0]);
	    if (tsp == (techSection *) NULL)
	    {
		TechError("Unrecognized section name: %s\n", argv[0]);
		goto skipsection;
	    }
	    else if (initmask & tsp->ts_thisSect)
	    {
		skip = TRUE;
		continue;
	    }
	    if (mask = (tsp->ts_prevSects & ~techSectionMask))
	    {
		techSection *sp;

		TechError("Section %s appears too early.\n", argv[0]);
		TxError("\tMissing prerequisite sections:\n");
		for (sp = techSectionTable; sp < techSectionFree; sp++)
		    if (mask & sp->ts_thisSect)
			TxError("\t\t%s\n", sp->ts_name);
		goto skipsection;
	    }
	    techCurrentSection = tsp;

	    /* Invoke initialization routines for all clients that
	     * provided them.
	     */

	    for (tcp = techCurrentSection->ts_clients;
		    tcp != NULL;
		    tcp = tcp->tc_next)
	    {
		if (tcp->tc_init)
		    (void) (*tcp->tc_init)();
	    }
	    continue;
	}

	/* At the end of the section, invoke the finalization routine
	 * of the client's, if there is one.
	 */

	if (argc == 1 && strcmp(argv[0], "end") == 0)
	{
	    if (!skip)
	    {
		techSectionMask |= techCurrentSection->ts_thisSect;
		techCurrentSection->ts_read = TRUE;
		for (tcp = techCurrentSection->ts_clients;
			tcp != NULL;
			tcp = tcp->tc_next)
		{
		    if (tcp->tc_final)
			(*tcp->tc_final)();
		}
	    }
	    techCurrentSection = (techSection *) NULL;
	    skip = FALSE;
	    continue;
	}

	if (!skip)
	    for (tcp = techCurrentSection->ts_clients;
			tcp != NULL;
			tcp = tcp->tc_next)
		if (tcp->tc_proc)
		{
		    if (!(*tcp->tc_proc)(techCurrentSection->ts_name,argc,argv))
		    {
			retval = FALSE;
			badMask |= techCurrentSection->ts_thisSect;
		    }
		}
	continue;

skipsection:
	TxError("[Skipping to \"end\"]\n");
	skip = TRUE;
    }

    if (badMask)
    {
	TxError("The following sections of %s contained errors:\n", TechFileName);
	for (s = 0; s < techSectionNum; s++)
	    if (SectionMaskHasSection(badMask, s))
		TxError("    %s\n", techSectionTable[s].ts_name);
    }

    for (tsp = techSectionTable; tsp < techSectionFree; tsp++)
    {
	if (!(initmask & tsp->ts_thisSect))
	{
	    if (!tsp->ts_read && !tsp->ts_optional)
	    {
		TxError("Section \"%s\" was missing from %s.\n",
			tsp->ts_name, TechFileName);
		retval = FALSE;
	    }
	}
    }

    /* In case we hit an error in an included file. . . */
    while ((fstack != NULL) && (fstack != &topfile))
    {
	fclose(fstack->file);
	freeMagic(fstack);
	fstack = fstack->next;
    }
    if (fstack) fclose(fstack->file);

    /* Note:  If filename is NULL, then individual sections are	*/
    /* being reloaded, and it is the responsibility of the	*/
    /* calling routine to invoke any exit function specific to	*/
    /* that section (e.g., DRCTechScale() when loading a new	*/
    /* DRC style).						*/

    if ((filename != NULL) && (retval == TRUE))
    {
	/* If internal scalefactor is not the default 1:1, then we  */
	/* need to scale the techfile numbers accordingly.          */

	if ((DBLambda[0] != 1) || (DBLambda[1] != 1))
	{
	    int d = DBLambda[0];
	    int n = DBLambda[1];

	    CIFTechInputScale(d, n, TRUE);
	    CIFTechOutputScale(d, n);
	    DRCTechScale(d, n);
	    ExtTechScale(d, n);
	    WireTechScale(d, n);
#ifdef LEF_MODULE
	    LefTechScale(d, n);
#endif
#ifdef ROUTE_MODULE
	    RtrTechScale(d, n);
#endif
	    TxPrintf("Scaled tech values by %d / %d to"
			" match internal grid scaling\n", n, d);

	    /* Check if we're below the scale set by cifoutput gridlimit */
	    if (CIFTechLimitScale(1, 1))
		TxError("WARNING:  Current grid scale is smaller"
		" than the minimum for the process!\n");
	}

	/* Post-technology reading routines */

#ifdef ROUTE_MODULE
	MZAfterTech();
	IRAfterTech();
	GAMazeInitParms();
#endif
	PlowAfterTech();

	if (DBCellSrDefs(0, checkForPaintFunc, (ClientData)&saveNumPlanes))
	{
	    if (saveNumPlanes != DBNumPlanes)
		TxError("Warning:  Number of planes has changed.  ");
	    TxError("Existing layout may be invalid.\n");
	}
	if (saveNumPlanes != DBNumPlanes)
	    DBCellSrDefs(0, changePlanesFunc, (ClientData) &saveNumPlanes);
    }
    else if (retval == FALSE)
    {
	/* On error, remove any existing technology file name */
	freeMagic(TechFileName);
	TechFileName = NULL;
    }

    return (retval);
}

/*
 * ----------------------------------------------------------------------------
 *
 * TechError --
 *
 * Print an error message referring to a given line number in the
 * technology module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints an error message.
 *
 * ----------------------------------------------------------------------------
 */

void
TechPrintLine()
{
    char *section;

    if (techCurrentSection)
	section = techCurrentSection->ts_name;
    else
	section = "(none)";

    TxError("%s: line %d: section %s:\n\t",
		TechFileName, techLineNumber, section);
}

void
TechError(char *fmt, ...)
{
    va_list args;

    TechPrintLine();
    va_start(args, fmt);
    Vfprintf(stderr, fmt, args);
    va_end(args);
}


/* ================== Functions local to this module ================== */

/*
 * ----------------------------------------------------------------------------
 *
 * techFindSection --
 *
 * Return a pointer to the entry in techSectionTable for the section
 * of the given name.
 *
 * Results:
 *	A pointer to the new entry, or NULL if none could be found.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

techSection *
techFindSection(sectionName)
    char *sectionName;
{
    techSection *tsp;

    for (tsp = techSectionTable; tsp < techSectionFree; tsp++)
    {
	if (!strcmp(tsp->ts_name, sectionName))
	    return (tsp);
	else if (tsp->ts_alias != NULL)
	{
	    if (!strcmp(tsp->ts_alias, sectionName))
		return (tsp);
	}
    }
    return ((techSection *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * techGetTokens --
 *
 * Read a line from the technology file and split it up into tokens.
 * Blank lines are ignored.  Lines ending in backslash are joined
 * to their successor lines.
 * We assume that all macro definition and comment elimination has
 * been done by the C preprocessor.
 *
 * Results:
 *	Returns the number of tokens into which the line was split, or
 *	-1 on end of file.  Never returns 0.
 *
 * Side effects:
 *	Copies the line just read into 'line'.  The trailing newline
 *	is turned into a '\0'.  The line is broken into tokens which
 *	are then placed into argv.
 *
 * ----------------------------------------------------------------------------
 */

int
techGetTokens(line, size, fstack, argv)
    char *line;			/* Character array into which line is read */
    int size;			/* Size of character array */
    filestack **fstack;		/* Open technology file on top of stack */
    char *argv[];		/* Vector of tokens built by techGetTokens() */
{
    char *get, *put, *getp;
    bool inquote;
    int argc = 0;
    int currspace;		/* chars remaining before end of line[size] */
    FILE *file;			/* Current technology file */

    file = (*fstack)->file;

    /* Read one line into the buffer, joining lines when they end
     * in backslashes.
     */

    /* Code revision (MDG, Stanford):  Prevent the 1024-character limit due */
    /* to unconditional decrement of size.  Long comment lists could cause  */
    /* infinite looping.  New code interprets first non-space character '#' */
    /* as a comment character, rather than requiring it to be in the first  */
    /* column.								    */

    /* Code revision (RTE, Open Circuit Design):  Handle DOS-style CR/LF    */
    /* Code revision (RTE, Open Circuit Design):  Handle "include" files    */

start:
     get = line;
     currspace = size;
     while (currspace > 0)
     {
	techLineNumber += 1;
	while (fgets(get, currspace, file) == NULL) {
	    if ((*fstack)->next != NULL)
	    {
		fclose((*fstack)->file);
		*fstack = (*fstack)->next;
		file = (*fstack)->file;
	    }
	    else
		return (-1);
	}
	getp = get;
	while(isspace(*getp)) getp++;
	if (*getp == '#') continue;
	for (put = get; *put != '\n'; put++) currspace -= 1;
	if (put != get)
	{
	    put--;
	    if (*put == 0xd) put--;	/* Handle DOS-style CR/LF */
	    if (*put == '\\')
	    {
		get = put;
		continue;
	    }
	    put++;
	}
	*put= '\0';
	break;
    }
    if (currspace == 0) TechError("long line truncated\n");

    get = put = line;

    while (*get != '\0')
    {
	/* Skip leading blanks */

	while (isspace(*get)) get++;

	/* Beginning of the token is here. */

	argv[argc] = put = get;
	if (*get == '"')
	{
	    get++;
	    inquote = TRUE;
	} else inquote = FALSE;

	/*
	 * Grab up characters to the end of the token.  Any character
	 * preceded by a backslash is taken literally.
	 */
	
	while (*get != '\0')
	{
	    if (inquote)
	    {
		if (*get == '"') break;
	    }
	    else if (isspace(*get)) break;

	    if (*get == '\\')	/* Process quoted characters literally */
	    {
		get += 1;
		if (*get == '\0') break;
	    }

	    /* Copy into token receiving area */
	    *put++ = *get++;
	}

	/*
	 * If we got no characters in the token, we must have been at
	 * the end of the line.
	 */
	if (get == argv[argc])
	    break;
	
	/* Terminate the token and advance over the terminating character. */

	if (*get != '\0') get++;	/* Careful!  could be at end of line! */
	*put++ = '\0';
	argc++;
    }

    if (argc == 0)
	goto start;

    return (argc);
}
