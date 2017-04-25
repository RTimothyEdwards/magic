/* NMnetlist.c -
 *
 *	This file manages netlists for the Magic netlist menu
 *	package.  It reads and writes netlists, and provides
 *	routines to modify the nets.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMnetlist.c,v 1.2 2010/09/12 23:36:13 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "textio/textio.h"
#include "netmenu/nmInt.h"
#include "utils/undo.h"
#include "utils/malloc.h"
#include "netmenu/netmenu.h"

/* The data structure below is used to describe each of the
 * netlist files currently known to Magic.  At any given time,
 * only one is "current".  A netlist is not much more than a
 * big hash table of terminal names.  Some of the entries in
 * the hash table have null values (these correspond to terminals
 * that have been deleted).  Where a hash table entry has a
 * non-zero value, it points to a NetEntry, which links together
 * all of the terminals in a net.
 */

/* Netlist structure:  one of these per netlist known to Magic. */

typedef struct xxx_nl_1
{
    char *nl_name;		/* Name of the netlist file, before
				 * path expansion.
				 */
    char *nl_fileName;		/* Actual path-expanded file name
				 * (place to write back the netlist.
				 */
    HashTable nl_table;		/* Hash table holding nets. */
    int nl_flags;		/* Various flag bits;  see below. */
    struct xxx_nl_1 *nl_next;	/* All netlists are linked together
				 * in one big long list.
				 */
} Netlist;

/* Flag bits for Netlist:
 *
 * NL_MODIFIED:		1 means this netlist has been modified since
 *			the last time it was written to disk.
 */

#define NL_MODIFIED 1

/* NetEntry structure:  one of these for each terminal in each
 * net.  The hash table entry for the terminal points to this
 * structure.  Double links are used to tie together all entries
 * for one net into a circular structure.
 */

typedef struct xxx_ne_1
{
    char *ne_name;		/* Pointer to name of terminal (this
				 * points to the text string in the
				 * hash table entry.
				 */
    int ne_flags;		/* Various flags, see below. */
    struct xxx_ne_1 *ne_next;	/* Next entry for this net. */
    struct xxx_ne_1 *ne_prev;	/* Previous entry for this net. */
} NetEntry;

/* The flags currently used in NetEntry's are:
 *
 * NETENTRY_SEEN:  Used to keep this entry from being processed twice
 *		   when enumerating nets.
 */

#define NETENTRY_SEEN 1

Netlist *nmCurrentNetlist = NULL;	/* The netlist all procedures operate
					 * on for now.
					 */
Netlist *nmListHead = NULL;		/* The first netlist in the linked
					 * list of all netlists.
					 */

/* Used in asking the user for confirmation: */

static char *(yesno[]) = {"no", "yes", NULL};


/*
 * ----------------------------------------------------------------------------
 *
 * NMAddTerm --
 *
 * 	This adds a terminal to the same net as another given terminal.
 *
 * Results:
 *	A pointer is returned to the name of the terminal that has
 *	been added.  This is a pointer to the hash table entry, so
 *	it's not going to go away until the hash table is explictly
 *	deleted.  This is a convenience to provide callers with a
 *	handle they can use later to refer to this net.  If no
 *	terminal was added, either because other is NULL or because
 *	there isn't a current netlist, NULL is returned.
 *
 * Side effects:
 *	If new is already on a net, it is removed from that net.
 *	If other doesn't exist in the table, an entry is created
 *	for it.  Then new is added to the net containing other.
 *	If new and other are the same, a new net is created with
 *	just one terminal.
 *
 * ----------------------------------------------------------------------------
 */

