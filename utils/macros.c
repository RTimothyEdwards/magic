/*
 * macros.c --
 *
 * Defines and retrieves macros
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/macros.c,v 1.2 2010/06/24 12:37:58 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#ifdef XLIB
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif

#include "utils/magic.h"
#include "utils/main.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/macros.h"
#include "windows/windows.h"

/* Define the macro client table */

HashTable MacroClients;

/*
 *---------------------------------------------------------
 * MacroInit ---
 *
 *	Initialize the macro hash table
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Memory allocated for the hash table MacroClients.
 *
 *---------------------------------------------------------
 */

void
MacroInit()
{
    HashInit(&MacroClients, 4, HT_WORDKEYS);
}

/*
 *---------------------------------------------------------
 * MacroDefine ---
 *
 *	This procedure defines a macro.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The string passed is copied and considered to be the
 *	macro definition for the character.
 *---------------------------------------------------------
 */

void
MacroDefine(client, xc, str, help, imacro)
    WindClient client;	/* window client type */
    int xc;		/* full (X11) keycode of macro with modifiers */
    char *str;		/* ...and the string to be attached to it */
    char *help;		/* ...and/or the help text for the macro */
    bool imacro;	/* is this an interactive macro? */
{
    HashEntry *h;
    HashTable *clienttable, newTable;
    macrodef *oldMacro, *newMacro;

    /* If a macro exists, delete the old string and redefine it */
    h = HashFind(&MacroClients, (char *)client);
    clienttable = (HashTable *)HashGetValue(h);
    if (clienttable == NULL)
    {
	clienttable = (HashTable *)mallocMagic(sizeof(HashTable));
	HashInit(clienttable, 32, HT_WORDKEYS);
	HashSetValue(h, clienttable);
    }
    h = HashFind(clienttable, (char *)((ClientData)xc));
    oldMacro = (macrodef *)HashGetValue(h);
    if (oldMacro != NULL)
    {
	if (oldMacro->macrotext != NULL)
	    freeMagic(oldMacro->macrotext);
	if (oldMacro->helptext != NULL) {
	    freeMagic(oldMacro->helptext);
	    oldMacro->helptext = NULL;
	}
	newMacro = oldMacro;
    }
    else
	newMacro = (macrodef *)mallocMagic(sizeof(macrodef));

    HashSetValue(h, newMacro);
    newMacro->interactive = imacro;
    newMacro->macrotext = StrDup((char **)NULL, str);
    if (help != NULL)
	newMacro->helptext = StrDup((char **)NULL, help);
    else
	newMacro->helptext = NULL;
}

/*
 *---------------------------------------------------------
 * MacroDefineHelp ---
 *
 *	This procedure defines the help text for a macro.
 *	A macro must already exist for the specified key.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The string passed is copied and considered to be the
 *	macro definition for the character.
 *---------------------------------------------------------
 */

void
MacroDefineHelp(client, xc, help)
    WindClient client;	/* window client type */
    int xc;		/* full (X11) keycode of macro with modifiers */
    char *help;		/* ...and/or the help text for the macro */
{
    HashEntry *h;
    HashTable *clienttable;
    macrodef *curMacro;

    /* If a macro exists, delete the old string and redefine it */
    h = HashFind(&MacroClients, (char *)client);
    clienttable = (HashTable *)HashGetValue(h);
    if (clienttable == NULL) return;

    h = HashFind(clienttable, (char *)((ClientData)xc));
    curMacro = (macrodef *)HashGetValue(h);
    if (curMacro == NULL) return;

    if (curMacro->helptext != NULL)
	freeMagic(curMacro->helptext);

    if (help == NULL)
	curMacro->helptext = NULL;
    else
	curMacro->helptext = StrDup((char **)NULL, help);
}

/*---------------------------------------------------------
 * MacroRetrieve:
 *	This procedure retrieves a macro.
 *
 * Results:
 *	A pointer to a new Malloc'ed string is returned.
 *	This structure should be freed when the caller is
 *	done with it.
 *
 * Side Effects:
 *	None.
 *---------------------------------------------------------
 */

