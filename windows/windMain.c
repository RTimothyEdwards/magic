/* windMain.c -
 *
 *	This package implements overlapping windows for the
 *	Magic VLSI layout system.
 *
 * Design:
 *	Windows are structures that are kept in a doubly linked list.
 *	Windows near the front of the list are on top of windows further
 *	towards the tail of the list.  Each window has some information
 *	about what is in the window, as well as the size of the window
 *	(both unclipped and clipped to accommodate windows that overlay it).
 *	Transforms control what portion of the window's contents show up
 *	on the screen, and at what magnification.
 *
 *	Each window is owned by a client (the database, a menu package, etc.).
 *	The window package is notified of a new client by the AddClient
 *	call.  The client supplies routines to redisplay the contents of
 *	a window, and to do other things with the window (such as delete it).
 *	Each window is a view onto a surface maintained by the client.  The
 *	client may be asked to redisplay any part of this surface at any time.
 *	The client must also supply each window with an ID that uniquely
 *	identifies the surface.
 *
 *	There are currently two types of window packages supported:  Magic
 *	windows (implemented here) and Sun Windows.  In Magic windows, all
 * 	windows use the screen's coordinate system.  In Sun Windows, each
 *	window has it's own coordinate system with (0, 0) being at the lower
 *	left corner.  Also, under Sun Windows some of the screen managment
 *	stuff (such as clipping to obscuring areas and drawing of the
 *	screen background color) is ignored by us.
 *
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windMain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "windows/windInt.h"
#include "utils/stack.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "textio/textio.h"
#include "graphics/graphics.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "textio/txcommands.h"

/* The type of windows that this package will implement */
int WindPackageType = WIND_MAGIC_WINDOWS;

/* The size of our scroll bars -- may be set externally (see windows.h)
 */
int WindScrollBarWidth = 7;

/* ------ Internal variables that are global within the window package ----- */
clientRec *windFirstClientRec = NULL;	/* the head of the linked list 
					 * of clients 
					 */
MagWindow *windTopWindow = NULL;		/* the topmost window */
MagWindow *windBottomWindow = NULL;	/* ...and the bottom window */
extern Plane *windRedisplayArea;	/* See windDisplay.c for details. */


/*
 * ----------------------------------------------------------------------------
 * WindInit --
 *
 *	Initialize the window package.  No windows are created, but the
 *	package will be initialized so that it can do these things in the 
 *	future.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Variables internal to the window package are initialized.
 * ----------------------------------------------------------------------------
 */

void
WindInit()
{
    extern Stack *windGrabberStack;
    Rect ts;
    char glyphName[30];

    windClientInit();
    windGrabberStack = StackNew(2);
    windRedisplayArea = DBNewPlane((ClientData) TT_SPACE);

    sprintf(glyphName, "windows%d", WindScrollBarWidth);
    if (!GrReadGlyphs(glyphName, ".", SysLibPath, &windGlyphs))
	MainExit(1);
    GrTextSize("XWyqP", GR_TEXT_DEFAULT, &ts);
    windCaptionPixels = ts.r_ytop - ts.r_ybot + 3;
    WindAreaChanged((MagWindow *) NULL, (Rect *) NULL);
}