char *
NMAddTerm(new, other)
    char *new;			/* Name of new terminal to be added. */
    char *other;		/* Name of terminal whose net new is to join.*/
{
    HashEntry *hNew, *hOther;
    NetEntry *newEntry, *otherEntry;

    /* Lookup new, and remove it from its current net, if there is one. */

    if ((nmCurrentNetlist == NULL) || (new == NULL) || (other == NULL))
	return NULL;
    
    nmCurrentNetlist->nl_flags |= NL_MODIFIED;
    hNew = HashFind(&nmCurrentNetlist->nl_table, new);
    newEntry = (NetEntry *) HashGetValue(hNew);
    if (newEntry != 0)
    {
	NMUndo(newEntry->ne_name, newEntry->ne_prev->ne_name, NMUE_REMOVE);
	newEntry->ne_prev->ne_next = newEntry->ne_next;
	newEntry->ne_next->ne_prev = newEntry->ne_prev;
    }
    else
    {
	/* Create a new entry for this terminal. */

	newEntry = (NetEntry *) mallocMagic(sizeof(NetEntry));
	newEntry->ne_name = hNew->h_key.h_name;
	newEntry->ne_flags = 0;
	HashSetValue(hNew, newEntry);
    }
    newEntry->ne_prev = newEntry;
    newEntry->ne_next = newEntry;

    /* Now lookup the (supposedly pre-existing) terminal.  If it
     * doesn't have an entry in the hash table, make a new one.
     */
    
    hOther = HashFind(&nmCurrentNetlist->nl_table, other);
    otherEntry = (NetEntry *) HashGetValue(hOther);
    if (otherEntry == 0)
    {
	otherEntry = (NetEntry *) mallocMagic(sizeof(NetEntry));
	otherEntry->ne_name = hOther->h_key.h_name;
	otherEntry->ne_flags = 0;
	otherEntry->ne_prev = otherEntry;
	otherEntry->ne_next = otherEntry;
	HashSetValue(hOther, otherEntry);
    }

    /* Tie the new terminal onto other's list. */

    if (otherEntry != newEntry)
    {
	newEntry->ne_prev = otherEntry->ne_prev;
	newEntry->ne_next = otherEntry;
	newEntry->ne_prev->ne_next = newEntry;
	otherEntry->ne_prev = newEntry;
    }
    NMUndo(new, other, NMUE_ADD);
    return otherEntry->ne_name;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMDeleteTerm --
 *
 * 	This procedure removes a terminal from its net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The named terminal is removed from its net, if it is currently
 *	in a net.
 *
 * ----------------------------------------------------------------------------
 */

void
NMDeleteTerm(name)
    char *name;			/* Name of a terminal. */
{
    HashEntry *h;
    NetEntry *entry;

    if ((name == 0) || (nmCurrentNetlist == NULL)) return;

    h = HashLookOnly(&nmCurrentNetlist->nl_table, name);
    if (h == NULL) return;
    entry = (NetEntry *) HashGetValue(h);
    if (entry == 0) return;
    nmCurrentNetlist->nl_flags |= NL_MODIFIED;
    HashSetValue(h, 0);
    NMUndo(entry->ne_name, entry->ne_next->ne_name, NMUE_REMOVE);
    entry->ne_next->ne_prev = entry->ne_prev;
    entry->ne_prev->ne_next = entry->ne_next;
    freeMagic((char *) entry);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMJoinNets --
 *
 * 	This procedure joins two nets together.  It is similar to
 *	NMAddTerm, except that it applies to every terminal in
 *	the first net rather than just a single terminal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All of the terminals in the nets associated with termA
 *	and termB are joined together into a single net.
 *
 * ----------------------------------------------------------------------------
 */

void
NMJoinNets(termA, termB)
    char *termA;		/* Name of a terminal in first net. */
    char *termB;		/* Name of a terminal in second net. */
{
    HashEntry *ha, *hb;
    NetEntry *netA, *netB, *tmp;

    if ((termA == NULL) || (termB == NULL)) return;
    if (nmCurrentNetlist == NULL) return;

    /* Lookup the two nets, and make sure that they both exist
     * and aren't already the same.
     */
    
    ha = HashFind(&nmCurrentNetlist->nl_table, termA);
    netA = (NetEntry *) HashGetValue(ha);
    hb = HashFind(&nmCurrentNetlist->nl_table, termB);
    netB = (NetEntry *) HashGetValue(hb);
    if ((netA == 0) || (netB == 0)) return;
    nmCurrentNetlist->nl_flags |= NL_MODIFIED;
    tmp = netA;
    while (TRUE)
    {
	if (tmp == netB) return;
	tmp = tmp->ne_next;
	if (tmp == netA) break;
    }

    /* Record the changes for undo purposes.  This code is a bit
     * tricky:  since termB is used as the reference network for
     * deleting all other terminals from it, termB itself must
     * be the last terminal to be deleted.  Otherwise undo won't
     * work.
     */

    tmp = netB->ne_next;
    while (TRUE)
    {
	NMUndo(tmp->ne_name, termB, NMUE_REMOVE);
	NMUndo(tmp->ne_name, termA, NMUE_ADD);
	if (tmp == netB) break;
	tmp = tmp->ne_next;
    }

    /* Join the two nets. */

    tmp = netA->ne_prev;
    netB->ne_prev->ne_next = netA;
    netA->ne_prev = netB->ne_prev;
    tmp->ne_next = netB;
    netB->ne_prev = tmp;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMDeleteNet --
 *
 * 	This procedure deletes a net by removing all of the terminals
 *	from it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the terminals in the given net are deleted from the net.
 *
 * ----------------------------------------------------------------------------
 */

void
NMDeleteNet(net)
    char *net;			/* Name of one of the terminals in the net
				 * to be deleted.
				 */
{
    HashEntry *h;
    NetEntry *ne, *next;

    if ((net == NULL) || (nmCurrentNetlist == NULL)) return;
    h = HashLookOnly(&nmCurrentNetlist->nl_table, net);
    if (h == NULL) return;
    ne = (NetEntry *) HashGetValue(h);
    if (ne == 0) return;
    nmCurrentNetlist->nl_flags |= NL_MODIFIED;

    /* The order of processing is a bit tricky.  Since we use net for
     * the "current net" in undo-ing, it must be the last terminal
     * to be deleted.
     */

    next = ne->ne_next;
    while (TRUE)
    {
	NMUndo(next->ne_name, net, NMUE_REMOVE);
	HashSetValue(HashFind(&nmCurrentNetlist->nl_table, next->ne_name), 0);
	freeMagic((char *) next);
	if (next == ne) break;
	next = next->ne_next;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMNewNetlist --
 *
 * 	This procedure sets everything up to use a new netlist from
 *	now on.  If the netlist isn't already loaded into memory,
 *	it is read from disk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new netlist may be read from disk.
 *
 * ----------------------------------------------------------------------------
 */

void
NMNewNetlist(name)
    char *name;			/* Name of the netlist file.  If NULL,
				 * then the netlist file association
				 * is eliminated.
				 */
{
    Netlist *new;
    FILE *file;
#define MAXLINESIZE 256
    char line[MAXLINESIZE], *fullName, *currentTerm, *p;

    /* Save undo information, and re-adjust things for the rest
     * of this module.
     */

    NMUndo(name, NMNetListButton.nmb_text, NMUE_NETLIST);
    (void) StrDup(&NMNetListButton.nmb_text, name);
    if (NMWindow != NULL)
        (void) NMredisplay(NMWindow, &NMNetListButton.nmb_area, (Rect *) NULL);
    NMSelectNet((char *) NULL);

    if ((name == NULL) || (name[0] == 0))
    {
	nmCurrentNetlist = NULL;
	return;
    }

    /* First of all, see if this netlist is already loaded. */

    for (new = nmListHead; new != NULL; new = new->nl_next)
    {
	if (strcmp(name, new->nl_name) == 0)
	{
	    nmCurrentNetlist = new;
	    return;
	}
    }

    /* Create and initialize a new netlist. */

    new = (Netlist *) mallocMagic(sizeof(Netlist));
    new->nl_name = NULL;
    new->nl_fileName = NULL;
    HashInit(&new->nl_table, 32, 0);
    new->nl_flags = 0;
    new->nl_next = nmListHead;
    nmListHead = new;
    nmCurrentNetlist = new;
    (void) StrDup(&new->nl_name, name);

    /* Open a file for reading the netlist.  If it doesn't exist,
     * or doesn't have a proper header line, issue a warning message,
     * then just start a new list.
     */
    
    file = PaOpen(name, "r", ".net", Path, CellLibPath, &fullName);
    if (file == NULL)
    {
	TxError("Netlist file %s.net couldn't be found.\n", name);
	TxError("Creating new netlist.\n");
	new->nl_fileName = mallocMagic((unsigned) (5 + strlen(name)));
	(void) sprintf(new->nl_fileName, "%s.net", name);
	return;
    }
    (void) StrDup(&new->nl_fileName, fullName);
    if ((fgets(line, MAXLINESIZE, file) == NULL)
	|| ((strcasecmp(line, " Net List File\n") != 0) /* Backward compatibility*/
	&& (strcasecmp(line, " Netlist File\n") != 0)))
    {
	TxError("%s isn't a legal netlist file.\n", new->nl_fileName);
	TxError("Creating new netlist.\n");
	(void) fclose(file);
	return;
    }

    /* Read nets from the file one at a time.  Each net consists of
     * a bunch of terminal names, one per line.  Nets are separated
     * by lines that are either empty or have a space as the first
     * character.  Lines starting with "#" are treated as comments.
     * None of this gets recorded for undo-ing.
     */
    
    UndoDisable();
    currentTerm = NULL;
    while (fgets(line, MAXLINESIZE, file) != NULL)
    {
	/* Strip the newline character from the end of the line. */

	for (p = line; *p != 0; p++)
	{
	    if (*p == '\n')
	    {
		*p = 0;
		break;
	    }
	}
	if ((line[0] == 0) || (line[0] == ' '))
	{
	    currentTerm = NULL;
	    continue;
	}
	if (line[0] == '#') continue;
	if (NMTermInList(line) != NULL)
	{
	    TxError("Warning: terminal \"%s\" appears in more than one net.\n",
		    line);
	    TxError("    Only the last appearance will be used.\n");
	}
	if (currentTerm == NULL)
	    currentTerm = NMAddTerm(line, line);
	else (void) NMAddTerm(line, currentTerm);
    }
    UndoEnable();

    nmCurrentNetlist->nl_flags &= ~NL_MODIFIED;
    (void) fclose(file);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMNetlistName --
 *
 * 	Return the name of the current net list.  Do it as a function to make
 *	it clear that the exported value is read-only.
 *
 * Results:
 *	The name of the current netlist.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
char *
NMNetlistName()
{
    if(nmCurrentNetlist!=NULL)
	return(nmCurrentNetlist->nl_name);
    else
	return ((char *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMEnumNets --
 *
 * 	This procedure calls a client function for each terminal in
 *	each net.  The supplied function should be of the form:
 *
 *	int
 *	func(name, firstInNet, clientData)
 *	    char *name;
 *	    bool firstInNet;
 *	    ClientData;
 *	{
 *	}
 *
 *	In the above, name is the name of a terminal.  FirstInNet
 *	is TRUE if this is the first terminal in the net, FALSE
 *	for all other terminals in the net.  All the terminals in
 *	a given net are enumerated consecutively.  ClientData is
 *	an arbitrary parameter value passed in by our caller.  Func
 *	should normally return 0.  If it returns a non-zero value,
 *	the enumeration will be aborted immediately.
 *
 * Results:
 *	If the search terminates normally, 0 is returned.  If the
 *	client function returns a non-zero value, then 1 is returned.
 *
 * Side effects:
 *	Whatever the client function does.
 *
 * ----------------------------------------------------------------------------
 */

int NMEnumNets(func, clientData)
    int (*func)();		/* Function to call for each terminal. */
    ClientData clientData;	/* Parameter to pass to function. */
{
    HashSearch hs;
    HashEntry *h;
    NetEntry *entry, *entry2;
    int result;

    if (nmCurrentNetlist == NULL) return 0;

    /* The search runs in two passes.  During the first pass, set flags
     * to avoid enumerating the same net or terminal twice.  During
     * the second pass, clear the flags.
     */

    HashStartSearch(&hs);
    result = 0;
    while (TRUE)
    {
	h = HashNext(&nmCurrentNetlist->nl_table, &hs);
	if (h == NULL) break;
	entry = (NetEntry *) HashGetValue(h);
	if (entry == 0) continue;
	if (entry->ne_flags & NETENTRY_SEEN) continue;
	entry->ne_flags |= NETENTRY_SEEN;

	/* Enumerate this entire net. */

	if ((*func)(entry->ne_name, TRUE, clientData) != 0)
	{
	    result = 1;
	    goto cleanup;
	}
	for (entry2 = entry->ne_next; entry2 != entry;
	    entry2 = entry2->ne_next)
	{
	    entry2->ne_flags |= NETENTRY_SEEN;
	    if ((*func)(entry2->ne_name, FALSE, clientData) != 0)
	    {
		result = 1;
		goto cleanup;
	    }
	}
    }

    cleanup: HashStartSearch(&hs);
    while (TRUE)
    {
	h = HashNext(&nmCurrentNetlist->nl_table, &hs);
	if (h == NULL) break;
	entry = (NetEntry *) HashGetValue(h);
	if (entry != 0) entry->ne_flags &= ~NETENTRY_SEEN;
    }
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMEnumTerms --
 *
 * 	This procedure calls a client function for each terminal in
 *	a given net.  The supplied function should be of the form:
 *
 *	int
 *	func(name, clientData)
 *	    char *name;
 *	    ClientData;
 *	{
 *	}
 *
 *	In the above, name is the name of a terminal.  The terminals
 *	in a given net are enumerated consecutively.  ClientData is
 *	an arbitrary parameter value passed in by our caller.  The
 *	client function should return 0 under normal conditions.  If
 *	it wishes to abort the search, it should return 1.
 *
 * Results:
 *	If the search terminates normally, 0 is returned.  If the
 *	client function returns a non-zero value, then 1 is returned.
 *
 * Side effects:
 *	Whatever the client function does.
 *
 * ----------------------------------------------------------------------------
 */

int
NMEnumTerms(name, func, clientData)
    char *name;			/* Name of terminal in net to be enumerated. */
    int (*func)();		/* Function to call for each terminal. */
    ClientData clientData;	/* Parameter to pass to function. */
{
    HashEntry *h;
    NetEntry *entry, *entry2;

    if (nmCurrentNetlist == NULL) return 0;
    h = HashLookOnly(&nmCurrentNetlist->nl_table, name);
    if (h == NULL) return 0;
    entry = (NetEntry *) HashGetValue(h);
    if (entry == NULL) return 0;
    entry2 = entry;
    while (TRUE)
    {
	if ((*func)(entry2->ne_name, clientData) != 0) return 1;
	entry2 = entry2->ne_next;
	if (entry2 == entry) break;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMHasList --
 *
 * 	This procedure checks to see if a netlist is selected.  It is used
 *	to let the outside world know.
 *
 * Results:
 *	TRUE if the netlist is selected.  Otherwise FALSE.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
NMHasList()
{
    return(nmCurrentNetlist != NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMTermInList --
 *
 * 	Tells whether or not the given terminal name is in the current
 *	netlist.
 *
 * Results:
 *	If the terminal isn't part of any net, NULL is returned.
 *	If it is part of some net, the terminal's name from the
 *	hash table is returned.  This is a token that won't go
 *	away, which the caller can use to remember the net name.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
NMTermInList(name)
    char *name;			/* Name of terminal. */
{
    HashEntry *h;
    NetEntry *entry;

    if (nmCurrentNetlist == NULL) return NULL;
    h = HashLookOnly(&nmCurrentNetlist->nl_table, name);
    if (h == NULL) return NULL;
    entry = (NetEntry *) HashGetValue(h);
    if (entry == NULL) return NULL;
    return entry->ne_name;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMWriteNetlist --
 *
 * 	This procedure writes out a netlist to a file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The file on disk is overwritten.
 *
 * ----------------------------------------------------------------------------
 */

void
NMWriteNetlist(fileName)
    char *fileName;		/* If non-NULL, gives name of file in
				 * which to write current netlist.  If NULL,
				 * the netlist gets written to the place
				 * from which it was read.
				 */
{
    FILE *file;
    int nmWriteNetsFunc();
    char *realName, line[50];

    if (nmCurrentNetlist == NULL)
    {
	TxError("There isn't a current net list to write.\n");
	return;
    }

    /* Decide what file to use to write the file (if an explicit name
     * is given, we have to add on a ".net" extension, and we also
     * check to make sure the file doesn't exist).
     */

    if (fileName == NULL) realName = nmCurrentNetlist->nl_fileName;
    else
    {
	realName = mallocMagic((unsigned) (5 + strlen(fileName)));
	(void) sprintf(realName, "%s.net", fileName);
	file = PaOpen(realName, "r", (char *) NULL, ".",
	    (char *) NULL, (char **) NULL);
	if (file != NULL)
	{
	    (void) fclose(file);
	    TxPrintf("Net list file %s already exists.", realName);
	    TxPrintf("  Should I overwrite it? [no] ");
	    if (TxGetLine(line, 50) == (char *) NULL) return;
	    if ((strcmp(line, "y") != 0) && (strcmp(line, "yes") != 0)) return;
	}
    }

    file = PaOpen(realName, "w", (char *) NULL, ".",
	(char *) NULL, (char **) NULL);
    if (file == NULL)
    {
	TxError("Couldn't write file %s.\n", realName);
	return;
    }
    fprintf(file, " Netlist File\n");
    (void) NMEnumNets(nmWriteNetsFunc, (ClientData) file);
    if (strcmp(realName, nmCurrentNetlist->nl_fileName) == 0)
	nmCurrentNetlist->nl_flags &= ~NL_MODIFIED;
    (void) fclose(file);
}

int
nmWriteNetsFunc(name, firstInNet, file)
    char *name;			/* Name of terminal. */
    bool firstInNet;		/* TRUE means first terminal in net. */
    FILE *file;			/* File in which to write info. */
{
    if (firstInNet) fputs("\n", file);
    fprintf(file, "%s\n", name);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCheckWritten --
 *
 * 	Checks to see if there are netlists that have been modified
 *	but not written back to disk.  If so, asks user whether he
 *	cares about them.
 *
 * Results:
 *	Returns TRUE if there are no modified netlists around, or
 *	if the user says he doesn't care about them.  Returns FALSE
 *	if the user says he cares.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
NMCheckWritten()
{
    char *name;
    Netlist *nl;
    int count, indx;
    char answer[12];

    count = 0;
    for (nl = nmListHead; nl != NULL; nl = nl->nl_next)
    {
	if (nl->nl_flags & NL_MODIFIED)
	{
	    count += 1;
	    name = nl->nl_name;
	}
    }
    if (count == 0) return TRUE;

    do
    {
	if (count == 1)
	    TxPrintf("Net-list \"%s\" has been modified.", name);
	else
	    TxPrintf("%d netlists have been modified.", count);
	TxPrintf("  Do you want to lose the changes? [no] ");
	if ((TxGetLine(answer, sizeof answer) == NULL) || (answer[0] == 0))
	    return FALSE;
        indx = Lookup(answer, yesno);
    } while (indx < 0);
    return indx;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMWriteAll --
 *
 * 	Goes through all netlists that have been modified, asking
 *	the user whether to write out the netlist or not.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Net-lists may be written to disk.
 *
 * ----------------------------------------------------------------------------
 */

void
NMWriteAll()
{
    Netlist *nl, *saveCurrent;
    static char *(options[]) = {"write", "skip", "abort", NULL};
    char answer[10];
    int indx;

    saveCurrent = nmCurrentNetlist;

    for (nl = nmListHead; nl != NULL; nl = nl->nl_next)
    {
	if ((nl->nl_flags & NL_MODIFIED) == 0) continue;
	do
	{
	    TxPrintf("%s: write, skip, or abort command? [write] ",
		nl->nl_name);
	    if (TxGetLine(answer, sizeof answer) == NULL) continue;
	    if (answer[0] == 0) indx = 0;
	    else indx = Lookup(answer, options);
	} while (indx < 0);
        switch (indx)
	{
	    case 0:
		nmCurrentNetlist = nl;
		NMWriteNetlist((char *) NULL);
		break;
	    case 1:
		break;
	    case 2:
		return;
	}
    }

    nmCurrentNetlist = saveCurrent;
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMFlushNetlist --
 *
 * 	This procedure flushes the contents of the named netlist
 *	from memory.  If the netlist was modified, the user is given
 *	a chance to abort the flush.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of the netlist are changed.  If the netlist
 *	had been modified, all previous undo events are flushed.
 *
 * ----------------------------------------------------------------------------
 */

void
NMFlushNetlist(name)
    char *name;			/* Name of the netlist to be flushed. */
{
    Netlist *list, **prev;
    HashSearch hs;
    HashEntry *h;

    /* Find the netlist in question. */
    
    list = NULL;
    for (prev = &nmListHead; *prev != NULL; prev = &(*prev)->nl_next)
    {
	if (strcmp(name, (*prev)->nl_name) == 0)
	{
	    list = *prev;
	    break;
	}
    }
    if (list == NULL)
    {
	TxError("Netlist \"%s\" isn't currently loaded.\n", name);
	return;
    }

    /* If the netlist has been modified, give the user a chance to
     * skip this.
     */
    
    if (list->nl_flags & NL_MODIFIED)
    {
	char answer[10];
	int indx;

	while (TRUE)
	{
	    TxPrintf("Really throw away all changes made ");
	    TxPrintf("to netlist \"%s\"? [no] ", name);
	    if ((TxGetLine(answer, sizeof answer) == NULL) || (answer[0] == 0))
		return;
	    indx = Lookup(answer, yesno);
	    if (indx == 0) return;
	    if (indx == 1)
	    {
		UndoFlush();
		break;
	    }
	}
    }

    /* Unlink the netlist from the list of netlists, and free up
     * everything in it.
     */
    
    *prev = list->nl_next;
    HashStartSearch(&hs);
    while (TRUE)
    {
	h = HashNext(&list->nl_table, &hs);
	if (h == NULL) break;
	if (HashGetValue(h) != NULL)
	    freeMagic((char *) HashGetValue(h));
    }
    freeMagic((char *) list);

    /* If the netlist was the current netlist, read it in again from
     * disk.
     */
    
    if (list == nmCurrentNetlist)
        NMNewNetlist(name);
}