char *
MacroRetrieve(client, xc, iReturn)
    WindClient client;		/* window client type */
    int xc;			/* the extended name of the macro */
    bool *iReturn;		/* TRUE if macro is interactive */
{
    HashEntry *h;
    HashTable *clienttable;
    macrodef *cMacro;

    /* If a macro exists, delete the old string and redefine it */
    h = HashLookOnly(&MacroClients, (char *)client);
    if (h != NULL)
    {
	clienttable = (HashTable *)HashGetValue(h);
	if (clienttable != NULL)
	{
	    h = HashLookOnly(clienttable, (char *)((ClientData)xc));
	    if (h != NULL)
	    {
		cMacro = (macrodef *)HashGetValue(h);
		if (cMacro != NULL)
		{
		    if (iReturn != NULL)
			*iReturn = cMacro->interactive;
		    return StrDup((char **)NULL, cMacro->macrotext);
		}
	    }
	}
    }
    if (iReturn != NULL) *iReturn = FALSE;
    return (char *)NULL;
}

/*---------------------------------------------------------
 * MacroRetrieveHelp:
 *	This procedure retrieves the help text for a macro.
 *
 * Results:
 *	A pointer to a new Malloc'ed string is returned.
 *	This structure should be freed when the caller is
 *	done with it.
 *
 * Side Effects:
 *	None.
 *---------------------------------------------------------
 */

char *
MacroRetrieveHelp(client, xc)
    WindClient client;		/* window client type */
    int xc;			/* the extended name of the macro */
{
    HashEntry *h;
    HashTable *clienttable;
    macrodef *cMacro;

    /* If a macro exists, delete the old string and redefine it */
    h = HashLookOnly(&MacroClients, (char *)client);
    if (h != NULL)
    {
	clienttable = (HashTable *)HashGetValue(h);
	if (clienttable != NULL)
	{
	    h = HashLookOnly(clienttable, (char *)((ClientData)xc));
	    if (h != NULL)
	    {
		cMacro = (macrodef *)HashGetValue(h);
		if (cMacro != NULL)
		    if (cMacro->helptext != NULL)
			return StrDup((char **)NULL, cMacro->helptext);
	    }
	}
    }
    return (char *)NULL;
}

/*---------------------------------------------------------
 * MacroSubsitute:
 *	Make a substitution of one string for another
 *	in a macro string.  This is really just a string
 *	manipulation;  it doesn't have anything specifically
 *	to do with macros.
 *
 * Results:
 *	A new string pointer to the macro contents.
 *
 * Side Effects:
 *	None.
 *---------------------------------------------------------
 */

char *
MacroSubstitute(macrostr, searchstr, replacestr)
    char *macrostr;
    char *searchstr;
    char *replacestr;
{
    char *found, *last, *new;
    int expand, length, oldlength, srchsize;

    if (macrostr == (char *)NULL) return NULL;

    oldlength = strlen(macrostr);
    srchsize = strlen(searchstr);
    expand = strlen(replacestr) - srchsize;
    last = macrostr;
    length = oldlength;
    while ((found = strstr(last, searchstr)) != NULL)
    {
	length += expand;
	last = found + srchsize;
    }
    if (length > oldlength)
    {
	new = (char *)mallocMagic(length + 1);
	*new = '\0';
	last = macrostr;
	while ((found = strstr(last, searchstr)) != NULL)
	{
	   *found = '\0';
	   strcat(new, last);
	   strcat(new, replacestr);
	   last = found + srchsize;
	}
	strcat(new, last);
	freeMagic(macrostr);
	return new;
    }
    else
	return macrostr;
}


/*---------------------------------------------------------
 * MacroDelete:
 *	This procedure deletes a macro.
 *
 * Results:	None.
 *
 * Side Effects:
 *	The string that defines a macro is deleted.  This means
 *	that if anybody still has a pointer to that string
 *	they are in trouble.
 *---------------------------------------------------------
 */