/*
 * ----------------------------------------------------------------------------
 * WindAddClient --
 *
 *	Add a new client of the window package.  The client must supply a
 *	set of routines, as described below.
 *
 * Results:
 *	A unique ID (of type WindClient) is returned.
 *	This is used to identify the client in future calls to the window 
 *	package.
 *
 * Routines supplied:
 *
 *	( A new window was just created for this client.  Do things to
 *	  initialize the window, such as filling in the caption and making
 *	  the contents be empty. The client may refuse to create a new
 *	  window by returning FALSE, otherwise the client should return
 *	  TRUE.  The client will get passed argc and argv, with the command
 *	  name stripped off.  The client may do whatever it wants with this.
 *	  It may even modify parts of the window record -- such as changing
 *	  the window's location on the screen.)
 *
 *	bool
 *	create(w, argc, argv)
 *	    MagWindow *w;
 *	    int argc;
 *	    char *argv[];
 *	{
 *	}
 *
 *	( One of the client's windows is about to be deleted.  Do whatever
 *	  needs to be done, such as freeing up dynamically allocated data
 *	  structures. Fields manipulated by the window package, such as
 *	  the caption, should not be freed by the client.  The client should
 *	  normally return TRUE.  If the client returns FALSE, the window
 *	  manager will refuse the request to delete the window.)
 *
 *	bool
 *	delete(w)
 *	    MagWindow *w;
 *	{
 *	}
 *
 *	( Redisplay an area of the screen.  The client is passed the window,
 *	  the area in his coordinate system, and a clipping rectangle in
 *	  screen coordinates. )
 *
 *	redisplay(w, clientArea, screenArea)
 *	    MagWindow *w;
 *	    Rect *clientArea, *screenArea;
 *	{
 *	}
 *
 *
 *	( The window is about to be moved or resized.  This procedure will
 *	  be called twice.  
 *
 *	  The first time (with 'final' == FALSE), the window 
 *	  will be passed in 'w' as it is now and a suggested new w_screenarea 
 *	  is passed in 'newpos'.  The client is free to modify 'newpos' to
 *	  be whatever screen location it desires.  The routine should not 
 *	  pass 'w' to any window procedure such as windMove since 'w' has
 *	  the old transform, etc. instead of the new one.
 *
 *	  On the second call ('final' == TRUE), the window 'w' has all of
 *	  it's fields updated, newpos is equal to w->w_frameArea, and the
 *	  client is free to do things like windMove which require a window
 *	  as an argument.  It should not modify newpos.
 *
 *	reposition(w, newpos, final)
 *	    MagWindow *w;
 *	    Rect *newpos	-- new w_framearea (screen area of window)
 *	    bool final;
 *	{
 *	}
 *
 *
 *	( A command has been issued to this window.  The client should
 *	  process it.  It is split into Unix-style argc and argv words. )
 *
 *	command(w, client, cmd)
 *	    MagWindow *w;
 *	    TxCommand *cmd;
 *	{
 *	}
 *
 *	( A command has just finished.  Update any screen info that may have
 *	  been changed as a result. )
 *
 *	update()
 *	{
 *	}
 *
 * Side effects:
 *	Internal tables are expanded to include the new client.
 * ----------------------------------------------------------------------------
 */

WindClient
WindAddClient(clientName, create, delete, redisplay, command, update, 
		exitproc, reposition, icon)
    char *clientName;		/* A textual name for the client.  This
				 * name will be visable in the user
				 * interface as the name to use to switch
				 * a window over to a new client
				 */
    bool (*create)();
    bool (*delete)();
    void (*redisplay)();
    void (*command)();
    void (*update)();
    bool (*exitproc)();
    void (*reposition)();
    GrGlyph *icon;		/* An icon to draw when the window is closed.
				 * (currently for Sun Windows only).
				 */
{
    clientRec *res;

    ASSERT( (clientName != NULL), "WindAddClient");
    ASSERT( (command != NULL), "WindAddClient");

    res = (clientRec *) mallocMagic(sizeof(clientRec));
    res->w_clientName = clientName;
    res->w_create = create;
    res->w_delete = delete;
    res->w_redisplay = redisplay;
    res->w_command = command;
    res->w_update = update;
    res->w_exit = exitproc;
    res->w_reposition = reposition;
    res->w_icon = icon;
    res->w_nextClient = windFirstClientRec;

    /* The command and function tables are dynamically allocated.  */
    /* Commands and functions should be registered with the client */
    /* using the WindAddCommand() function.			   */

    res->w_commandTable = (char **)mallocMagic(sizeof(char *));
    *(res->w_commandTable) = NULL;
    res->w_functionTable = (void (**)())mallocMagic(sizeof(void (*)()));
    *(res->w_functionTable) = NULL;

    windFirstClientRec = res;

    return (WindClient) res;
}


