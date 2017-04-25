/*
 * DBcellname.c --
 *
 * CellUse/CellDef creation, deletion, naming.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellname.c,v 1.5 2009/07/27 00:56:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "select/select.h"
#include "utils/signals.h"
#include "utils/undo.h"
#include "utils/malloc.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "drc/drc.h"

/*
 * Hash tables for CellDefs and CellUses
 */

#define	NCELLDEFBUCKETS	64	/* Initial number of buckets for CellDef tbl */
#define	NCELLUSEBUCKETS	128	/* Initial number of buckets for CellUse tbl */

HashTable dbCellDefTable;
HashTable dbUniqueDefTable;
HashTable dbUniqueNameTable;
bool dbWarnUniqueIds;

/*
 * Routines used before defined
 */
extern CellDef *DBCellDefAlloc();
extern int dbLinkFunc();
extern void dbUsePrintInfo();
extern void DBsetUseIdHash();
extern void DBUnLinkCell();

/*
 * Routines from other database modules
 */

extern void DBSetUseIdHash();

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellRename --
 *
 *	Rename a cell.  This differs from the "save" command in that the
 *	cell is not immediately written to disk.  However, the cell is
 *	marked as modified.
 *
 * Results:
 *	Return TRUE if the cell was successfully renamed.  Return FALSE
 *	otherwise.
 *
 * Side Effects:
 *	Cellname is changed.
 *
 * ----------------------------------------------------------------------------
 */