void
MacroDelete(client, xc)
    WindClient client;	/* window client type */
    int xc;		/* the extended name of the macro */
{
    HashEntry *h;
    HashTable *clienttable;
    macrodef *cMacro;

    h = HashLookOnly(&MacroClients, (char *)client);
    if (h != NULL)
    {
	clienttable = (HashTable *)HashGetValue(h);
	if (clienttable != NULL)
	{
	    h = HashLookOnly(clienttable, (char *)((ClientData)xc));
	    if (h != NULL)
	    {
		cMacro = (macrodef *)HashGetValue(h);
		if (cMacro != NULL)
		{
		    if (cMacro->macrotext != NULL)
			freeMagic(cMacro->macrotext);
		    if (cMacro->helptext != NULL)
			freeMagic(cMacro->helptext);
		    HashSetValue(h, NULL);
		    freeMagic(cMacro);
		}
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 * MacroName --
 * 	Convert an extended keycode to a string.
 *
 * Results:
 *	The string.
 *
 * Side effects:
 *	The string is malloc'ed from memory, and must be free'd later.
 * ----------------------------------------------------------------------------
 */

char *
MacroName(xc)
    int xc;
{
    char *vis;
    static char hex[17] = "0123456789ABCDEF";

#ifdef XLIB
    char *str;
    extern Display *grXdpy;
    KeySym ks = xc & 0xffff;
    int kmod = xc >> 16;

    str = NULL;
    if (grXdpy != NULL) {
        ks = xc & 0xffff;
	if (ks != NoSymbol) str = XKeysymToString(ks);
    }
    if (str != NULL)
    {
	vis = (char *) mallocMagic( sizeof(char) * (strlen(str) + 32) );
	vis[0] = '\0';
	if (kmod & Mod1Mask) strcat(vis, "Meta_");
	if (kmod & ControlMask) strcat(vis, "Control_");
	if (kmod & LockMask) strcat(vis, "Capslock_");
	if (kmod & ShiftMask) strcat(vis, "Shift_");
	strcat(vis, "XK_");
	strcat(vis, str);
	return (vis);
    }
#endif

    vis = (char *) mallocMagic( sizeof(char) * 6 );
    if (xc < (int)' ')
    {
	vis[0] = '^';
	vis[1] = (char)xc + '@';
	vis[2] = '\0';
    }
    else if (xc == 0x7F)
    {
	vis[0] = '<';
	vis[1] = 'd';
	vis[2] = 'e';
	vis[3] = 'l';
	vis[4] = '>';
	vis[5] = '\0';
    }
    else if (xc < 0x80)
    {
	vis[0] = (char)xc;
	vis[1] = '\0';
    }
    else
    {
	vis = (char *) mallocMagic( sizeof(char) * 8 );
	vis[0] = '0';
	vis[1] = 'x';
#ifdef XLIB
	vis[2] = hex[ (kmod & 0xf)];
#else
	vis[2] = '0';
#endif
	vis[3] = hex[ (xc & 0x0f000) >> 12];
	vis[4] = hex[ (xc & 0x00f00) >>  8];
	vis[5] = hex[ (xc & 0x000f0) >>  4];
	vis[6] = hex[ (xc & 0x0000f)      ];
	vis[7] = '\0';
    }
    return(vis);
}


/*
 * ----------------------------------------------------------------------------
 * MacroKey:
 * 	Convert a string to an extended keycode.
 *
 * Results:
 *	An extended macro name.
 *
 * Side effects:
 *	If the display being used is not X11, a warning is printed the
 *	first time this function is called.
 * ----------------------------------------------------------------------------
 */

int
MacroKey(str, verbose)
    char *str;
    int *verbose;
{
    static int warn = 1;

#ifdef XLIB
    int kc;
    char *vis;
    extern Display *grXdpy;
    KeySym ks;
    int kmod = 0;

    *verbose = 1;
    if (grXdpy != NULL)
    {
	vis = str;
	while( (*vis) != '\0' )
	{
	    if (!strncmp(vis, "Meta_", 5))
	    {
		kmod |= Mod1Mask;
		vis += 5;
	    }
	    else if (!strncmp(vis, "Alt_", 4))
	    {
		kmod |= Mod1Mask;
		vis += 4;
	    }
	    else if (!strncmp(vis, "Control_", 8))
	    {
		kmod |= ControlMask;
		vis += 8;
	    }
	    else if (((*vis) == '^') && (*(vis + 1) != '\0'))
	    {
		kmod |= ControlMask;
		vis++;
	    }
	    else if (!strncmp(vis, "Capslock_", 9))
	    {
		kmod |= LockMask;
		vis += 9;
	    }
	    else if (!strncmp(vis, "Shift_", 6))
	    {
		kmod |= ShiftMask;
		vis += 6;
	    }
	    else if (*vis == '\'')
	    {
		// If single quotes are used to protect the integrity
		// of the macro character, strip them now.

		char *aptr = strrchr(vis, '\'');
		if (aptr != NULL && aptr != vis)
		{
		   vis++;
		   *aptr = '\0';
		}
		else break;	// Don't hang on an unmatched quote
	    }
	    else break;
	}
	if (!strncmp(vis, "XK_", 3)) vis += 3;

	/* We're converting all ASCII back into X Keycodes 	*/
	/* The original macro (.magicrc) file format allows	*/
	/* embedded control characters, so we have to handle	*/
	/* those.  For regular ASCII characters, the keysym =	*/
	/* the ASCII value.					*/

	if ((*(vis + 1)) == '\0')
	{
	    /* single ASCII character handling */

	    char tc = *vis;
	   
	    /* Revert Control and Shift characters to ASCII	*/
	    /* unless other modifiers are present. Always make	*/
	    /* characters with Control and Shift uppercase.	*/

	    if (kmod & (ControlMask | ShiftMask))
	    {
		tc = toupper(tc);
		if (kmod & ShiftMask)
		    kc = (int)tc;
		else if (kmod & ControlMask)
		    kc = (int)(tc - 'A' + 1);

		if (!(kmod & (Mod1Mask | LockMask)))
		    if (!(kmod & ShiftMask) || !(kmod & ControlMask))
		       kmod = 0;
	    }
	    else
		kc = (int)tc;
	}
	else if (!strncmp(vis, "<del>", 5))
	    /* Because, weirdly, there is no keysymdef for the "Delete" key */
	    kc = (int)0x7f;
	else
	{
	    /* X11 keysym name handling */

	    /* If macro "Button" is used, then prepend	*/
	    /* "Pointer" to match the keysymdef.	*/
	    /* (Added by NP 10/20/04)			*/

	    char *pointerStr = NULL;
	    if (!strncmp(vis, "Button", 6))
	    {
		pointerStr = (char *)mallocMagic(9 + strlen(str));
		strcpy(pointerStr, "Pointer_");
		strcat(pointerStr, vis);
		vis = pointerStr;
	    }
	    ks = XStringToKeysym(vis);
	    kc = (ks != NoSymbol) ? ks & 0xffff : 0;
	    if (pointerStr != NULL) freeMagic(pointerStr);
	}
	return (kc | (kmod << 16));
    }
#endif

    *verbose = 1;
    if (strlen(str) == 1)
    {
	return (int)str[0];
    }
    else if (strlen(str) == 2 && *str == '^')
    {
	return (int)str[1] - 'A' + 1;
    }
    if (warn)
    {
	if (strcasecmp(MainDisplayType, "NULL") || (TxTkConsole == TRUE))
	    TxPrintf("Extended macros are unavailable"
			" with graphics type \"%s\".\n", MainDisplayType);
    }
    warn = 0;
    *verbose = 0;
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 * CommandLineTranslate
 * 	Convert X11 definitions to readline controls
 *
 * Results:
 *	A converted character (integer)
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
TranslateChar(key)
    int key;
{
    int rval = key;

#ifdef XLIB
    switch (key)
    {
	case XK_Left:
	    rval = (int)'\002';	/* Ctrl-B */
	    break;
	case XK_Right:
	    rval = (int)'\006';	/* Ctrl-F */
	    break;
	case XK_Up:
	    rval = (int)'\020';	/* Ctrl-P */
	    break;
	case XK_Down:
	    rval = (int)'\016';	/* Ctrl-N */
	    break;
	case XK_BackSpace:
	case XK_Delete:
	    rval = (int)'\010';	/* Ctrl-H */
	    break;
	case XK_Home:
	    rval = (int)'\001';	/* Ctrl-A */
	    break;
	case XK_End:
	    rval = (int)'\005';	/* Ctrl-E */
	    break;
    }
#endif
    return rval;
}