/*
 * ----------------------------------------------------------------------------
 * WindGetClient --
 *
 *	Looks up the unique ID of a client of the window package.
 *
 * Results:
 *	A variable of type WindClient is returned if the client was found,
 *	otherwise NULL is returned.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

WindClient
WindGetClient(clientName, exact)
    char *clientName;	/* the textual name of the client */
    bool exact;		/* must the name be exact, or are abbreviations allowed */
{
    clientRec *cr, *found;
    int length;

    /* Accept only an exact match */

    if (exact)
    {
	for (cr = windFirstClientRec; cr != (clientRec *) NULL; 
		cr = cr->w_nextClient)
	    if (!strcmp(clientName, cr->w_clientName))
		return (WindClient)cr;
	return (WindClient) NULL;
    }

    /* Accept any unique abbreviation */

    found = NULL;
    length = strlen(clientName);
    for (cr = windFirstClientRec; cr != (clientRec *) NULL; 
	    cr = cr->w_nextClient)
    {
	if (!strncmp(clientName, cr->w_clientName, length))
	{
	    if (found != NULL) return (WindClient) NULL;
	    found = cr;
	}
    }

    return (WindClient) found;
}

/*
 * ----------------------------------------------------------------------------
 * WindNextClient --
 *
 *	Return the w_nextClient record to the caller as a WindClient
 *	variable.  If "client" is 0, pass the first client record.
 *	This allows the calling routine to enumerate all the known
 *	clients.
 *
 * Results:
 *	Type WindClient is returned.  If the end of the list is
 *	reached, (WindClient)NULL (0) is returned.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

WindClient
WindNextClient(client)
    WindClient client;
{
    clientRec *cr = (clientRec *)client;
    int length;

    if (cr == NULL)
	return (WindClient)windFirstClientRec;
    else
	return (WindClient)(cr->w_nextClient);
}

/*
 * ----------------------------------------------------------------------------
 * WindPrintClientList --
 *
 *	Print the name of each client of the window package.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
WindPrintClientList(wizard)
    bool wizard;	/* If true print the names of ALL clients, even those
			 * that don't have user-visable windows */
{
    clientRec *cr;

    for (cr = windFirstClientRec; cr != (clientRec *) NULL; 
	    cr = cr->w_nextClient) {
	if (wizard || (cr->w_clientName[0] != '*'))
	    TxError("	%s\n", cr->w_clientName);
	}
}

/*
 * ----------------------------------------------------------------------------
 * WindExecute --
 *
 *	Execute the command associated with a windClient
 *
 * Results:
 *	Returns the command index on success.  Returns -1 if the
 *	command was not found in the client's command list.  Returns
 *	-2 if the procedure was sent an empty command.
 *
 * Side effects:
 *	Whatever is done by the command execution.
 *
 * ----------------------------------------------------------------------------
 */

int
WindExecute(w, rc, cmd)
    MagWindow *w;
    WindClient rc;
    TxCommand *cmd;
{
    int cmdNum;
    clientRec *client = (clientRec *) rc;
    char **commandTable = client->w_commandTable;
    void (**functionTable)() = client->w_functionTable;

    if (cmd->tx_argc > 0)
    {
	cmdNum = Lookup(cmd->tx_argv[0], commandTable);

	if (cmdNum >= 0)
	{
	    (*functionTable[cmdNum])(w, cmd);
	    return cmdNum;
	}
	return -1;
    }
    return -2;
}

/*
 * ----------------------------------------------------------------------------
 * WindAddCommand --
 *
 *	Add a command to the indicated client.  The command is passed
 *	in "text", which also contains the (1-line) help text for the
 *	command.  "func" is a function pointer, and "volatile" is TRUE
 *	if the command "text" is dynamically allocated and must be
 *	copied before adding to the client.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	The memory allocated to the command and function pointers may
 *	be reallocated and the entries in the client record updated.
 *
 * ----------------------------------------------------------------------------
 */