bool
DBCellRename(cellname, newname)
    char *cellname;
    char *newname;
{
    HashEntry *entry;
    CellDef *celldef;
    bool result;

    entry = HashLookOnly(&dbCellDefTable, cellname);
    if (entry == NULL)
    {
	TxError("No such cell \"%s\"\n",  cellname);
	return FALSE;
    }

    celldef = (CellDef *)HashGetValue(entry);
    if (celldef == NULL) return FALSE;

    if ((celldef->cd_flags & CDINTERNAL) == CDINTERNAL)
    {
	TxError("Attempt to rename internal cell \"%s\"\n", cellname);
	return FALSE;
    }

    /* Good to go! */

    UndoDisable();
    result = DBCellRenameDef(celldef, newname);
    DBWAreaChanged(celldef, &celldef->cd_bbox, DBW_ALLWINDOWS,
        (TileTypeBitMask *) NULL);
    UndoEnable();
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBEnumerateTypes
 *
 *	Generate a mask of all known types in all known cells.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Returns the OR of the layer mask of each cell definition in
 *	the pointer to rMask.
 *
 * Notes:
 *	cd_types needs to be properly handled through paint/erase
 *	operations, or else we should not rely on cd_types.
 * ----------------------------------------------------------------------------
 */

void
DBEnumerateTypes(rMask)
    TileTypeBitMask *rMask;
{
    HashSearch hs;
    HashEntry *entry;
    CellDef *cellDef;
    
    TTMaskZero(rMask);
    HashStartSearch(&hs);
    while ((entry = HashNext(&dbCellDefTable, &hs)) != NULL)
    {
	cellDef = (CellDef *) HashGetValue(entry);
	if (cellDef != (CellDef *)NULL)
	    if ((cellDef->cd_flags & CDINTERNAL) != CDINTERNAL)
		TTMaskSetMask(rMask, &cellDef->cd_types);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellDelete --
 *
 *	Destroy a cell.
 *
 * Results:
 *	Return TRUE if the cell was successfully eliminated.  Return FALSE
 *	if the cell could not be deleted (i.e., has dependencies).
 *
 * Side Effects:
 *	Frees memory.  May prompt the user to save, if the cell has been
 *	changed.
 *
 * ----------------------------------------------------------------------------
 */
bool
DBCellDelete(cellname, force)
    char *cellname;
    bool force;
{
    HashEntry *entry;
    CellDef *celldef;
    CellUse *celluse;
    bool result;

    entry = HashLookOnly(&dbCellDefTable, cellname);
    if (entry == NULL)
    {
	TxError("No such cell \"%s\"\n",  cellname);
	return FALSE;
    }

    celldef = (CellDef *)HashGetValue(entry);
    if (celldef == NULL) return FALSE;

    if ((celldef->cd_flags & CDINTERNAL) == CDINTERNAL)
    {
	TxError("Attempt to delete internal cell \"%s\"\n", cellname);
	return FALSE;
    }

    /* If the cell has any instances which are not top-level	*/
    /* uses, don't allow the deletion.				*/

    for (celluse = celldef->cd_parents; celluse != (CellUse *) NULL;
	 	celluse = celluse->cu_nextuse)
	if (celluse->cu_parent != (CellDef *) NULL)
	    if ((celluse->cu_parent->cd_flags & CDINTERNAL) != CDINTERNAL)
		break;

    if (celluse != (CellUse *)NULL)
    {
	TxError("Cell has non-top-level dependency in use \"%s\"\n",
		celluse->cu_id);
	return FALSE;
    }

    /* Cleared to delete. . . now prompt user if the cell has changes. */
    /* Last chance to save! */

    if ((force == FALSE) &&
	(celldef->cd_flags & (CDMODIFIED|CDBOXESCHANGED|CDSTAMPSCHANGED)))
    {
	static char *yesno[] = { "no", "yes", 0 };
	int code;
	char *prompt = TxPrintString("Cell %s has been modified.\n  Do you"
		" want to delete it and lose all changes? ",
		cellname);
	code = TxDialog(prompt, yesno, 0);
	if (code == 0) return FALSE;
    }

    /* If we're destroying the cell pointed to by dbUndoLastCell, we	*/
    /* need to NULL it and flush the Undo queue.			*/

    DBUndoReset(celldef);

    /* If we're destroying "UNNAMED", then we want to rename it first	*/
    /* so that WindUnload() will create a new one.			*/ 

    if (!strcmp(cellname, UNNAMED))
	DBCellRename(cellname, "__UNNAMED__");

    /* For all top-level cell uses, check if any windows have this	*/
    /* use.  If so, load the window with (UNNAMED).			*/

    UndoDisable();
    for (celluse = celldef->cd_parents; celluse != (CellUse *) NULL;
	 	celluse = celluse->cu_nextuse)
    {
	if (celluse->cu_parent == (CellDef *) NULL)
	{
	    WindUnload(celluse);
	    freeMagic(celluse->cu_id);
	}
	freeMagic((char *)celluse);
    }
    celldef->cd_parents = (CellUse *)NULL;

    result = DBCellDeleteDef(celldef);

    if (result == FALSE)
	TxError("Error:  Deleted all cell uses, but could not delete cell.\n");

    UndoEnable();
    return result;
}



/*
 * ----------------------------------------------------------------------------
 *
 * DBCellInit --
 *
 * Initialize the world of the cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up the symbol tables for CellDefs and CellUses.
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellInit()
{
    HashInit(&dbCellDefTable, NCELLDEFBUCKETS, 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbGetUseName --
 *
 *	Returns the name of the indicated cell use.  If the use is part of
 *	an array, it returns the largest array member in x, y, or both.
 *
 * Results:
 *	An allocated char * array which must be free'd using freeMagic().
 *
 * ----------------------------------------------------------------------------
 */
 
char *
dbGetUseName(celluse)
   CellUse *celluse;
{
    char *useID, *newID, xbuf[10], ybuf[10];
    int arxl, aryl, arxh, aryh;
    int newsize;
    bool isx, isy;

    arxl = celluse->cu_array.ar_xlo;
    aryl = celluse->cu_array.ar_ylo;
    arxh = celluse->cu_array.ar_xhi;
    aryh = celluse->cu_array.ar_yhi;

    isx = (arxh == arxl) ? FALSE : TRUE;
    isy = (aryh == aryl) ? FALSE : TRUE;

    xbuf[0] = '\0';
    ybuf[0] = '\0';

    useID = celluse->cu_id;
    newsize = strlen(useID) + 1;
    if (isx || isy)
    {
	newsize += 4;
	if (isx && isy) newsize += 1;
	if (isx)
	{
	    snprintf(xbuf, 9, "%d", arxl);
	    newsize += strlen(xbuf);
	}
	if (isy)
	{
	    snprintf(ybuf, 9, "%d", aryl);
	    newsize += strlen(ybuf);
	}
    }
   
    newID = (char *)mallocMagic(newsize);
    strcpy(newID, useID);
    if (isx || isy)
    {
	strcat(newID, "\\[");
	if (isx) strcat(newID, xbuf);
	if (isx && isy) strcat(newID, ",");
	if (isy) strcat(newID, ybuf);
	strcat(newID, "\\]");
    }
    return (newID);
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbCellPrintInfo --
 *
 * 	This is the working function for DBCellPrint.
 *
 * Results:
 *	None (Tcl returns list if "dolist" is true in magic Tcl version).
 *
 * Side effects:
 *	Stuff is printed.
 *
 * ----------------------------------------------------------------------------
 */

void
dbCellPrintInfo(StartDef, who, dolist)
    CellDef *StartDef;
    int who;
    bool dolist;
{
    HashSearch hs;
    HashEntry *entry;
    CellDef *celldef;
    CellUse *celluse;
    char *cu_name;
    bool topdone;

    switch (who) {
	case SELF:

	    if (StartDef->cd_name == NULL)
	    {
	        if (dolist)
#ifdef MAGIC_WRAPPER
		    Tcl_AppendElement(magicinterp, "1");
#else
		    TxPrintf("TRUE\n");
#endif
		else
		    TxPrintf("Cell is currently loaded.\n");
	    }
	    else
	    {
		if (dolist)
#ifdef MAGIC_WRAPPER
		    Tcl_AppendElement(magicinterp, StartDef->cd_name);
#else
		    TxPrintf("%s\n", StartDef->cd_name);
#endif
		else
		    TxPrintf("Cell %s is currently loaded.\n", StartDef->cd_name);
	    }
	    break;

	case OTHER:
	    if (!dolist)
		TxPrintf("Names of cell instances:\n");

	    for (celluse = StartDef->cd_parents; celluse != NULL;
			celluse = celluse->cu_nextuse)
	    {
		if ((celluse->cu_parent != NULL) &&
			((celluse->cu_parent->cd_flags & CDINTERNAL)
			== CDINTERNAL))
		    continue;

		if (celluse->cu_id != NULL)
		{
		    cu_name = dbGetUseName(celluse);
		    if (dolist)
#ifdef MAGIC_WRAPPER
			Tcl_AppendElement(magicinterp, cu_name);
#else
			TxPrintf("%s\n", cu_name);
#endif
		    else
			TxPrintf("    %s\n", cu_name);
		    freeMagic(cu_name);
		}
	    }
	    break;

	case CHILDINST:
	    if (!dolist)
		TxPrintf("Def %s's children are:\n", StartDef->cd_name);

	    HashStartSearch(&hs);
	    while ((entry = HashNext(&StartDef->cd_idHash, &hs)) != NULL)
	    {
		celluse = (CellUse *)HashGetValue(entry);
		if (celluse != (CellUse *)NULL)
		    dbCellUsePrintFunc(celluse, &dolist);
	    }
	    break;

	case PARENTS:

	    /*
	     *
	     * Print out a list of all the parents by scanning the 'use' list.
	     *
	     */

	    if (StartDef->cd_name == NULL && (!dolist))
	    {
		TxPrintf("Cell's parents are:\n");
	    }
	    else if (!dolist)
	    {
		TxPrintf("Cell %s's parents are:\n", StartDef->cd_name);
	    }
	    for (celluse = StartDef->cd_parents; celluse != (CellUse *) NULL;
			celluse = celluse->cu_nextuse)
	    {
		if (celluse->cu_parent != (CellDef *) NULL)
		{
		    celluse->cu_parent->cd_client = (ClientData) 1;
		}
	    }
	    for (celluse = StartDef->cd_parents; celluse != (CellUse *) NULL;
			celluse = celluse->cu_nextuse)
	    {
		if (celluse->cu_parent != (CellDef *) NULL)
		{
		    if (celluse->cu_parent->cd_client == (ClientData) 1)
		    {
			celluse->cu_parent->cd_client = (ClientData) NULL;
			if ( (celluse->cu_parent->cd_flags & CDINTERNAL)
					!= CDINTERNAL)
			{
			    if (dolist)
#ifdef MAGIC_WRAPPER
				Tcl_AppendElement(magicinterp,
					celluse->cu_parent->cd_name);
#else
				TxPrintf("%s ", celluse->cu_parent->cd_name);
#endif
			    else
				TxPrintf("    %s\n", celluse->cu_parent->cd_name);
			}
		    }
		}
	    }
	    break;

	case CHILDREN:

	    /*
	     *
	     * Print out a list of all the children by checking all the cells.
	     *
	     */

	    if (StartDef->cd_name == NULL && (!dolist))
	    {
		TxPrintf("Cell's children are:\n");
	    }
	    else if (!dolist)
	    {
		TxPrintf("Cell %s's children are:\n", StartDef->cd_name);
	    }
	    HashStartSearch(&hs);
	    while( (entry = HashNext(&dbCellDefTable, &hs)) != NULL)
	    {
		celldef = (CellDef *) HashGetValue(entry);
		if (!celldef) continue;
		for (celluse = celldef->cd_parents; celluse != (CellUse *) NULL;
			celluse = celluse->cu_nextuse)
		{
		    if (celluse->cu_parent == StartDef)
		    {
			if (dolist)
#ifdef MAGIC_WRAPPER
			    Tcl_AppendElement(magicinterp, celldef->cd_name);
#else
		            TxPrintf("%s ", celldef->cd_name);
#endif
			else
		            TxPrintf("    %s\n", celldef->cd_name);
			break;
		    }
		}
	    }
	    break;
    }  /* endswitch */
}



/*
 * ----------------------------------------------------------------------------
 *
 * DBTopPrint --
 *
 *	This routine prints the cell definition name of the topmost instance
 *	in the window.
 *
 * ----------------------------------------------------------------------------
 */
void
DBTopPrint(mw, dolist)
    MagWindow *mw;
    bool dolist;
{
    CellDef *celldef;
    CellUse *celluse;

    if (mw == NULL)
    {
	TxError("No window was selected for search.\n");
    }
    else
    {
	celluse = (CellUse *)mw->w_surfaceID;
	celldef = celluse->cu_def;

	if (celldef == NULL) return;
#ifdef MAGIC_WRAPPER
	if (dolist)
	    Tcl_AppendElement(magicinterp, celldef->cd_name);
	else
#endif
	TxPrintf("Top-level cell in the window is: %s\n", celldef->cd_name);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellPrint --
 *
 * 	This routine prints out cell names.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff is printed.
 *
 * Notes: "who" takes one of the options defined in database.h:
 *	PARENTS, CHILDREN, SELF, OTHER, ALLCELLS, MODIFIED, or TOPCELLS.
 *	"SELF" lists celldef names (most useful to list the name of
 *	a selected cell).  "OTHER" lists instance names.
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellPrint(CellName, who, dolist)
    char *CellName;
    int who;
    bool dolist;
{
    int found;
    HashSearch hs;
    HashEntry *entry;
    CellDef *celldef;
    CellUse *celluse;
    
    if (!dolist)
    {
	switch (who)
	{
	    case ALLCELLS:
		TxPrintf("Cell currently loaded:\n");
		break;
	    case MODIFIED:
		TxPrintf("Modified cells:\n");
		break;
	    case TOPCELLS:
		TxPrintf("Top level cells are:\n");
		break;
	}
    }

    switch (who)
    {
	case ALLCELLS:
	case MODIFIED:
	    /*
	     * Print the name of all the 'known' cells.
	     * If "MODIFIED", print only those cells that have the
	     * CDMODIFIED flag set.
	     */

	    HashStartSearch(&hs);
	    while( (entry = HashNext(&dbCellDefTable, &hs)) != NULL)
	    {
		celldef = (CellDef *) HashGetValue(entry);
		if (celldef != (CellDef *) NULL)
		{
		    if (((celldef->cd_flags & CDINTERNAL) != CDINTERNAL) &&
				((who != MODIFIED) ||
				(celldef->cd_flags & CDMODIFIED)))
		    {
			if (celldef->cd_name != NULL)
			{
			    if (dolist)
#ifdef MAGIC_WRAPPER
			        Tcl_AppendElement(magicinterp, celldef->cd_name);
#else
			        TxPrintf("%s ", celldef->cd_name);
#endif
			    else
			        TxPrintf("    %s\n", celldef->cd_name);
		        }
		    }
	        }
	    }
	    break;

	case TOPCELLS:
	    /*
	     * Print the name of all the 'top' cells.
	     */

	    HashStartSearch(&hs);
	    while( (entry = HashNext(&dbCellDefTable, &hs)) != NULL)
	    {
		celldef = (CellDef *) HashGetValue(entry);
		if (celldef != (CellDef *) NULL)
		{
		    if ( (celldef->cd_flags & CDINTERNAL) != CDINTERNAL)
		    {
			found = 0;
			for (celluse = celldef->cd_parents;
			 	celluse != (CellUse *) NULL;
			 	celluse = celluse->cu_nextuse)
		        {
			    if (celluse->cu_parent != (CellDef *) NULL)
			    {
			        if ( (celluse->cu_parent->cd_flags & CDINTERNAL)
					!= CDINTERNAL)
			        {
				    found = 1;
				    break;
			        }
			    }
		        }
		        if ( (found == 0) && (celldef->cd_name != NULL) )
		        {
			    if (dolist)
#ifdef MAGIC_WRAPPER
			        Tcl_AppendElement(magicinterp, celldef->cd_name);
#else
			        TxPrintf("%s ", celldef->cd_name);
#endif
			    else
			        TxPrintf("    %s\n", celldef->cd_name);
		        }
		    }
	        }
	    }
	    break;

	default:

	   /*
	    * Check to see if a cell name was specified. If not,
	    * search for selected cells.
	    */

	    if (CellName == NULL)
	    {
		found = 0;
		HashStartSearch(&hs);
		while( (entry = HashNext(&dbCellDefTable, &hs)) != NULL)
		{
		    celldef = (CellDef *) HashGetValue(entry);
		    if (celldef != (CellDef *) NULL)
		    {
			for (celluse = celldef->cd_parents;
		     		celluse != (CellUse *) NULL;
		     		celluse = celluse->cu_nextuse)
			{
			    if (celluse->cu_parent == SelectDef)
			    {
				dbCellPrintInfo(celldef, who, dolist);
				found = 1;
				break;
			    }
			}
		    }
		}
		if (found == 0)
		{
		    if (!dolist)
			TxPrintf("No cells selected.\n");
		}
	    }
	    else
	    {
		celldef = DBCellLookDef(CellName);
		if (celldef == (CellDef *) NULL)
		{
		    if (dolist)
#ifdef MAGIC_WRAPPER
			Tcl_AppendElement(magicinterp, "0");
#else
			TxPrintf("Not loaded\n");
#endif
		    else
		        TxError("Cell %s is not currently loaded.\n", CellName);
		}
		else
		{
		    dbCellPrintInfo(celldef, who, dolist);
		}
   	    }
	    break;
    } /* endswitch */
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbUsePrintInfo --
 *
 * 	This is the working function for DBUsePrint.
 *
 * Results:
 *	None (Tcl returns list if "dolist" is true in magic Tcl version).
 *
 * Side effects:
 *	Stuff is printed.
 *
 * ----------------------------------------------------------------------------
 */

void
dbUsePrintInfo(StartUse, who, dolist)
    CellUse *StartUse;
    int who;
    bool dolist;
{
    CellDef *celldef;
    CellUse *celluse;
    char *cu_name;
    HashSearch hs;
    HashEntry *entry;

    int dbCellUsePrintFunc();

    /* fprintf(stderr, "dbCellUseInfo called with %s, %d, %s\n",
     *	StartUse->cu_id == NULL ? "(unnamed)" : StartUse->cu_id,
     *	who, (dolist) ? "list" : "no list" );
     * fflush(stderr);
     */

    switch (who) {
	case SELF:
	    if (StartUse->cu_id == NULL)
	    {
	        if (dolist)
#ifdef MAGIC_WRAPPER
		    Tcl_AppendElement(magicinterp, "1");
#else
		    TxPrintf("TRUE\n");
#endif
		else
		    TxPrintf("Use is currently loaded.\n");
	    }
	    else
	    {
		cu_name = dbGetUseName(StartUse);
		if (dolist)
#ifdef MAGIC_WRAPPER
		    Tcl_AppendElement(magicinterp, cu_name);
#else
		    TxPrintf("%s\n", cu_name);
#endif
		else
		    TxPrintf("Use %s is currently loaded.\n", cu_name);
		freeMagic(cu_name);
	    }
	    break;

	case OTHER:
	    if (StartUse->cu_def->cd_name == NULL)
	    {
	        if (dolist)
#ifdef MAGIC_WRAPPER
		    Tcl_AppendElement(magicinterp, "0");
#else
		    TxPrintf("FALSE\n");
#endif
		else
		    TxPrintf("Cell definition has no name.\n");
	    }
	    else
	    {
	        if (dolist)
#ifdef MAGIC_WRAPPER
		    Tcl_AppendElement(magicinterp, StartUse->cu_def->cd_name);
#else
		    TxPrintf("%s\n", StartUse->cu_def->cd_name);
#endif
		else
		    TxPrintf("Cell definition is %s.\n", StartUse->cu_def->cd_name);
	    }
	    break;

	case PARENTS:

	    /*
	     *
	     * Print out a list of all the parents by scanning the 'use' list.
	     *
	     */

	    if (StartUse->cu_id == NULL && (!dolist))
	    {
		TxPrintf("Use's parent is:\n");
	    }
	    else if (!dolist)
	    {
		cu_name = dbGetUseName(StartUse);
		TxPrintf("Use %s's parent is:\n", cu_name);
		freeMagic(cu_name);
	    }
	    if (StartUse->cu_parent != (CellDef *) NULL)
	    {
		if ((StartUse->cu_parent->cd_flags & CDINTERNAL)
					!= CDINTERNAL)
		{
		    if (dolist)
#ifdef MAGIC_WRAPPER
			Tcl_AppendElement(magicinterp,
				StartUse->cu_parent->cd_name);
#else
			TxPrintf("%s ", StartUse->cu_parent->cd_name);
#endif
		    else
			TxPrintf("    %s\n", StartUse->cu_parent->cd_name);
		}
	    }
	    break;

	case CHILDREN:

	    /*
	     *
	     * Print out a list of all the children by checking all the cells.
	     *
	     */

	    if (StartUse->cu_id == NULL && (!dolist))
	    {
		TxPrintf("Use's children are:\n");
	    }
	    else if (!dolist)
	    {
		cu_name = dbGetUseName(StartUse);
		TxPrintf("Use %s's children are:\n", cu_name);
		freeMagic(cu_name);
	    }
	    celldef = StartUse->cu_def;
	    HashStartSearch(&hs);
	    while ((entry = HashNext(&celldef->cd_idHash, &hs)) != NULL)
	    {
		celluse = (CellUse *)HashGetValue(entry);
		if (celluse != (CellUse *)NULL)
		    dbCellUsePrintFunc(celluse, &dolist);
	    }
	    break;
    }  /* endswitch */
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBUsePrint --
 *
 * 	This routine prints out cell use names.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff is printed.
 *
 * Notes: "who" takes one of the options defined in database.h:
 *	PARENTS, CHILDREN, SELF, OTHER, or ALLCELLS.
 *	"SELF" lists instance names (most useful to list the name of
 *	a selected instance).  "OTHER" lists the celldef name of the
 *	instance.
 *
 *	CellName should be referenced either to the current edit cell,
 *	if it is not a hierarchical name;  otherwise, if it is a
 *	hierarchical name, the instance before the last '/' is mapped
 *	to its cellDef, and that cellDef is searched for the indicated
 *	instance.  
 *
 * ----------------------------------------------------------------------------
 */

void
DBUsePrint(CellName, who, dolist)
    char *CellName;
    int who;
    bool dolist;
{
    int found;
    HashSearch hs;
    HashEntry *entry;
    CellDef *celldef;
    CellUse *celluse;
    char *lasthier;

    int dbCellUsePrintFunc();
    
    if ((CellName != NULL) && ((lasthier = strrchr(CellName, '/')) != NULL))
    {
	char *prevhier;
	*lasthier = '\0';
	prevhier = strrchr(CellName, '/');
	if (prevhier == NULL)
	    prevhier = CellName;	
	else
	    prevhier++;

	celldef = DBCellLookDef(CellName);
	*lasthier = '/';
    }
    else
    {
	/* Referenced cellDef is the current edit def */
	celldef = EditCellUse->cu_def;
    }

    switch (who)
    {
	case ALLCELLS:
	    /*
	     * Print the name of all the 'known' cell uses (hierarchical names).
	     */

	    break;

	default:

	   /*
	    *
	    * Check to see if a cell name was specified. If not,
	    * search for selected cells.
	    *
	    */

	    if (CellName == NULL)
	    {
		found = 0;
		HashStartSearch(&hs);
		while( (entry = HashNext(&dbCellDefTable, &hs)) != NULL)
		{
		    celldef = (CellDef *) HashGetValue(entry);
		    if (celldef != (CellDef *) NULL)
		    {
			for (celluse = celldef->cd_parents;
		     		celluse != (CellUse *) NULL;
		     		celluse = celluse->cu_nextuse)
			{
			    if (celluse->cu_parent == SelectDef)
			    {
				dbUsePrintInfo(celluse, who, dolist);
				found = 1;
			    }
			}
		    }
		}
		if (found == 0)
		    if (!dolist)
		        TxPrintf("No cells selected.\n");
	    }
	    else
	    {
		celluse = DBFindUse(CellName, celldef);

		if (celluse == NULL)
		{
		    if (!dolist)
		        TxError("Cell %s is not currently loaded.\n", CellName);
		}
		else
		{
		    dbUsePrintInfo(celluse, who, dolist);
		}
	    }
	    break;

    } /* endswitch */
}

int
dbCellUsePrintFunc(cellUse, dolist)
    CellUse *cellUse;
    bool *dolist;
{
    char *cu_name;

    if (cellUse->cu_id != NULL)
    {
	cu_name = dbGetUseName(cellUse);
	if (*dolist)
#ifdef MAGIC_WRAPPER
            Tcl_AppendElement(magicinterp, cu_name);
#else
	    TxPrintf("%s ", cu_name);
#endif
	else
	    TxPrintf("    %s\n", cu_name);
	freeMagic(cu_name);
    }
    return 0;
}

/*
 *  dbLockUseFunc()
 */

int
dbLockUseFunc(selUse, use, transform, data)
    CellUse *selUse;	/* Use from selection cell */
    CellUse *use;	/* Use from layout corresponding to selection */
    Transform *transform;
    ClientData data;
{
    bool dolock = *((bool *)data);

    if (EditCellUse && !DBIsChild(use, EditCellUse))
    {
	TxError("Cell %s (%s) isn't a child of the edit cell.\n",
		use->cu_id, use->cu_def->cd_name);
	return 0;
    }
    if ((dolock && (use->cu_flags & CU_LOCKED)) ||
		(!dolock && !(use->cu_flags & CU_LOCKED)))
	return 0;	/* nothing to do */

    if (UndoIsEnabled()) DBUndoCellUse(use, UNDO_CELL_LOCKDOWN);
    if (dolock) use->cu_flags |= CU_LOCKED;
    else use->cu_flags &= ~CU_LOCKED;
    if (UndoIsEnabled()) DBUndoCellUse(use, UNDO_CELL_LOCKDOWN);

    if (selUse != NULL)
    {
	if (dolock) selUse->cu_flags |= CU_LOCKED;
	else selUse->cu_flags &= ~CU_LOCKED;
    }

    DBWAreaChanged(use->cu_parent, &use->cu_bbox,
	(int) ~(use->cu_expandMask), &DBAllButSpaceBits);
    DBWHLRedraw(EditRootDef, &selUse->cu_bbox, TRUE);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBLockUse --
 *
 * 	This routine sets or clears a cell instance's lock flag
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	cu_flags changed for indicated cell use.
 *
 * ----------------------------------------------------------------------------
 */

void
DBLockUse(UseName, bval)
    char *UseName;
    bool bval;
{
    int found;
    HashSearch hs;
    HashEntry *entry;
    CellDef *celldef;
    CellUse *celluse;

    int dbUseLockFunc();
    
   /*
    *
    * Check to see if a cell name was specified. If not,
    * search for selected cells.
    *
    */

    if (UseName == NULL)
    {
	if (EditCellUse == NULL)
	    TxError("Cannot set lock in a non-edit cell!\n");
	else
	    SelEnumCells(TRUE, (int *)NULL, (SearchContext *)NULL,
			dbLockUseFunc, (ClientData)&bval);
    }
    else
    {
	SearchContext scx;

	bzero(&scx, sizeof(SearchContext));
	found = 0;

	HashStartSearch(&hs);
	while( (entry = HashNext(&dbCellDefTable, &hs)) != NULL)
	{
	    celldef = (CellDef *) HashGetValue(entry);
	    if (celldef != (CellDef *) NULL)
	    {
		celluse = celldef->cd_parents;   /* only need one */
		if (celluse != (CellUse *)NULL) {
		    DBTreeFindUse(UseName, celluse, &scx);
		    if (scx.scx_use != NULL) break;
		}
	    }
	}

	if (scx.scx_use == NULL)
	    TxError("Cell %s is not currently loaded.\n", UseName);
	else
	    dbLockUseFunc(NULL, scx.scx_use, NULL, (ClientData)&bval);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellLookDef --
 *
 * Find the definition of the cell with the given name.
 *
 * Results:
 *	Returns a pointer to the CellDef with the given name if it
 *	exists.  Otherwise, returns (CellDef *) NULL.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

CellDef *
DBCellLookDef(cellName)
    char *cellName;
{
    HashEntry *entry;

    entry = HashFind(&dbCellDefTable, cellName);
    return ((CellDef *) HashGetValue(entry));
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBCellNewDef --
 *
 * Create a new cell definition with the given name.  There must not
 * be any cells already known with the same name.
 *
 * Results:
 *	Returns a pointer to the newly created CellDef.  The CellDef
 *	is completely initialized, showing no uses and having all
 *	tile planes initialized via TiNewPlane() to contain a single
 *	space tile.  The filename associated with the cell is set to
 *	the name supplied, but no attempt is made to open it or create
 *	it.
 *
 *	If the cellName supplied is NULL, the cell is entered into
 *	the symbol table with a name of UNNAMED.
 *
 *	Returns NULL if a cell by the given name already exists.
 *
 * Side effects:
 *	The name of the CellDef is entered into the symbol table
 *	of known cells.
 *
 * ----------------------------------------------------------------------------
 */

CellDef *
DBCellNewDef(cellName, cellFileName)
    char *cellName;		/* Name by which the cell is known */
    char *cellFileName;		/* Name of disk file in which the cell 
				 * should be kept when written out.
				 */
{
    CellDef *cellDef;
    HashEntry *entry;

    if (cellName == (char *) NULL)
	cellName = UNNAMED;

    entry = HashFind(&dbCellDefTable, cellName);
    if (HashGetValue(entry) != (ClientData) NULL)
	return ((CellDef *) NULL);

    cellDef = DBCellDefAlloc();
    HashSetValue(entry, (ClientData) cellDef);
    cellDef->cd_name = StrDup((char **) NULL, cellName);
    if (cellFileName == (char *) NULL)
	cellDef->cd_file = cellFileName;
    else
	cellDef->cd_file = StrDup((char **) NULL, cellFileName);
    return (cellDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellDefAlloc --
 *
 * Create a new cell definition structure.  The new def is not added
 * to any symbol tables.
 *
 * Results:
 *	Returns a pointer to the newly created CellDef.  The CellDef
 *	is completely initialized, showing no uses and having all
 *	tile planes initialized via TiNewPlane() to contain a single
 *	space tile.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

CellDef *
DBCellDefAlloc()
{
    CellDef *cellDef;
    int pNum;

    cellDef = (CellDef *) mallocMagic((unsigned) (sizeof (CellDef)));
    cellDef->cd_flags = 0;
    cellDef->cd_bbox.r_xbot = 0;
    cellDef->cd_bbox.r_ybot = 0;
    cellDef->cd_bbox.r_xtop = 1;
    cellDef->cd_bbox.r_ytop = 1;
    cellDef->cd_extended = cellDef->cd_bbox;
    cellDef->cd_name = (char *) NULL;
    cellDef->cd_file = (char *) NULL;
#ifdef FILE_LOCKS
    cellDef->cd_fd = -1;
#endif
    cellDef->cd_parents = (CellUse *) NULL;
    cellDef->cd_labels = (Label *) NULL;
    cellDef->cd_lastLabel = (Label *) NULL;
    cellDef->cd_client = (ClientData) 0;
    cellDef->cd_props = (ClientData) NULL;
    cellDef->cd_timestamp = 0;
    TTMaskZero(&cellDef->cd_types);
    HashInit(&cellDef->cd_idHash, 16, HT_STRINGKEYS);

    cellDef->cd_planes[PL_CELL] = DBNewPlane((ClientData) NULL);
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	cellDef->cd_planes[pNum] = DBNewPlane((ClientData) TT_SPACE);

    /* Definitively zero out all other plane entries */
    for (pNum = DBNumPlanes; pNum < MAXPLANES; pNum++)
	cellDef->cd_planes[pNum] = NULL;

    return (cellDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellNewUse --
 *
 * Create a new cell use of the supplied CellDef.
 *
 * Results:
 *	Returns a pointer to the new CellUse.  The CellUse is initialized
 *	to reflect that cellDef is its definition.  The transform is
 *	initialized to the identity, and the parent pointer initialized
 *	to NULL.
 *
 * Side effects:
 *	Updates the use list for cellDef.
 *
 * ----------------------------------------------------------------------------
 */

CellUse *
DBCellNewUse(cellDef, useName)
    CellDef *cellDef;	/* Pointer to definition of the cell */
    char *useName;	/* Pointer to use identifier for the cell.  This may
			 * be NULL, in which case a unique use identifier is
			 * generated automatically when the cell use is linked
			 * into a parent def.
			 */
{
    CellUse *cellUse;

    cellUse = (CellUse *) mallocMagic((unsigned) (sizeof (CellUse)));
    cellUse->cu_id = StrDup((char **) NULL, useName);
    cellUse->cu_flags = (unsigned char)0;
    cellUse->cu_expandMask = 0;
    cellUse->cu_transform = GeoIdentityTransform;
    cellUse->cu_def = cellDef;
    cellUse->cu_parent = (CellDef *) NULL;
    cellUse->cu_xlo = 0;
    cellUse->cu_ylo = 0;
    cellUse->cu_xhi = 0;
    cellUse->cu_yhi = 0;
    cellUse->cu_xsep = 0;
    cellUse->cu_ysep = 0;
    cellUse->cu_nextuse = cellDef->cd_parents;

    /* Initial client field */
    /* (commands can use this field for whatever
     * they like, but should restore its value to CLIENTDEFAULT before exiting.) 
     */
    cellUse->cu_client = (ClientData) CLIENTDEFAULT;

    cellDef->cd_parents = cellUse;
    DBComputeUseBbox(cellUse);
    return (cellUse);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellRenameDef --
 *
 * Renames the indicated CellDef.
 *
 * Results:
 *	TRUE if successful, FALSE if the new name was not unique.
 *
 * Side effects:
 *	The name of the CellDef is entered into the symbol table
 *	of known cells.  The CDMODIFIED bit is set in the flags
 *	of each of the parents of the CellDef to force them to
 *	be written out using the new name.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellRenameDef(cellDef, newName)
    CellDef *cellDef;		/* Pointer to CellDef being renamed */
    char *newName;		/* Pointer to new name */
{
    HashEntry *oldEntry, *newEntry;
    CellUse *parent;

    oldEntry = HashFind(&dbCellDefTable, cellDef->cd_name);
    ASSERT(HashGetValue(oldEntry) == (ClientData) cellDef, "DBCellRenameDef");

    newEntry = HashFind(&dbCellDefTable, newName);
    if (HashGetValue(newEntry) != (ClientData) NULL)
	return (FALSE);

    HashSetValue(oldEntry, (ClientData) NULL);
    HashSetValue(newEntry, (ClientData) cellDef);
    (void) StrDup(&cellDef->cd_name, newName);

    for (parent = cellDef->cd_parents; parent; parent = parent->cu_nextuse)
	if (parent->cu_parent)
	    parent->cu_parent->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellDeleteDef --
 *
 * Removes the CellDef from the symbol table of known CellDefs and
 * frees the storage allocated to the CellDef.  The CellDef must have
 * no CellUses.
 *
 * Results:
 *	TRUE if successful, FALSE if there were any outstanding
 *	CellUses found.
 *
 * Side effects:
 *	The CellDef is removed from the table of known CellDefs.
 *	All storage for the CellDef and its tile planes is freed.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellDeleteDef(cellDef)
    CellDef *cellDef;		/* Pointer to CellDef to be deleted */
{
    HashEntry *entry;

    if (cellDef->cd_parents != (CellUse *) NULL)
	return (FALSE);

    entry = HashFind(&dbCellDefTable, cellDef->cd_name);
    ASSERT(HashGetValue(entry) == (ClientData) cellDef, "DBCellDeleteDef");
    HashSetValue(entry, (ClientData) NULL);
    if (cellDef->cd_props)
	DBPropClearAll(cellDef);

    /* Need to check the DRC pending queue, and remove	*/
    /* this cell if it's there.				*/
    DRCRemovePending(cellDef);

    DBCellDefFree(cellDef);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellDefFree --
 *
 * 	Does all the dirty work of freeing up stuff inside a celldef.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All memory associated with the cellDef is freed.  This may
 *	cause lower-level cellUses and defs to be freed up.  This
 *	procedure is separated from DBDeleteCellDef so that it can
 *	be used for cells that aren't in the hash table (e.g. cells
 *	used by the window manager).
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellDefFree(cellDef)
    CellDef *cellDef;

{
    int pNum;
    Label *lab;

    if (cellDef->cd_file != (char *) NULL)
	freeMagic(cellDef->cd_file);
    if (cellDef->cd_name != (char *) NULL)
	freeMagic(cellDef->cd_name);

    /*
     * We want the following searching to be non-interruptible
     * to guarantee that all storage gets freed.
     */

    SigDisableInterrupts();
    DBFreeCellPlane(cellDef->cd_planes[PL_CELL]);
    TiFreePlane(cellDef->cd_planes[PL_CELL]);

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	DBFreePaintPlane(cellDef->cd_planes[pNum]);
	TiFreePlane(cellDef->cd_planes[pNum]);
	cellDef->cd_planes[pNum] = (Plane *) NULL;
    }

    for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
	freeMagic((char *) lab);
    SigEnableInterrupts();
    HashKill(&cellDef->cd_idHash);

    freeMagic((char *) cellDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellDeleteUse --
 *
 * Frees the storage allocated to the CellUse.
 *
 * It is required that the CellUse has been removed from any CellTileBodies
 * in the subcell plane of its parent.  The parent pointer for this
 * CellUse must therefore be NULL.
 *
 * Results:
 *	TRUE if the CellUse was successfully removed, FALSE if
 *	the parent pointer were not NULL.
 *
 * Side effects:
 *	All storage for the CellUse is freed.
 *	The list of all CellUses associated with a given CellDef is
 *	updated to reflect the absence of the deleted CellUse.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellDeleteUse(cellUse)
    CellUse *cellUse;		/* Pointer to CellUse to be deleted */
{
    CellDef *cellDef;
    CellUse *useptr;

    if (cellUse->cu_parent != (CellDef *) NULL)
	return (FALSE);

    cellDef = cellUse->cu_def;
    if (cellUse->cu_id != (char *) NULL)
	freeMagic(cellUse->cu_id);
    cellUse->cu_id = (char *) NULL;
    cellUse->cu_def = (CellDef *) NULL;

    ASSERT(cellDef->cd_parents != (CellUse *) NULL, "DBCellDeleteUse");

    if (cellDef->cd_parents == cellUse)
	cellDef->cd_parents = cellUse->cu_nextuse;
    else for (useptr = cellDef->cd_parents;  useptr != NULL;
	useptr = useptr->cu_nextuse)
    {
	if (useptr->cu_nextuse == cellUse)
	{
	    useptr->cu_nextuse = cellUse->cu_nextuse;
	    break;
	}
    }

    freeMagic((char *) cellUse);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellSrDefs --
 *
 * Search for all cell definitions matching a given pattern.
 * For each cell definition whose flag word contains any of the
 * bits in pattern, the supplied procedure is invoked.
 *
 * The procedure should be of the following form:
 *	int
 *	func(cellDef, cdata)
 *	    CellDef *cellDef;
 *	    ClientData cdata;
 *	{
 *	}
 * Func should normally return 0.  If it returns 1 then the
 * search is aborted.
 *
 * Results:
 *	Returns 1 if the search completed normally, 1 if it aborted.
 *
 * Side effects:
 *	Whatever the user-supplied procedure does.
 *
 * ----------------------------------------------------------------------------
 */

int
DBCellSrDefs(pattern, func, cdata)
    int pattern;	/* Used for selecting cell definitions.  If any
			 * of the bits in the pattern are in a def->cd_flags,
			 * or if pattern is 0, the user-supplied function
			 * is invoked.  
			 */
    int (*func)();	/* Function to be applied to each matching CellDef */
    ClientData cdata;	/* Client data also passed to function */
{
    HashSearch hs;
    HashEntry *he;
    CellDef *cellDef;

    HashStartSearch(&hs);
    while ((he = HashNext(&dbCellDefTable, &hs)) != (HashEntry *) NULL)
    {
	cellDef = (CellDef *) HashGetValue(he);
	if (cellDef == (CellDef *) NULL)
	    continue;
	if ((pattern != 0) && !(cellDef->cd_flags & pattern))
	    continue;
	if ((*func)(cellDef, cdata)) return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBLinkCell --
 *
 * Set the cu_id for the supplied CellUse appropriately for linking into
 * the parent CellDef.  If the cu_id is NULL, a cu_id unique within the
 * CellDef is automatically generated and stored in cu_id; otherwise, the
 * one supplied in cu_id is used.
 *
 *			*** WARNING ***
 *
 * This operation is not recorded on the undo list, as it always accompanies
 * the creation of a new cell use.
 *
 * Results:
 *	TRUE if the CellUse is unique within the parent CellDef, FALSE
 *	if there would be a name conflict.  If the cu_id of the CellUse
 *	is NULL, TRUE is always returned; FALSE is only returned if
 *	there is an existing name which would conflict with names already
 *	present in the CellUse.
 *
 * Side effects:
 *	Will set cu_id to an automatically generated instance id if
 *	it was originally NULL.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBLinkCell(use, parentDef)
    CellUse *use;
    CellDef *parentDef;
{
    char useId[100], *lastName;
    HashEntry *he;
    int n;

    if (use->cu_id)
    {
	if (DBFindUse(use->cu_id, parentDef))
	    return FALSE;
	DBSetUseIdHash(use, parentDef);
	return TRUE;
    }

    HashInit(&dbUniqueNameTable, 32, 0);	/* Indexed by use-id */

    /*
     * Uses can't contain slashes (otherwise they'd interfere with
     * terminal naming conventions).  If the cellName has a slash,
     * just use the part of it after the last slash.
     */
    lastName = strrchr(use->cu_def->cd_name, '/');
    if (lastName == NULL) lastName = use->cu_def->cd_name;
    else lastName++;
    
    /* This search must not be interrupted */
    SigDisableInterrupts();
    (void) DBCellEnum(parentDef, dbLinkFunc, (ClientData) lastName);
    SigEnableInterrupts();

    /* This loop terminates only when an empty useid is found	*/
    /* This loop should *not* terminate on interrupt, because	*/
    /* lots of code relies on a NULL cu_id being passed to ensure */
    /* that the cell gets linked in.				*/

    for (n = 0;; n++)
    {
	(void) sprintf(useId, "%s_%d", lastName, n);
	he = HashLookOnly(&dbUniqueNameTable, useId);
	if (he == (HashEntry *) NULL)
	{
	    HashKill(&dbUniqueNameTable);
	    use->cu_id = StrDup((char **) NULL, useId);
	    DBSetUseIdHash(use, parentDef);
	    return (TRUE);
	}
    }

    /* Never gets here 			*/
    /* HashKill(&dbUniqueNameTable);	*/
    /* return (FALSE);			*/
}

/*
 * dbLinkFunc --
 *
 * Filter function called via DBCellEnum by DBLinkCell above.
 * Creates an entry in the hash table dbUniqueNameTable to
 * indicate that the name "defname_#" is not available.
 */

int
dbLinkFunc(cellUse, defname)
    CellUse *cellUse;
    char *defname;
{
    char *usep = cellUse->cu_id;

    /* Skip in the unlikely event that this cell has no use-id */
    if (usep == (char *) NULL)
	return (0);

    /*
     * Only add names whose initial part matches 'defname',
     * and which are of the form 'defname_something'.
     */
    while (*defname)
	if (*defname++ != *usep++)
	    return 0;
    if (*usep++ != '_') return 0;
    if (*usep == '\0') return 0;

    /* Remember this name as being in use */
    (void) HashFind(&dbUniqueNameTable, cellUse->cu_id);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBReLinkCell --
 *
 * Change the instance id of the supplied CellUse.
 * If the instance id is non-NULL, and the new id is the same
 * as the old one, we do nothing.
 *
 * Results:
 *	Returns TRUE if successful, FALSE if the new name was not
 *	unique within the parent def.
 *
 * Side effects:
 *	May modify the cu_id of the supplied CellUse.
 *	Marks the parent of the cell use as having been modified.
 * ----------------------------------------------------------------------------
 */

bool
DBReLinkCell(cellUse, newName)
    CellUse *cellUse;
    char *newName;
{
    if (cellUse->cu_id && strcmp(cellUse->cu_id, newName) == 0)
	return (TRUE);

    if (DBFindUse(newName, cellUse->cu_parent))
	return (FALSE);

    if (cellUse->cu_parent)
	cellUse->cu_parent->cd_flags |= CDMODIFIED;

    /* Old id (may be NULL) */
    if (cellUse->cu_id)
	DBUnLinkCell(cellUse, cellUse->cu_parent);
    if (UndoIsEnabled()) DBUndoCellUse(cellUse, UNDO_CELL_CLRID);

    /* New id */
    (void) StrDup(&cellUse->cu_id, newName);
    DBSetUseIdHash(cellUse, cellUse->cu_parent);
    if (UndoIsEnabled()) DBUndoCellUse(cellUse, UNDO_CELL_SETID);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBFindUse --
 *
 * Find a CellUse with the given name in the supplied parent CellDef.
 *
 * Results:
 *	Returns a pointer to the found CellUse, or NULL if it was not
 *	found.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

CellUse *
DBFindUse(id, parentDef)
    char *id;
    CellDef *parentDef;
{
    HashEntry *he;
    char *delimit;
   
    /* Sanity checks */
    if (id == NULL) return NULL;
    if (parentDef == NULL) return NULL;

    /* Array delimiters should be ignored */
    if ((delimit = strrchr(id, '[')) != NULL) *delimit = '\0';

    he = HashLookOnly(&parentDef->cd_idHash, id);
    if (delimit != NULL) *delimit = '[';
    if (he == NULL)
	return (CellUse *) NULL;

    return (CellUse *) HashGetValue(he);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBGenerateUniqueIds --
 *
 * Make certain that the use-id of each CellUse under 'def' is unique
 * if it exists.  If duplicates are detected, all but one is freed and
 * set to NULL.
 *
 * The second pass consists of giving each CellUse beneath 'def' with
 * a NULL use-id a uniquely generated one.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify the use-id's of the cells in the cell plane of 'def'.
 *	Prints error messages if use-ids had to be reassigned.
 *
 * ----------------------------------------------------------------------------
 */

void
DBGenerateUniqueIds(def, warn)
    CellDef *def;
    bool warn;		/* If TRUE, warn user when we assign new ids */
{
    int dbFindNamesFunc();
    int dbGenerateUniqueIdsFunc();

    dbWarnUniqueIds = warn;
    HashInit(&dbUniqueDefTable, 32, 1);		/* Indexed by (CellDef *) */
    HashInit(&dbUniqueNameTable, 32, 0);	/* Indexed by use-id */

    /* Build up tables of names, complaining about duplicates */
    (void) DBCellEnum(def, dbFindNamesFunc, (ClientData) def);

    /* Assign unique use-ids to all cells */
    (void) DBCellEnum(def, dbGenerateUniqueIdsFunc, (ClientData) def);

    HashKill(&dbUniqueDefTable);
    HashKill(&dbUniqueNameTable);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbFindNamesFunc --
 *
 * Called via DBCellEnum() on behalf of DBGenerateUniqueIds() above,
 * for each subcell of the def just processed.  If any cell has a
 * use-id, we add it to our table of in-use names (dbUniqueNameTable).
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	If the name already is in the table, free this cell's use-id
 *	and set it to NULL.  In any event, add the name to the table.
 *
 * ----------------------------------------------------------------------------
 */

int
dbFindNamesFunc(use, parentDef)
    CellUse *use;
    CellDef *parentDef;
{
    HashEntry *he;

    if (use->cu_id)
    {
	he = HashFind(&dbUniqueNameTable, use->cu_id);
	if (HashGetValue(he))
	{
	    TxError("Duplicate instance-id for cell %s (%s) will be renamed\n",
			use->cu_def->cd_name, use->cu_id);
	    DBUnLinkCell(use, parentDef);
	    freeMagic(use->cu_id);
	    use->cu_id = (char *) NULL;
	}
	HashSetValue(he, use);
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbGenerateUniqueIdsFunc --
 *
 * Called via DBCellEnum() on behalf of DBGenerateUniqueIds() above,
 * for each subcell of the def just processed.  If the cell has no
 * use-id, we generate one automatically.  In any event, install the
 * use identifier for each cell processed in the HashTable
 * parentDef->cd_idHash.
 *
 * Algorithm:
 *	We generate unique use-id's of the form def_# where #
 *	is an integer such that no other use-id in this cell has
 *	the same name.  The HashTable dbUniqueDefTable is indexed
 *	by CellDef and contains the highest sequence number (#
 *	above) processed for that CellDef.  Each time we process
 *	a cell, we start with this sequence number and continue
 *	to increment it until we find a name that is not in
 *	calmaNamesTable, then generate the use-id, and store
 *	the next sequence number in the HashEntry for the CellDef.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
dbGenerateUniqueIdsFunc(use, parentDef)
    CellUse *use;
    CellDef *parentDef;
{
    HashEntry *hedef, *hename;
    int suffix;
    char name[1024];

    if (use->cu_id)
	goto setHash;

    hedef = HashFind(&dbUniqueDefTable, (char *) use->cu_def);
    for (suffix = (spointertype) HashGetValue(hedef); ; suffix++)
    {
	(void) sprintf(name, "%s_%d", use->cu_def->cd_name, suffix);
	hename = HashLookOnly(&dbUniqueNameTable, name);
	if (hename == NULL)
	    break;
    }

    if (dbWarnUniqueIds)
	TxPrintf("Setting instance-id of cell %s to %s\n",
		use->cu_def->cd_name, name);
    use->cu_id = StrDup((char **) NULL, name);
    HashSetValue(hedef, (spointertype)suffix + 1);

setHash:
    DBSetUseIdHash(use, parentDef);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBSetUseIdHash --
 *
 * Update the use-id hash table in parentDef to reflect the fact
 * that 'use' now has instance-id use->cu_id.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
DBSetUseIdHash(use, parentDef)
    CellUse *use;
    CellDef *parentDef;
{
    HashEntry *he;

    he = HashFind(&parentDef->cd_idHash, use->cu_id);
    HashSetValue(he, (ClientData) use);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBUnLinkCell --
 *
 * Update the use-id hash table in parentDef to reflect the fact
 * that 'use' no longer is known by instance-id use->cu_id.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
DBUnLinkCell(use, parentDef)
    CellUse *use;
    CellDef *parentDef;
{
    HashEntry *he;

    if (he = HashLookOnly(&parentDef->cd_idHash, use->cu_id))
	HashSetValue(he, (ClientData) NULL);
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBNewYank --
 *
 * Create a new yank buffer with name 'yname'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in *pydef with a newly created CellDef by that name, and
 *	*pyuse with a newly created CellUse pointing to the new def.
 *	The CellDef pointed to by *pydef has the CD_INTERNAL flag
 *	set, and is marked as being available.
 *
 * ----------------------------------------------------------------------------
 */

void
DBNewYank(yname, pyuse, pydef)
    char *yname;	/* Name of yank buffer */
    CellUse **pyuse;	/* Pointer to new cell use is stored in *pyuse */
    CellDef **pydef;	/* Similarly for def */
{
    *pydef = DBCellLookDef(yname);
    if (*pydef == (CellDef *) NULL)
    {
	*pydef = DBCellNewDef (yname,(char *) NULL);
	ASSERT(*pydef != (CellDef *) NULL, "DBNewYank");
	DBCellSetAvail(*pydef);
	(*pydef)->cd_flags |= CDINTERNAL;
    }
    *pyuse = DBCellNewUse(*pydef, (char *) NULL);
    DBSetTrans(*pyuse, &GeoIdentityTransform);
    (*pyuse)->cu_expandMask = CU_DESCEND_SPECIAL; /* This is always expanded. */
}

