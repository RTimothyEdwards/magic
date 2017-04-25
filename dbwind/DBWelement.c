/* DBWelement.c -
 *
 *
 *	This file provides a standard set of procedures for Magic
 *	commands to use to handle drawing of various elements on
 *	top of the layout.  Elements can be lines, rectangles,
 *	and text (and hopefully will be expanded to include polygons
 *	and arcs).  These operate very similarly to feedback regions,
 *	in that they are persistant until destroyed, and do not
 *	interact with the layout in any way.
 * 
 * Copyright (C) 2003 Open Circuit Design, Inc., for MultiGiG, Ltd.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "utils/signals.h"

/* Types of elements */

#define ELEMENT_RECT	0
#define ELEMENT_LINE	1
#define ELEMENT_TEXT	2

/* Linked list definition for styles */

typedef struct _style *styleptr;

typedef struct _style
{
    int style;
    styleptr next;
} stylestruct;
    
/* Each element is stored in a record that looks like this: */

typedef struct _element
{
    int type;			/* Type of element (see list above) */
    unsigned char flags;	/* Special flags (depends on element type) */
    CellDef *rootDef;		/* Root definition of windows in which to
				 * display this element.
				 */
    stylestruct *stylelist;	/* Linked list of display styles with which
				 * to paint this element.  There must be at
				 * least one style, so first one is hardwired.
				 */
    Rect area;			/* The area of a rectangle element,
				 * or the two points of a line element,
				 * or the area box of a text element.
				 */
    char *text;			/* The text of a text element, or NULL. */

} DBWElement;

/* The following stuff describes all the feedback information we know
 * about.  The feedback is stored in a big array that grows whenever
 * necessary.
 */

HashTable elementTable;			/* Hash table holding elements info */

static CellDef *dbwelemRootDef;		/* To pass root cell definition from
					 * dbwelemGetTransform back up to
					 * DBWElementAdd.
					 */

/*
 * ----------------------------------------------------------------------------
 *
 * AppendString ---
 *
 *	Support function for DBWPrintElements().  Allocates memory for and
 *	adds string "newstr" to the string pointed to by "oldstr".  If
 *	"postfix" is non-NULL, this is also appended to "oldstr".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory reallocation of *oldstr.
 *
 * ----------------------------------------------------------------------------
 */

void
AppendString(oldstr, newstr, postfix)
    char **oldstr;
    char *newstr;
    char *postfix;
{
    char *tmpstr;
    int olen = 0;
    int nlen = strlen(newstr);
    int plen = 0;

    if (*oldstr != NULL) olen = strlen(*oldstr);
    if (postfix != NULL) plen = strlen(postfix);
    tmpstr = (char *)mallocMagic(olen + nlen + plen + 1);
    if (*oldstr == NULL)
	strcpy(tmpstr, newstr);
    else
    {
	strcpy(tmpstr, *oldstr);
	strcat(tmpstr, newstr);
	freeMagic(*oldstr);
    }
    if (postfix != NULL) strcat(tmpstr, postfix);
    *oldstr = tmpstr;
}

/*
 * ----------------------------------------------------------------------------
 *
 * AppendFlag --
 *
 *	Support function for DBWPrintElements to assist in generating
 *	the string of comma-separated flags for each element printed
 *	to the output.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Space is allocated and text added to "rstr".  The pointer to
 *	boolean "flagset" is set to TRUE, indicating that at least one
 *	flag has been written to the output.
 *
 * ----------------------------------------------------------------------------
 */