void
WindAddCommand(rc, text, func, dynamic)
    WindClient rc;
    char *text;
    void (*func)();
    bool dynamic;
{
    int cidx, numCommands = 0;
    clientRec *client = (clientRec *) rc;
    char **commandTable = client->w_commandTable;
    void (**functionTable)() = client->w_functionTable;
    char **newcmdTable;
    void (**newfnTable)();

    /* Find the number of commands and functions, increment by one, and */
    /* Allocate a new array of pointers.				*/

    while (commandTable[numCommands] != NULL) numCommands++;
    numCommands++;

    newcmdTable = (char **)mallocMagic((numCommands + 1) * sizeof(char *));
    newfnTable = (void (**)())mallocMagic((numCommands + 1) * sizeof(void (*)()));

    /* Copy the old values, inserting the new command in alphabetical	*/
    /* order.								*/

    for (cidx = 0; (commandTable[cidx] != NULL) && 
		(strcmp(commandTable[cidx], text) < 0); cidx++)
    {
	newcmdTable[cidx] = commandTable[cidx];
	newfnTable[cidx] = functionTable[cidx];
    }

    if (dynamic)
	newcmdTable[cidx] = StrDup(NULL, text);
    else
	newcmdTable[cidx] = text;
    newfnTable[cidx] = func;

    for (; commandTable[cidx] != NULL; cidx++)
    {
	newcmdTable[cidx + 1] = commandTable[cidx];
	newfnTable[cidx + 1] = functionTable[cidx];
    }

    newcmdTable[cidx + 1] = NULL;

    /* Release memory for the original pointers, and replace the	*/
    /* pointers in the client record.					*/

    freeMagic(commandTable);
    freeMagic(functionTable);

    client->w_commandTable = newcmdTable;
    client->w_functionTable = newfnTable;
}

/*
 * ----------------------------------------------------------------------------
 * WindReplaceCommand --
 *
 *	Change the function for the indicated command.  This routine
 *	is mainly used by the Tcl module interface, where commands
 *	are registered with the command interpreter pointing to an
 *	"auto" function which loads the module, then calls this routine
 *	to replace the auto-load function with the real one.
 *
 *      Note that this routine matches to the length of "command", then
 *	checks one character beyond in the command table to ensure that
 *	we don't inadvertently change a command which happens to be a
 *	substring of the intended command.  In cases where this is
 *	intended (e.g., "ext" and "extract"), the routine must be called
 *	separately for each command string.
 *
 * Results:
 *	0 on success, -1 if the command was not found.
 *
 * Side effects:
 *	The clientRec structure for the DBWind interface is altered.
 *
 * ----------------------------------------------------------------------------
 */

int
WindReplaceCommand(rc, command, newfunc)
    WindClient rc;
    char *command;
    void (*newfunc)();
{
    int cidx, clen;
    clientRec *client = (clientRec *) rc;
    char **commandTable = client->w_commandTable;
    void (**functionTable)() = client->w_functionTable;

    clen = strlen(command);

    for (cidx = 0; commandTable[cidx] != NULL; cidx++)
	if (!strncmp(commandTable[cidx], command, clen))
	    if (!isalnum(*(commandTable[cidx] + clen)))
	    {
		functionTable[cidx] = newfunc;
		return 0;
	    }

    return -1;
}

/*
 * ----------------------------------------------------------------------------
 * WindGetCommandTable --
 * 
 *	For functions wishing to parse the command table of a client
 *	directly, this routine returns a pointer to the top of the
 *	table.  The only purpose of this routine is to export the
 *	w_commandTable value inside the clientRec structure, which is
 *	not itself exported.
 *
 * Results:
 *	A pointer to the top of the command table of the indicated
 *	client.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

char **
WindGetCommandTable(rc)
    WindClient rc;
{
    clientRec *client = (clientRec *) rc;
    return client->w_commandTable;
}