void AppendFlag(char **rstr, bool *flagset, char *fname)
{
    AppendString(rstr, *flagset ? "," : " ", fname);
    *flagset = TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWPrintElements --
 *
 *	Generate a string containing the names of all elements which are
 *	defined in CellDef "cellDef" and have flags matching "flagmask".
 *	Format is appropriate for writing output to a .mag file.
 *
 * Results:
 *	An allocated string, or NULL if no elements were found.
 *
 * Side Effects:
 *	Memory is allocated for the string and must be freed by the calling
 *	procedure.
 * ----------------------------------------------------------------------------
 */

char *
DBWPrintElements(cellDef, flagmask)
    CellDef *cellDef;
    unsigned char flagmask;
{
    DBWElement *elem;
    HashSearch hs;
    HashEntry *he;
    char *rstr = NULL;	/* allocated return string */
    char istr[10];
    styleptr sptr;
    bool flagset;

    /* These must match the order of text sizes in graphics/graphics.h */
    static char *textSizes[] = {"small", "medium", "large", "xlarge",
	"default", NULL}; 

    /* These must match the order of element type definitions above! */
    char *etypes[] = {"rectangle", "line", "text"};

    HashStartSearch(&hs);
    while (he = HashNext(&elementTable, &hs))
    {
	if (elem = (DBWElement *)HashGetValue(he))
	{
	    if ((elem->rootDef == cellDef) && (elem->flags & flagmask))
	    { 
		/* print element type */
		AppendString(&rstr, etypes[elem->type], " ");
		/* print element name */
		AppendString(&rstr, (char *)he->h_key.h_name, " ");

		/* print dstyle(s) */
		for (sptr = elem->stylelist; sptr != NULL; sptr = sptr->next)
		    AppendString(&rstr, GrStyleTable[sptr->style].longname,
			((sptr->next == NULL) ? " " : ","));

		/* print start point */
		sprintf(istr, "%d", elem->area.r_xbot);
		AppendString(&rstr, istr, " ");
		sprintf(istr, "%d", elem->area.r_ybot);
		AppendString(&rstr, istr, " ");
		switch (elem->type)
		{
		    case ELEMENT_RECT:
		        /* end point */
			sprintf(istr, "%d", elem->area.r_xtop);
			AppendString(&rstr, istr, " ");
			sprintf(istr, "%d", elem->area.r_ytop);
			AppendString(&rstr, istr, "\n");
			/* no flags to write.  Only applicable flag is  */
			/* temporary/persistent, and temporary elements */
			/* don't get written to the file.		*/
		        break;
		    case ELEMENT_LINE:
		        /* end point */
			sprintf(istr, "%d", elem->area.r_xtop);
			AppendString(&rstr, istr, " ");
			sprintf(istr, "%d", elem->area.r_ytop);
			AppendString(&rstr, istr, NULL);
			/* any non-default flags? */
			flagset = FALSE;
			if (elem->flags & DBW_ELEMENT_LINE_HALFX)
			    AppendFlag(&rstr, &flagset, "halfx");
			if (elem->flags & DBW_ELEMENT_LINE_HALFY)
			    AppendFlag(&rstr, &flagset, "halfy");
			if (elem->flags & DBW_ELEMENT_LINE_ARROWL)
			    AppendFlag(&rstr, &flagset, "arrowleft");
			if (elem->flags & DBW_ELEMENT_LINE_ARROWR)
			    AppendFlag(&rstr, &flagset, "arrowright");
			AppendString(&rstr, "\n", NULL);
		        break;
		    case ELEMENT_TEXT:
		        /* label text */
		        AppendString(&rstr, "\"", NULL);
		        AppendString(&rstr, elem->text, NULL);
		        AppendString(&rstr, "\"", NULL);
			/* any non-default flags? */
			flagset = FALSE;
			if (((elem->flags & DBW_ELEMENT_TEXT_POS) >> 4) !=
					GEO_CENTER)
			    AppendFlag(&rstr, &flagset, GeoPosToName((elem->flags &
					DBW_ELEMENT_TEXT_POS) >> 4));
			if (((elem->flags & DBW_ELEMENT_TEXT_SIZE) >> 1) !=
					GR_TEXT_MEDIUM)
			    AppendFlag(&rstr, &flagset, textSizes[(elem->flags &
					DBW_ELEMENT_TEXT_SIZE) >> 1]);
		        AppendString(&rstr, "\n", NULL);
		        break;
		}
	    }
	}
    }
    return rstr;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementRedraw --
 *
 * 	This procedure is called by the highlight manager to redisplay
 *	feedback highlights.  The window is locked before entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any feedback information that overlaps a non-space tile in
 *	plane is redrawn.
 *
 * Tricky stuff:
 *	Redisplay is numerically difficult, particularly when feedbacks
 *	have a large internal scale factor:  the tendency is to get
 *	integer overflow and get everything goofed up.  Be careful
 *	when making changes to the code below.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementRedraw(window, plane)
    MagWindow *window;		/* Window in which to redraw. */
    Plane *plane;		/* Non-space tiles on this plane mark what
				 * needs to be redrawn.
				 */
{
    int curStyle, newStyle;
    styleptr stylePtr;
    CellDef *windowRoot;
    Rect screenArea;
    DBWElement *elem;
    HashSearch hs;
    HashEntry *entry;
    Point p;

    windowRoot = ((CellUse *) (window->w_surfaceID))->cu_def;
    curStyle = -1;

    HashStartSearch(&hs);
    while ((entry = HashNext(&elementTable, &hs)) != NULL)
    {
	elem = (DBWElement *) HashGetValue(entry);
	if (!elem) continue;
	else if (elem->rootDef != windowRoot) continue;

	/* Transform the feedback area to screen coords.  This is
	 * very similar to the code in WindSurfaceToScreen, except
	 * that there's additional scaling involved.
	 */

	WindSurfaceToScreenNoClip(window, &elem->area, &screenArea);

	/* Deal with half-point-offset flags for the line element */
	if ((elem->type == ELEMENT_LINE) &&
		(elem->flags & (DBW_ELEMENT_LINE_HALFX | DBW_ELEMENT_LINE_HALFY)))
	{
	    static Rect unitArea = {{0, 0}, {1, 1}};
	    Rect transArea;
	    int offx, offy;

	    WindSurfaceToScreenNoClip(window, &unitArea, &transArea);
	    offx = (transArea.r_xtop - transArea.r_xbot) >> 1;
	    offy = (transArea.r_ytop - transArea.r_ybot) >> 1;

	    if (elem->flags & DBW_ELEMENT_LINE_HALFX)
	    {
		screenArea.r_xbot += offx;
		screenArea.r_xtop += offx;
	    }
	    if (elem->flags & DBW_ELEMENT_LINE_HALFY)
	    {
		screenArea.r_ybot += offy;
		screenArea.r_ytop += offy;
	    }
	}

	if ((screenArea.r_xbot <= screenArea.r_xtop) &&
		(screenArea.r_ybot <= screenArea.r_ytop))
	{
	    for (stylePtr = elem->stylelist; stylePtr != NULL;
			stylePtr = stylePtr->next)
	    {
		newStyle = stylePtr->style;
		if (newStyle != curStyle)
		{
		    curStyle = newStyle;
		    GrSetStuff(curStyle);
		}
		switch (elem->type)
		{
		    case ELEMENT_RECT:
			GrFastBox(&screenArea);
			break;
		    case ELEMENT_TEXT:
			p.p_x = screenArea.r_xbot;
			p.p_y = screenArea.r_ybot;
			GrPutText(elem->text, curStyle, &p,
				((elem->flags & DBW_ELEMENT_TEXT_POS) >> 4),
				((elem->flags & DBW_ELEMENT_TEXT_SIZE) >> 1),
				FALSE, &(window->w_screenArea), (Rect *)NULL);
			break;
		    case ELEMENT_LINE:
			GrClipLine(screenArea.r_xbot, screenArea.r_ybot,
				screenArea.r_xtop, screenArea.r_ytop);
			/* Draw arrowheads on endpoints, if flags are set */

			if (elem->flags & (DBW_ELEMENT_LINE_ARROWL |
					DBW_ELEMENT_LINE_ARROWR)) {
			    static Rect unitArea = {{0, 0}, {1, 1}};
			    Rect transArea;
			    Point polyp[4];
			    double theta, r;
			    int i, offx, offy;

			    WindSurfaceToScreenNoClip(window, &unitArea, &transArea);
			    offx = (transArea.r_xtop - transArea.r_xbot) >> 1;
			    offy = (transArea.r_ytop - transArea.r_ybot) >> 1;
			    WindSurfaceToScreenNoClip(window, &elem->area, &screenArea);
			    if (elem->flags & DBW_ELEMENT_LINE_HALFX)
			    {
                                screenArea.r_xbot += offx;
                                screenArea.r_xtop += offx;
			    }
			    if (elem->flags & DBW_ELEMENT_LINE_HALFY)
			    {
                                screenArea.r_ybot += offy;
                                screenArea.r_ytop += offy;
			    }
			    theta = atan2((double)(screenArea.r_ytop
					- screenArea.r_ybot), (double)
                                         (screenArea.r_xtop - screenArea.r_xbot));
			    r = (double)(transArea.r_xtop - transArea.r_xbot);
			    if (elem->flags & DBW_ELEMENT_LINE_ARROWL)
			    {
				for (i = 0; i < 4; i++)
				{
				    polyp[i].p_x = screenArea.r_xbot;
				    polyp[i].p_y = screenArea.r_ybot;
				}
				polyp[1].p_x += (int)(r * cos(theta + 0.2));
				polyp[1].p_y += (int)(r * sin(theta + 0.2));
				polyp[2].p_x += (int)(r * 0.9 * cos(theta));
				polyp[2].p_y += (int)(r * 0.9 * sin(theta));
				polyp[3].p_x += (int)(r * cos(theta - 0.2));
				polyp[3].p_y += (int)(r * sin(theta - 0.2));
				GrFillPolygon(polyp, 4);
			    }

			    if (elem->flags & DBW_ELEMENT_LINE_ARROWR)
			    {
				for (i = 0; i < 4; i++)
				{
				    polyp[i].p_x = screenArea.r_xtop;
				    polyp[i].p_y = screenArea.r_ytop;
				}
				polyp[1].p_x -= (int)(r * cos(theta + 0.2));
				polyp[1].p_y -= (int)(r * sin(theta + 0.2));
				polyp[2].p_x -= (int)(r * 0.9 * cos(theta));
				polyp[2].p_y -= (int)(r * 0.9 * sin(theta));
				polyp[3].p_x -= (int)(r * cos(theta - 0.2));
				polyp[3].p_y -= (int)(r * sin(theta - 0.2));
				GrFillPolygon(polyp, 4);
			    }
			}
			break;
		}
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementUndraw --
 *
 *	Paint the element in style ERASE_ALL to erase it.
 *
 * ----------------------------------------------------------------------------
 */

void
dbwElementUndraw(mw, elem)
    MagWindow *mw;
    DBWElement *elem;		/* The element to erase */
{
    CellDef *windowRoot;
    Rect screenArea, textArea;

    if (mw == NULL) return;	/* No window; can't undraw */
    windowRoot = ((CellUse *) (mw->w_surfaceID))->cu_def;
  
    GrLock(mw, TRUE);

    /* Deal with half-point-offset flags for the line element	*/
    /* by enlarging the area by 1 unit.				*/

    if ((elem->type == ELEMENT_LINE) &&
		(elem->flags & (DBW_ELEMENT_LINE_HALFX | DBW_ELEMENT_LINE_HALFY)))
    {
	Rect newArea = elem->area;

	if (elem->flags & DBW_ELEMENT_LINE_HALFX) newArea.r_xtop++;
	if (elem->flags & DBW_ELEMENT_LINE_HALFY) newArea.r_ytop++;

	WindSurfaceToScreen(mw, &newArea, &screenArea);
    }
    else
	WindSurfaceToScreen(mw, &elem->area, &screenArea);

    /* For text, determine the size of the text and expand the	*/
    /* screenArea rectangle accordingly.			*/

    if (elem->type == ELEMENT_TEXT)
    {
	int tpos = (elem->flags & DBW_ELEMENT_TEXT_POS) >> 4;
	int tsize = (elem->flags & DBW_ELEMENT_TEXT_SIZE) >> 1;

	GrLabelSize(elem->text, tpos, tsize, &textArea);
	screenArea.r_xbot += textArea.r_xbot;
	screenArea.r_ybot += textArea.r_ybot;
	screenArea.r_xtop += textArea.r_xtop;
	screenArea.r_ytop += textArea.r_ytop;
    }

    if ((screenArea.r_xbot <= screenArea.r_xtop) &&
		(screenArea.r_ybot <= screenArea.r_ytop))
    {
	GrSetStuff(STYLE_ERASEALL);
	GrFastBox(&screenArea);
        WindAreaChanged(mw, &screenArea);
    }
    GrUnlock(mw, TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementDelete --
 *
 * 	This procedure deletes an element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An element is found in the hash table by name, and if it exists,
 *	it is deleted.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementDelete(MagWindow *w, char *name)
{
    DBWElement *elem;
    CellDef *currentRoot;
    HashEntry *entry;
    styleptr stylePtr;

    entry = HashFind(&elementTable, name);

    if (entry == NULL) return;

    elem = (DBWElement *)HashGetValue(entry);

    if (elem == NULL) return;

    dbwElementUndraw(w, elem);

    /* mark element's cell as having been modified */
    if (elem->flags & DBW_ELEMENT_PERSISTENT)
	elem->rootDef->cd_flags |= CDMODIFIED;

    for (stylePtr = elem->stylelist; stylePtr != NULL; stylePtr = stylePtr->next)
    {
	freeMagic(stylePtr);
    }
    if (elem->type == ELEMENT_TEXT)
	freeMagic(elem->text);

    HashSetValue(entry, NULL);
    freeMagic(elem);

    /* Area of elem->area is set to be updated by dbwElementUndraw().	*/
    /* Can't do WindUpdate(), though, until the hash table entry is	*/
    /* removed, or DBWdisplay will try to draw it again.		*/

    WindUpdate();
}

/*
 * Initialize the Element hash table.
 */

void
dbwElementInit()
{
    HashInit(&elementTable, 10, HT_STRINGKEYS);
    DBWHLAddClient(DBWElementRedraw);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementNames --
 *
 *	Go through the hash table and print the names of all elements
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementNames()
{
    DBWElement *elem;
    HashSearch hs;
    HashEntry *he;

#ifndef MAGIC_WRAPPER
    TxPrintf(stdout, "Known elements:");
#endif

    HashStartSearch(&hs);
    while (he = HashNext(&elementTable, &hs))
    {
	if (elem = (DBWElement *)HashGetValue(he))
	{
#ifdef MAGIC_WRAPPER
	    Tcl_AppendElement(magicinterp, he->h_key.h_name);
#else
	    TxPrintf(stdout, " %s", he->h_key.h_name);
#endif
	}
    }

#ifndef MAGIC_WRAPPER
    TxPrintf(stdout, "/n");
#endif

}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementInbox --
 *
 *	Find the element that is nearest the box.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementInbox(area)
    Rect *area;
{
    DBWElement *elem;
    HashSearch hs;
    HashEntry *he;
    int sqdist;

#ifndef MAGIC_WRAPPER
    TxPrintf(stdout, "Element(s) inside box: ");
#endif

    HashStartSearch(&hs);
    while (he = HashNext(&elementTable, &hs))
    {
	if (elem = (DBWElement *)HashGetValue(he))
	{
	    if (GEO_SURROUND(area, &elem->area))
	    {
#ifdef MAGIC_WRAPPER
		Tcl_AppendElement(magicinterp, he->h_key.h_name);
#else
		TxPrintf(stdout, " %s", he->h_key.h_name);
#endif
	    }
	}
    }

#ifndef MAGIC_WRAPPER
    TxPrintf(stdout, "/n");
#endif
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementAdd* --
 *
 * 	Adds a new element to the element hash table.
 *
 * Results:
 *	None, except
 *	DBWElementAdd(): returns Pointer to a DBWElement structure.
 *
 * Side effects:
 *	CellDef's ancestors are searched until its first root definition
 *	is found, and the coordinates of area are transformed into the
 *	root.  Then the area is added to the element structure.
 *	This stuff will be displayed on the screen at the end of the
 *	current command.
 * ----------------------------------------------------------------------------
 */

/* Set up everything is generic to all element types */

DBWElement *
DBWElementAdd(w, name, area, cellDef, style)
    MagWindow *w;
    char *name;			/* Name of this element for the hash table */
    Rect *area;			/* The area of the element */
    CellDef *cellDef;		/* The cellDef in whose coordinates area
				 * is given.
				 */
    int style;			/* An initial display style to use */
{
    Transform transform;
    DBWElement *elem;
    HashEntry *entry;
    extern int dbwelemGetTransform();	/* Forward declaration. */

    /* Find a transform from this cell to the root, and use it to
     * transform the area.  If the root isn't an ancestor, just
     * return.
     */
    
    if (!DBSrRoots(cellDef, &GeoIdentityTransform,
		dbwelemGetTransform, (ClientData) &transform))
	if (w != NULL)
	    return NULL;

    /* SigInterruptPending screws up DBSrRoots */
    if (SigInterruptPending)
	return NULL;
  
    /* If there is already an entry by this name, delete it */
    DBWElementDelete(w, name);

    entry = HashFind(&elementTable, name);
    elem = (DBWElement *)mallocMagic(sizeof(DBWElement));
    HashSetValue(entry, elem);

    GeoCanonicalRect(area, &elem->area);
    elem->stylelist = (styleptr)mallocMagic(sizeof(stylestruct));
    elem->stylelist->style = style;
    elem->stylelist->next = NULL;

    /* For .mag file loads, w will be NULL and cellDef will be	*/
    /* the root.						*/
    if (w == NULL)
        elem->rootDef = cellDef;
    else
	elem->rootDef = dbwelemRootDef;
    elem->text = NULL;
    elem->flags = 0;

    return elem;
}

void
DBWElementAddRect(w, name, area, cellDef, style)
    MagWindow *w;
    char *name;			/* Name of this element for the hash table */
    Rect *area;			/* The area to be highlighted. */
    CellDef *cellDef;		/* The cellDef in whose coordinates area
				 * is given.
				 */
    int style;			/* An initial display style to use */
{
    DBWElement *elem;

    elem = DBWElementAdd(w, name, area, cellDef, style);
    if (elem == NULL) return;
    elem->type = ELEMENT_RECT;
}

void
DBWElementAddLine(w, name, area, cellDef, style)
    MagWindow *w;
    char *name;			/* Name of this element for the hash table */
    Rect *area;			/* The area to be highlighted. */
    CellDef *cellDef;		/* The cellDef in whose coordinates area
				 * is given.
				 */
    int style;			/* An initial display style to use */
{
    DBWElement *elem;

    elem = DBWElementAdd(w, name, area, cellDef, style);
    if (elem == NULL) return;
    elem->type = ELEMENT_LINE;
}

void
DBWElementAddText(w, name, x, y, text, cellDef, style)
    MagWindow *w;
    char *name;			/* Name of this element for the hash table */
    int x, y;			/* Point of origin (x, y coordinates) */
    char *text;			/* The text of the label */
    CellDef *cellDef;		/* The cellDef in whose coordinates area
				 * is given.
				 */
    int style;			/* An initial display style to use */
{
    DBWElement *elem;
    Rect area;
 
    area.r_xbot = x;
    area.r_xtop = x;
    area.r_ybot = y;
    area.r_ytop = y;

    elem = DBWElementAdd(w, name, &area, cellDef, style);
    if (elem == NULL) return;
    elem->type = ELEMENT_TEXT;
    elem->text = StrDup((char **)NULL, text);
    elem->flags |= (GEO_CENTER << 4);		/* default centered */
    elem->flags |= (GR_TEXT_MEDIUM << 1);	/* default medium size */
}

/* This utility procedure is invoked by DBSrRoots.  Save the root definition
 * in dbwRootDef, save the transform in the argument, and abort the search.
 * Make sure that the root we pick is actually displayed in a window
 * someplace (there could be root cells that are no longer displayed
 * anywhere).
 */

int
dbwelemGetTransform(use, transform, cdarg)
    CellUse *use;			/* A root use that is an ancestor
					 * of cellDef in DBWElementAdd.
					 */
    Transform *transform;		/* Transform up from cellDef to use. */
    Transform *cdarg;			/* Place to store transform from
					 * cellDef to its root def.
					 */
{
    extern int dbwElementAlways1();
    if (use->cu_def->cd_flags & CDINTERNAL) return 0;
    if (!WindSearch((ClientData) DBWclientID, (ClientData) use,
	    (Rect *) NULL, dbwElementAlways1, (ClientData) NULL)) return 0;
    if (SigInterruptPending)
	return 0;
    dbwelemRootDef = use->cu_def;
    *cdarg = *transform;
    return 1;
}

int
dbwElementAlways1()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementText --
 *
 * 	Configures text of a text element
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Text element's text string is reallocated, or string is printed.
 *	If altered, the element is erased and redrawn.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementText(MagWindow *w, char *ename, char *text)
{
    DBWElement *elem;
    HashEntry *entry;

    entry = HashFind(&elementTable, ename);
    if (entry == NULL) 
    {
	TxError("No such element %s\n", ename);
	return;
    }	

    elem = (DBWElement *)HashGetValue(entry);
    if (elem == NULL) return;

    if (elem->type != ELEMENT_TEXT)
    {
	TxError("Element %s is not a text element\n", ename);
	return;
    }

    if (text == NULL)	/* get text */
    {
#ifdef MAGIC_WRAPPER
	Tcl_SetResult(magicinterp, elem->text, NULL);
#else
	TxPrintf("%s\n", elem->text);
#endif
    }
    else  /* replace text */
    {
	dbwElementUndraw(w, elem);
	freeMagic(elem->text);
	elem->text = StrDup((char **)NULL, text);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementParseFlags --
 *
 * 	Configures flags of any element
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Element's flags are set.
 *	If altered, the element is erased and redrawn.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementParseFlags(MagWindow *w, char *ename, char *flagstr)
{
    DBWElement *elem;
    HashEntry *entry;
    int flidx, newflags;

    static char *lineOffset[] = {"halfx", "halfy", "exactx", "exacty",
		"arrowleft", "arrowbottom", "arrowright", "arrowtop",
		"plainleft", "plainbottom", "plainright", "plaintop", NULL};
    static char *textSizes[] = {"small", "medium", "large", "xlarge",
	"default", NULL}; 
    static char *genFlags[] = {"persistent", "temporary", NULL}; 

    entry = HashFind(&elementTable, ename);
    if (entry == NULL)
    {
	TxError("No such element %s\n", ename);
	return;
    }	

    elem = (DBWElement *)HashGetValue(entry);
    if (elem == NULL) return;
    newflags = elem->flags;

    /* Return list of known flags */
    if (flagstr == NULL)
    {
#ifdef MAGIC_WRAPPER
	Tcl_AppendElement(magicinterp, "(flags)");
#else
	TxPrintf("%s\n", "(flags)");
#endif
    }
    else
    {
	/* Parse string for known flags */
	flidx = Lookup(flagstr, genFlags);
	switch (flidx)
	{
	    case 0:
		newflags |= DBW_ELEMENT_PERSISTENT;
		break;
	    case 1:
		newflags &= ~DBW_ELEMENT_PERSISTENT;
		break;
	    default:
		switch (elem->type)
		{
		    case ELEMENT_TEXT:
			flidx = Lookup(flagstr, textSizes);
			if (flidx >= 0)
			{
			    newflags &= ~DBW_ELEMENT_TEXT_SIZE;
			    newflags |= (flidx << 1) & DBW_ELEMENT_TEXT_SIZE;
			}
			else
			{
			    flidx = GeoNameToPos(flagstr, FALSE, FALSE);
			    if (flidx >= 0)
			    {
				newflags &= ~DBW_ELEMENT_TEXT_POS;
				newflags |= (flidx << 4) & DBW_ELEMENT_TEXT_POS;
			    }
			    else
				TxError("No such text element flag \"%s\"\n",
					flagstr);
			}
			break;
		    case ELEMENT_LINE:
			flidx = Lookup(flagstr, lineOffset);
			switch(flidx)
			{
			    case 0:
				newflags |= DBW_ELEMENT_LINE_HALFX;
				break;
			    case 1:
				newflags |= DBW_ELEMENT_LINE_HALFY;
				break;
			    case 2:
				newflags &= ~(DBW_ELEMENT_LINE_HALFX);
				break;
			    case 3:
				newflags &= ~(DBW_ELEMENT_LINE_HALFY);
				break;
			    case 4:
			    case 5:
				newflags |= DBW_ELEMENT_LINE_ARROWL;
				break;
			    case 6:
			    case 7:
				newflags |= DBW_ELEMENT_LINE_ARROWR;
				break;
			    case 8:
			    case 9:
				newflags &= ~(DBW_ELEMENT_LINE_ARROWL);
				break;
			    case 10:
			    case 11:
				newflags &= ~(DBW_ELEMENT_LINE_ARROWR);
				break;
			    default:
				TxError("No such line element flag \"%s\"\n",
					flagstr);
				break;
			}
			break;
		    case ELEMENT_RECT:
			TxError("No such rect element flag \"%s\"\n",
					flagstr);
			break;
		}
		break;
	}

	if (newflags != elem->flags)
	{
	    dbwElementUndraw(w, elem);

	    /* Mark element's cell as having been modified either if	*/
	    /* it is persistent or if its persistence has changed.	*/

	    if (elem->flags & DBW_ELEMENT_PERSISTENT ||
			newflags & DBW_ELEMENT_PERSISTENT)
		elem->rootDef->cd_flags |= CDMODIFIED;

	    elem->flags = newflags;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementStyle --
 *
 * 	Configures style of any element
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Element's style list is reallocated, or printed.
 *	If altered, the element is erased and redrawn.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementStyle(MagWindow *w, char *ename, int style, bool add)
{
    DBWElement *elem;
    HashEntry *entry;
    styleptr sptr, newstyle;

    entry = HashFind(&elementTable, ename);
    if (entry == NULL)
    {
	TxError("No such element %s\n", ename);
	return;
    }	

    elem = (DBWElement *)HashGetValue(entry);
    if (elem == NULL) return;

    if (style== -1)	/* get style(s) */
    {
#ifdef MAGIC_WRAPPER
	for (sptr = elem->stylelist; sptr != NULL; sptr = sptr->next)
	{
	    Tcl_AppendElement(magicinterp, GrStyleTable[sptr->style].longname);
	}
#else
	for (sptr = elem->stylelist; sptr != NULL; sptr = sptr->next)
	{
	    TxPrintf("%s ", GrStyleTable[sptr->style].longname);
	}
	TxPrintf("\n");
#endif
    }
    else
    {
	dbwElementUndraw(w, elem);
	if (add == TRUE)
	{
	    /* add style */
	    for (sptr = elem->stylelist; sptr != NULL && sptr->next != NULL;
			sptr = sptr->next);
	    
	    newstyle = (styleptr)mallocMagic(sizeof(stylestruct));
	    newstyle->style = style;
	    newstyle->next = NULL;
	    if (sptr == NULL)
		elem->stylelist = newstyle;
	    else
		sptr->next = newstyle;
	}
	else
	{
	    /* find style in list */
	    for (sptr = elem->stylelist; sptr != NULL; sptr = sptr->next)
	    {
		if (sptr->next != NULL)
		    if (sptr->next->style == style)
			break;
	    }

	    /* delete style (if it is in the list) */

	    if ((sptr == NULL) && (elem->stylelist != NULL) &&
			(elem->stylelist->style == style))
	    {
		dbwElementUndraw(w, elem);
		freeMagic(elem->stylelist);
		elem->stylelist = elem->stylelist->next;
		if (elem->stylelist == NULL)
		    TxPrintf("Warning:  Element %s has no styles!\n", ename);
	    }
	    else if (sptr == NULL)
	    {
		TxError("Style %d is not in the style list for element %s\n",
			style, ename);
	    }
	    else if (sptr->next != NULL)
	    {
		dbwElementUndraw(w, elem);
		freeMagic(sptr->next);
		sptr->next = sptr->next->next;
	    }
	}
	/* mark element's cell as having been modified */
	if (elem->flags & DBW_ELEMENT_PERSISTENT)
	    elem->rootDef->cd_flags |= CDMODIFIED;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWElementPos --
 *
 * 	Configures position of a rect or line element
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Rect or line element's rect structure is altered, or else the
 *	position is printed.  If altered, the element is erased and redrawn.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWElementPos(MagWindow *w, char *ename, Rect *crect)
{
    DBWElement *elem;
    HashEntry *entry;
    Rect prect;
    char ptemp[22];

    entry = HashFind(&elementTable, ename);
    if (entry == NULL)
    {
	TxError("No such element %s\n", ename);
	return;
    }	

    elem = (DBWElement *)HashGetValue(entry);
    if (elem == NULL) return;

    if (crect == NULL)	/* Get position */
    {
#ifdef MAGIC_WRAPPER
	snprintf(ptemp, 20, "%d", elem->area.r_xbot);
	Tcl_AppendElement(magicinterp, ptemp);
	snprintf(ptemp, 20, "%d", elem->area.r_ybot);
	Tcl_AppendElement(magicinterp, ptemp);
	if (elem->type == ELEMENT_RECT || elem->type == ELEMENT_LINE)
	{
	    snprintf(ptemp, 20, "%d", elem->area.r_xtop);
	    Tcl_AppendElement(magicinterp, ptemp);
	    snprintf(ptemp, 20, "%d", elem->area.r_ytop);
	    Tcl_AppendElement(magicinterp, ptemp);
	}
#else
	TxPrintf("(%d, %d)", elem->area.r_xbot, elem->area.r_ybot);

	if (elem->type == ELEMENT_RECT || elem->type == ELEMENT_LINE)
	    TxPrintf(" to (%d, %d)", elem->area.r_xtop, elem->area.r_ytop);

	TxPrintf("\n");
#endif
    }
    else		/* Change position */
    {
	dbwElementUndraw(w, elem);
	elem->area = *crect;
	/* mark element's cell as having been modified */
	if (elem->flags & DBW_ELEMENT_PERSISTENT)
	    elem->rootDef->cd_flags |= CDMODIFIED;
    }
}
