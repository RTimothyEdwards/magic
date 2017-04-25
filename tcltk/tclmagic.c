/*----------------------------------------------------------------------*/
/* tclmagic.c --- Creates the interpreter-wrapped version of magic.	*/
/*									*/
/*   Written by Tim Edwards August 2002					*/
/*									*/
/*   Note that this file is tied to Tcl.  The original version (from	*/
/*   around April 2002) relied on SWIG, the only differences being	*/
/*   as few %{ ... %} boundaries and the replacement of the 		*/
/*   Tclmagic_Init function header with "%init %{", and call the	*/
/*   file "tclmagic.i".  However, the rest of the associated wrapper	*/
/*   code got so dependent on Tcl commands that there is no longer any	*/
/*   point in using SWIG.						*/
/*									*/
/*   When using SWIG, the Makefile requires:				*/
/*									*/
/*	tclmagic.c: tclmagic.i						*/
/*		swig -tcl8 -o tclmagic.c tclmagic.i			*/
/*									*/
/*----------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "tcltk/tclmagic.h"
#include "utils/main.h"   
#include "utils/magic.h"   
#include "utils/geometry.h"
#include "tiles/tile.h"  
#include "utils/hash.h"  
#include "utils/dqueue.h"
#include "database/database.h"
#include "windows/windows.h"
#include "commands/commands.h"
#include "utils/utils.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/signals.h"
#include "graphics/graphics.h"
#include "utils/malloc.h" 
#include "dbwind/dbwind.h"

/*
 * String containing the version number of magic.  Don't change the string
 * here, nor its format.  It is updated by the Makefile in this directory.
 */

char *MagicVersion = MAGIC_VERSION;
char *MagicRevision = MAGIC_REVISION;
char *MagicCompileTime = MAGIC_DATE;

Tcl_Interp *magicinterp;
Tcl_Interp *consoleinterp;

HashTable txTclTagTable;

Tcl_ChannelType inChannel;

/* Forward declarations */

int TerminalInputProc(ClientData, char *, int, int *);
void TxFlushErr();
void TxFlushOut();
void RegisterTkCommands();

/*--------------------------------------------------------------*/
/* Verify if a command has a tag callback.			*/
/*--------------------------------------------------------------*/

int
TagVerify(keyword)
    char *keyword;
{
    char *croot, *postcmd;
    HashEntry *entry;

    /* Skip over namespace qualifier, if any */

    croot = keyword;
    if (!strncmp(croot, "::", 2)) croot += 2;
    if (!strncmp(croot, "magic::", 7)) croot += 7;

    entry = HashLookOnly(&txTclTagTable, croot);
    postcmd = (entry) ? (char *)HashGetValue(entry) : NULL;
    return (postcmd) ? TRUE : FALSE;
}

/*--------------------------------------------------------------*/
/* Find any tags associated with a command and execute them.	*/
/*--------------------------------------------------------------*/

static int
TagCallback(interp, tkpath, argc, argv)
    Tcl_Interp *interp;
    char *tkpath;
    int argc;		/* original command's number of arguments */
    char *argv[];	/* original command's argument list */
{
    int argidx, result = TCL_OK;
    char *postcmd, *substcmd, *newcmd, *sptr, *sres;
    char *croot;
    HashEntry *entry;
    Tcl_SavedResult state;
    bool reset = FALSE;
    int cmdnum;

    /* No command, no action */

    if (argc == 0) return TCL_OK;

    /* Skip over namespace qualifier, if any */

    croot = argv[0];
    if (!strncmp(croot, "::", 2)) croot += 2;
    if (!strncmp(croot, "magic::", 7)) croot += 7;

    entry = HashLookOnly(&txTclTagTable, croot);
    postcmd = (entry) ? (char *)HashGetValue(entry) : NULL;

    if (postcmd)
    {
	/* The Tag callback should not increase the command number	*/
	/* sequence, so save it now and restore it before returning.	*/ 
	cmdnum = TxCommandNumber;

	substcmd = (char *)mallocMagic(strlen(postcmd) + 1);
	strcpy(substcmd, postcmd);
	sptr = substcmd;

	/*--------------------------------------------------------------*/
	/* Parse "postcmd" for Tk-substitution escapes			*/
	/* Allowed escapes are:						*/
	/* 	%W	substitute the tk path of the layout window	*/
	/*	%r	substitute the previous Tcl result string	*/
	/*	%R	substitute the previous Tcl result string and	*/
	/*		reset the Tcl result.				*/
	/*	%[0-5]  substitute the argument to the original command	*/
	/*	%%	substitute a single percent character		*/
	/*	%*	(all others) no action: print as-is.		*/
	/*--------------------------------------------------------------*/

	while ((sptr = strchr(sptr, '%')) != NULL)
	{
	    switch (*(sptr + 1))
	    {
		case 'W':

		    /* In the case of the %W escape, first we see if a Tk */
		    /* path has been passed in the argument.  If not, get */
		    /* the window path if there is only one window.       */
		    /* Otherwise, the window is unknown so we substitute  */
		    /* a null list "{}".				  */ 

		    if (tkpath == NULL)
		    {
			MagWindow *w = NULL;
			windCheckOnlyWindow(&w, DBWclientID);
			if (w != NULL && !(w->w_flags & WIND_OFFSCREEN))
			{
			    Tk_Window tkwind = (Tk_Window) w->w_grdata;
			    if (tkwind != NULL) tkpath = Tk_PathName(tkwind);
			}
		    }
		    if (tkpath == NULL)
			newcmd = (char *)mallocMagic(strlen(substcmd) + 2);
		    else
			newcmd = (char *)mallocMagic(strlen(substcmd) + strlen(tkpath));

		    strcpy(newcmd, substcmd);

		    if (tkpath == NULL)
			strcpy(newcmd + (int)(sptr - substcmd), "{}");
		    else
			strcpy(newcmd + (int)(sptr - substcmd), tkpath);

		    strcat(newcmd, sptr + 2);
		    freeMagic(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;
		    break;

		case 'R':
		    reset = TRUE;
		case 'r':
		    sres = (char *)Tcl_GetStringResult(magicinterp);
		    newcmd = (char *)mallocMagic(strlen(substcmd)
				+ strlen(sres) + 1);
		    strcpy(newcmd, substcmd);
		    sprintf(newcmd + (int)(sptr - substcmd), "\"%s\"", sres);
		    strcat(newcmd, sptr + 2);
		    freeMagic(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;

		    break;

		case '0': case '1': case '2': case '3': case '4': case '5':
		    argidx = (int)(*(sptr + 1) - '0');
		    if ((argidx >= 0) && (argidx < argc))
		    {
		        newcmd = (char *)mallocMagic(strlen(substcmd)
				+ strlen(argv[argidx]));
		        strcpy(newcmd, substcmd);
			strcpy(newcmd + (int)(sptr - substcmd), argv[argidx]);
			strcat(newcmd, sptr + 2);
			freeMagic(substcmd);
			substcmd = newcmd;
			sptr = substcmd;
		    }
		    else if (argidx >= argc)
		    {
		        newcmd = (char *)mallocMagic(strlen(substcmd) + 1);
		        strcpy(newcmd, substcmd);
			strcpy(newcmd + (int)(sptr - substcmd), sptr + 2);
			freeMagic(substcmd);
			substcmd = newcmd;
			sptr = substcmd;
		    }
		    else sptr++;
		    break;

		case '%':
		    newcmd = (char *)mallocMagic(strlen(substcmd) + 1);
		    strcpy(newcmd, substcmd);
		    strcpy(newcmd + (int)(sptr - substcmd), sptr + 1);
		    freeMagic(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;
		    break;

		default:
		    break;
	    }
	}

	/* fprintf(stderr, "Substituted tag callback is \"%s\"\n", substcmd); */
	/* fflush(stderr); */

	Tcl_SaveResult(interp, &state);
	result = Tcl_EvalEx(interp, substcmd, -1, 0);
	if ((result == TCL_OK) && (reset == FALSE))
	    Tcl_RestoreResult(interp, &state);
	else
	    Tcl_DiscardResult(&state);

	freeMagic(substcmd);
	TxCommandNumber = cmdnum;	/* restore original value */
    }
    return result;
}

/*--------------------------------------------------------------*/
/* Add a command tag callback					*/
/*--------------------------------------------------------------*/

static int
AddCommandTag(ClientData clientData,
        Tcl_Interp *interp, int argc, char *argv[])
{
    HashEntry *entry;
    char *hstring;

    if (argc != 2 && argc != 3)
	return TCL_ERROR;

    entry = HashFind(&txTclTagTable, argv[1]);
 
    if (entry == NULL) return TCL_ERROR;

    hstring = (char *)HashGetValue(entry);

    if (argc == 2)
    {
	Tcl_SetResult(magicinterp, hstring, NULL);
	return TCL_OK;
    }

    if (hstring != NULL) freeMagic(hstring);

    if (strlen(argv[2]) == 0)
    {
	HashSetValue(entry, NULL);
    }
    else
    {
	hstring = StrDup((char **)NULL, argv[2]);
	HashSetValue(entry, hstring);
    }
    return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Dispatch a command from Tcl					*/
/* See TxTclDispatch() in textio/txCommands.c			*/
/*--------------------------------------------------------------*/

static int
_tcl_dispatch(ClientData clientData,
        Tcl_Interp *interp, int argc, char *argv[])
{
    int wval; 
    int result, idx;
    Tcl_Obj *objv0;
    char *argv0, *tkwind;

    /* Check command (argv[0]) against known conflicting */
    /* command names.  If the command is potentially a	 */
    /* Tcl/Tk command, try it as such, first.  If a Tcl	 */
    /* error is returned, then try it as a magic	 */
    /* command.  Note that the other way (try the magic	 */
    /* command first) would necessitate setting Tcl 	 */
    /* results for every magic command.  Too much work.	 */

    static char *conflicts[] =
    {
	"clockwise", "flush", "load", "label", "array", "grid", NULL
    };
    static char *resolutions[] =
    {
	"orig_clock", "tcl_flush", "tcl_load", "tcl_label", "tcl_array",
	"tcl_grid", NULL
    };

    typedef enum
    {
	IDX_CLOCK, IDX_FLUSH, IDX_LOAD, IDX_LABEL, IDX_ARRAY,
	IDX_GRID
    } conflictCommand;

    /* Skip any "::" namespace prefix before parsing */
    argv0 = argv[0];
    if (!strncmp(argv0, "::", 2)) argv0 += 2;

    objv0 = Tcl_NewStringObj(argv0, strlen(argv0));
    if (Tcl_GetIndexFromObj(interp, objv0, (CONST84 char **)conflicts,
	"overloaded command", 0, &idx) == TCL_OK)
    {
	int i;
	Tcl_Obj **objv = (Tcl_Obj **)Tcl_Alloc(argc * sizeof(Tcl_Obj *));
	
	/* Create a Tcl_Obj array suitable for calling Tcl_EvalObjv.	*/
	/* The first argument is changed from the magic command name to	*/
	/* "tcl" + the command name.  This assumes that all conflicting	*/
	/* command names have been so renamed in the startup script!	*/

	objv[0] = Tcl_NewStringObj(resolutions[idx], strlen(resolutions[idx]));
	Tcl_IncrRefCount(objv[0]);

	for (i = 1; i < argc; i++)
	{
	    objv[i] = Tcl_NewStringObj(argv[i], strlen(argv[i]));
	    Tcl_IncrRefCount(objv[i]);
	}

	result = Tcl_EvalObjv(interp, argc, objv, 0);

	for (i = 0; i < argc; i++)
	    Tcl_DecrRefCount(objv[i]);
	Tcl_Free((char *)objv);

	if (result == TCL_OK)
	    return result;

	/* The rule is to execute Magic commands for any Tcl command 	*/
	/* with the same name that returns an error.  However, this	*/
	/* rule hangs magic when the "load" command is used on a shared	*/
	/* object file that fails to load properly.  So if the filename	*/
	/* has an extension which is not ".mag", we will return the 	*/
	/* error.							*/

	/* Updated 1/20/2015:  Need to check for a '.' AFTER the last	*/
	/* slash, so as to avoid problems with ./, ../, etc.		*/

	if (idx == IDX_LOAD)
	{
	    char *dotptr, *slashptr;
	    if (argc >= 2)
	    {
		slashptr = strrchr(argv[1], '/');
		if (slashptr == NULL)
		    slashptr = argv[1];
		else
		    slashptr++;

		if ((dotptr = strrchr(slashptr, '.')) != NULL)
		    if (strcmp(dotptr + 1, "mag"))
			return result;
	    }
	}
    }
    Tcl_ResetResult(interp);

    if (TxInputRedirect == TX_INPUT_REDIRECTED)
	TxInputRedirect = TX_INPUT_PENDING_RESET;

    wval = TxTclDispatch(clientData, argc, argv, TRUE);

    if (TxInputRedirect == TX_INPUT_PENDING_RESET)
	TxInputRedirect = TX_INPUT_NORMAL;

    /* If the command did not pass through _tk_dispatch, but the command was	*/
    /* entered by key redirection from a window, then TxInputRedirect will be	*/
    /* set to TX_INPUT_PROCESSING and the window ID will have been set by	*/
    /* TxSetPoint().  Do our level best to find the Tk window name.		*/

    if (TxInputRedirect == TX_INPUT_PROCESSING)
    {
	if (GrWindowNamePtr)
	{
	    MagWindow *mw = WindSearchWid(TxGetPoint(NULL));
	    if (mw != NULL)
		tkwind = (*GrWindowNamePtr)(mw);
	    else
		tkwind = NULL;
	}
	else
	    tkwind = NULL;
    }
    else
	tkwind = NULL;

    // Pass back an error if TxTclDispatch failed
    if (wval != 0) return TCL_ERROR;

    return TagCallback(interp, tkwind, argc, argv);
}

/*--------------------------------------------------------------*/
/* Dispatch a window-related command.  The first argument is	*/
/* the window to which the command should be directed, so we	*/
/* determine which window this is, set "TxCurCommand" values	*/
/* to point to the window, then dispatch the command.		*/
/*--------------------------------------------------------------*/

static int
_tk_dispatch(ClientData clientData,
        Tcl_Interp *interp, int argc, char *argv[])
{
    int id;
    char *tkpath;
    char *arg0;
    Point txp;

    if (GrWindowIdPtr)
    {
	/* Key macros set the point from the graphics module code but	*/
	/* set up the command to be dispatched via _tk_dispatch().	*/
	/* Therefore it is necessary to check if a point position	*/
	/* has already been set for this command.  If not, then the	*/
	/* command was probably called from the command entry window,	*/
	/* so we choose an arbitrary point which is somewhere in the	*/
	/* window, so that command functions have a point of reference.	*/

	id = (*GrWindowIdPtr)(argv[0]);

	if (TxGetPoint(&txp) != id)
	{
	    /* This is a point in the window, inside the	*/
	    /* scrollbars if they are managed by magic.		*/

	    txp.p_x = 20;
	    txp.p_y = 20;
	}
	TxSetPoint(txp.p_x, txp.p_y, id);
	arg0 = argv[0];
	argc--;
	argv++;
    }

    TxTclDispatch(clientData, argc, argv, FALSE);

    /* Get pathname of window and pass to TagCallback */
    return TagCallback(interp, arg0, argc, argv);
}

/*--------------------------------------------------------------*/
/* Set up a window to use commands via _tk_dispatch		*/
/*--------------------------------------------------------------*/

void
MakeWindowCommand(char *wname, MagWindow *mw)
{
    char *tclcmdstr;

    Tcl_CreateCommand(magicinterp, wname, (Tcl_CmdProc *)_tk_dispatch,
		(ClientData)mw, (Tcl_CmdDeleteProc *) NULL);

    /* Force the window manager to use magic's "close" command to close	*/
    /* down a window.							*/

    tclcmdstr = (char *)mallocMagic(52 + 2 * strlen(wname));
    sprintf(tclcmdstr, "wm protocol %s WM_DELETE_WINDOW "
		"{magic::closewindow %s}", wname, wname);
    Tcl_EvalEx(magicinterp, tclcmdstr, -1, 0);
    freeMagic(tclcmdstr);
}

/*------------------------------------------------------*/
/* Main startup procedure				*/
/*------------------------------------------------------*/
 
static int
_magic_initialize(ClientData clientData,
        Tcl_Interp *interp, int argc, char *argv[])
{
    WindClient client;
    int n, i;
    char keyword[100];
    char *kwptr = keyword + 7;
    char **commandTable;
    int result;

    /* Is magic being executed in a slave interpreter? */

    if ((consoleinterp = Tcl_GetMaster(interp)) == NULL)
	consoleinterp = interp;

    // Force tkcon to send output to terminal during initialization
    else
    {
  	RuntimeFlags |= (MAIN_TK_CONSOLE | MAIN_TK_PRINTF);
    	Tcl_Eval(consoleinterp, "rename ::puts ::unused_puts\n");
    	Tcl_Eval(consoleinterp, "rename ::tkcon_tcl_puts ::puts\n");
    }

    /* Did we start in the same interpreter as we initialized? */
    if (magicinterp != interp)
    {
	TxError("Warning:  Switching interpreters.  Tcl-magic is not set up "
		"to handle this.\n");
	magicinterp = interp;
    }

    if (mainInitBeforeArgs(argc, argv) != 0) goto magicfatal;
    if (mainDoArgs(argc, argv) != 0) goto magicfatal;

    // Redirect output back to the console
    if (TxTkConsole)
    {
  	RuntimeFlags &= ~MAIN_TK_PRINTF;
    	Tcl_Eval(consoleinterp, "rename ::puts ::tkcon_tcl_puts\n");
    	Tcl_Eval(consoleinterp, "rename ::unused_puts ::puts\n");
    }

    /* Identify version and revision */

    TxPrintf("\nMagic %s revision %s - Compiled on %s.\n", MagicVersion,
                MagicRevision, MagicCompileTime);
    TxPrintf("Starting magic under Tcl interpreter\n");
    if (TxTkConsole)
	TxPrintf("Using Tk console window\n");
    else
	TxPrintf("Using the terminal as the console.\n");
    TxFlushOut();

    if (mainInitAfterArgs() != 0) goto magicfatal;

    /* Registration of commands is performed after calling the	*/
    /* start function, not after initialization, as the command */
    /* modularization requires magic initialization to get a	*/
    /* valid DBWclientID, windClientID, etc.			*/

    sprintf(keyword, "magic::");

    /* Work through all the known clients, and register the	*/
    /* commands of all of them.					*/

    client = (WindClient)NULL;
    while ((client = WindNextClient(client)) != NULL)
    {
	commandTable = WindGetCommandTable(client);
	for (n = 0; commandTable[n] != NULL; n++)
	{
	    sscanf(commandTable[n], "%s ", kwptr); /* get first word */
	    Tcl_CreateCommand(interp, keyword, (Tcl_CmdProc *)_tcl_dispatch,
			(ClientData)NULL, (Tcl_CmdDeleteProc *) NULL);
	}
    }

    /* Extra commands provided by the Tk graphics routines	*/
    /* (See graphics/grTkCommon.c)				*/
    /* (Unless "-dnull" option has been given)			*/

    if (strcmp(MainDisplayType, "NULL"))
	RegisterTkCommands(interp);

    /* Set up the console so that its menu option File->Exit	*/
    /* calls magic's exit routine first.  This should not be	*/
    /* done in console.tcl, or else it puts the console in a	*/
    /* state where it is difficult to exit, if magic doesn't	*/
    /* start up correctly.					*/

    if (TxTkConsole)
    {
	Tcl_Eval(consoleinterp, "rename ::exit ::quit\n");
	Tcl_Eval(consoleinterp, "proc ::exit args {slave eval quit}\n");
    }

    return TCL_OK;

magicfatal:
    TxResetTerminal();
    Tcl_SetResult(interp, "Magic initialization encountered a fatal error.", NULL);
    return TCL_ERROR;
}

/*--------------------------------------------------------------*/

typedef struct FileState {
    Tcl_Channel channel;
    int fd;
    int validMask;
} FileState;

/*--------------------------------------------------------------*/
/* "Wizard" command for manipulating run-time flags.		*/
/*--------------------------------------------------------------*/

static int
_magic_flags(ClientData clientData,
        Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int index, index2;
    bool value;
    static char *flagOptions[] = {"debug", "recover", "silent",
		"window", "console", "printf", (char *)NULL};
    static char *yesNo[] = {"off", "no", "false", "0", "on", "yes",
		"true", "1", (char *)NULL};

    if ((objc != 2) && (objc != 3)) {
	Tcl_WrongNumArgs(interp, 1, objv, "flag ?value?"); 
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)flagOptions,
		"option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 2) {
	switch (index) {
	    case 0:
	        value = (RuntimeFlags & MAIN_DEBUG) ? TRUE : FALSE;
		break;
	    case 1:
	        value = (RuntimeFlags & MAIN_RECOVER) ? TRUE : FALSE;
		break;
	    case 2:
	        value = (RuntimeFlags & MAIN_SILENT) ? TRUE : FALSE;
		break;
	    case 3:
	        value = (RuntimeFlags & MAIN_MAKE_WINDOW) ? TRUE : FALSE;
		break;
	    case 4:
	        value = (RuntimeFlags & MAIN_TK_CONSOLE) ? TRUE : FALSE;
		break;
	    case 5:
	        value = (RuntimeFlags & MAIN_TK_PRINTF) ? TRUE : FALSE;
		break;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(value));
    }
    else {
	if (Tcl_GetIndexFromObj(interp, objv[2], (CONST84 char **)yesNo,
		"value", 0, &index2) != TCL_OK)
	    return TCL_ERROR;

	value = (index2 > 3) ? TRUE : FALSE;
	switch (index) {
	    case 0:
		if (value == TRUE)
		    RuntimeFlags |= MAIN_DEBUG;
		else
		    RuntimeFlags &= ~MAIN_DEBUG;
		break;
	    case 1:
		if (value == TRUE)
		    RuntimeFlags |= MAIN_RECOVER;
		else
		    RuntimeFlags &= ~MAIN_RECOVER;
		break;
	    case 2:
		if (value == TRUE)
		    RuntimeFlags |= MAIN_SILENT;
		else
		    RuntimeFlags &= ~MAIN_SILENT;
		break;
	    case 3:
		if (value == TRUE)
		    RuntimeFlags |= MAIN_MAKE_WINDOW;
		else
		    RuntimeFlags &= ~MAIN_MAKE_WINDOW;
		break;
	    case 4:
		if (value == TRUE)
		    RuntimeFlags |= MAIN_TK_CONSOLE;
		else
		    RuntimeFlags &= ~MAIN_TK_CONSOLE;
		break;
	    case 5:
		if (value == TRUE)
		    RuntimeFlags |= MAIN_TK_PRINTF;
		else
		    RuntimeFlags &= ~MAIN_TK_PRINTF;
		break;
	}
    }
    return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Post-initialization:  read in the magic startup files and	*/
/* load any initial layout.  Note that this is not done via	*/
/* script, but probably should be.				*/
/*--------------------------------------------------------------*/

static int
_magic_startup(ClientData clientData,
        Tcl_Interp *interp, int argc, char *argv[])
{
    /* Execute contents of startup files and load any initial cell */

    if (mainInitFinal() != 0)
    {
	/* We don't want mainInitFinal errors to return TCL_ERROR from	*/
	/* magic::start; otherwise, the window won't come up.  As long	*/
	/* as we have successfully passed mainInitAfterArgs(), magic is	*/
	/* fundamentally sound.						*/

	Tcl_SetResult(interp,
		"Magic encountered problems with the startup files.",
		NULL);
    }

    TxResetTerminal();

    if (TxTkConsole)
    {
	Tcl_EvalEx(consoleinterp, "tkcon set ::tkcon::OPT(showstatusbar) 1", -1, 0);
	TxSetPrompt('%');
    }
    else
    {
	Tcl_Channel oldchannel;
	Tcl_ChannelType *stdChannel;
	FileState *fsPtr, *fsOrig;

	/* Use the terminal.				  */
	/* Replace the input proc for stdin with our own. */

	oldchannel = Tcl_GetStdChannel(TCL_STDIN);	// Get existing stdin
	fsOrig = Tcl_GetChannelInstanceData(oldchannel);

	/* Copy the structure from the old to the new channel */
	stdChannel = (Tcl_ChannelType *)Tcl_GetChannelType(oldchannel);
	memcpy(&inChannel, stdChannel, sizeof(Tcl_ChannelType));
	inChannel.inputProc = TerminalInputProc;

	fsPtr = (FileState *)Tcl_Alloc(sizeof(FileState));
	fsPtr->validMask = fsOrig->validMask;
	fsPtr->fd = fsOrig->fd;
	fsPtr->channel = Tcl_CreateChannel(&inChannel, "stdin",
	 	(ClientData)fsPtr, TCL_READABLE);

	Tcl_SetStdChannel(fsPtr->channel, TCL_STDIN);	// Apply new stdin
	Tcl_RegisterChannel(NULL, fsPtr->channel);
    }

    return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tk version of TxDialog					*/
/*--------------------------------------------------------------*/

int
TxDialog(prompt, responses, defresp)
    char *prompt;
    char *(responses[]);
    int defresp;
{
    Tcl_Obj *objPtr;
    int code, result, pos;
    char *evalstr, *newstr;

    /* Ensure that use of TxPrintString doesn't overwrite the	*/
    /* value of prompt my making a copy of it.			*/
    /* 5/11/05---use Tcl_escape() to do the duplication; this	*/
    /* ensures that cell names with special characters like '$'	*/
    /* will be handled properly.				*/

    newstr = Tcl_escape(prompt);
    /* newstr = StrDup((char **)NULL, prompt); */
    evalstr = TxPrintString("tk_dialog .dialog \"Dialog\""
	" \"%s\" {} %d ", newstr, defresp);
    /* freeMagic(newstr); */
    Tcl_Free(newstr);		/* Tcl_escape() uses Tcl_Alloc() */

    for (pos = 0; responses[pos] != 0; pos++)
    {
	newstr = StrDup((char **)NULL, evalstr);
	evalstr = TxPrintString("%s \"%s\" ", newstr,
		responses[pos]);
	freeMagic(newstr);
    }

    Tcl_EvalEx(magicinterp, evalstr, -1, 0);
    objPtr = Tcl_GetObjResult(magicinterp);
    result = Tcl_GetIntFromObj(magicinterp, objPtr, &code);

    if (result == TCL_OK) return code;
    else return -1;
}

/*--------------------------------------------------------------*/
/* TxUseMore and TxStopMore are dummy functions, although they	*/
/* could be used to set up a top-level window containing the	*/
/* result (redefine "puts" to write to the window).		*/
/*--------------------------------------------------------------*/

void
TxUseMore()
{
}

/*--------------------------------------------------------------*/

void
TxStopMore()
{
}

/*--------------------------------------------------------------*/
/* Set the prompt, if we are using the TkCon console		*/
/*--------------------------------------------------------------*/

extern char txPromptChar;

void
TxSetPrompt(ch)
    char ch;
{   
    Tcl_SavedResult state;
    char promptline[16];

    if (TxTkConsole)
    {
	sprintf(promptline, "replaceprompt %c", ch);
	Tcl_SaveResult(consoleinterp, &state);
	Tcl_EvalEx(consoleinterp, promptline, 15, 0);
	Tcl_RestoreResult(consoleinterp, &state);
    }
}   

/*--------------------------------------------------------------*/
/* Get a line from stdin (Tcl replacement for Tx function)	*/
/*--------------------------------------------------------------*/

char *
TxGetLinePfix(dest, maxChars, prefix)
    char *dest;
    int maxChars;
    char *prefix;
{
    Tcl_Obj *objPtr;
    int charsStored, length;
    char *string;

    if (TxTkConsole)
    {
	/* Use dialog function (must be defined in magic.tcl!)	*/
        if (prefix != NULL)
	{
	    string = Tcl_Alloc(20 + strlen(prefix));
	    sprintf(string, "magic::dialog \"\" \"%s\"\n", prefix);
	    Tcl_EvalEx(magicinterp, string, -1, 0);
	    Tcl_Free(string);
	}
	else
	    Tcl_EvalEx(magicinterp, "magic::dialog", 13, 0);
    }
    else
    {
	if (prefix != NULL)
	{
	    TxPrintf("%s", prefix);
	    TxFlushOut();
	}
	Tcl_EvalEx(magicinterp, "gets stdin", 10, 0);
    }

    objPtr = Tcl_GetObjResult(magicinterp);
    string = Tcl_GetStringFromObj(objPtr, &length);

    if (length > 0)
	if (*(string + length - 1) == '\n')
	    length--;

    if (length == 0)
	return NULL;
    else if (length >= maxChars)
	length = (maxChars - 1);

    strncpy(dest, string, length);
    *(dest + length) = '\0';
    return dest;
}

/*--------------------------------------------------------------*/
/* Parse a file.  This is a skeleton version of the TxDispatch	*/
/* routine in textio/txCommands.c				*/
/*--------------------------------------------------------------*/

void
TxDispatch(f)
    FILE *f;	/* Under Tcl, we never call this with NULL */
{
    if (f == NULL)
    {
	TxError("Error:  TxDispatch(NULL) was called\n");
    }
    while (!feof(f))
    {
	if (SigInterruptPending)
	{
	    TxError("Read-in of file aborted.\n");
	    SigInterruptPending = FALSE;
	    return;
	}
	txGetFileCommand(f, NULL);
    }
}

/*--------------------------------------------------------------*/
/* Send a command line which was collected by magic's TxEvent	*/
/* handler to the interpreter's event queue.			*/
/*--------------------------------------------------------------*/

void
TxParseString(str, q, event)
    char *str;
    caddr_t q;		/* unused */
    caddr_t event;	/* always NULL (ignored) */
{
    char *reply;

    Tcl_EvalEx(magicinterp, str, -1, 0);

    reply = (char *)Tcl_GetStringResult(magicinterp);

    if (strlen(reply) > 0)
	TxPrintf("%s: %s\n", str, reply);
}

/*--------------------------------------------------------------*/
/* Replacement for TxFlush():  use Tcl interpreter		*/
/*    If we just call "flush", _tcl_dispatch gets called, and	*/
/*    bad things will happen.					*/
/*--------------------------------------------------------------*/

void
TxFlushErr()
{
    Tcl_SavedResult state;

    Tcl_SaveResult(magicinterp, &state);
    Tcl_EvalEx(magicinterp, "::tcl_flush stderr", 18, 0);
    Tcl_RestoreResult(magicinterp, &state);
}

/*--------------------------------------------------------------*/

void
TxFlushOut()
{
    Tcl_SavedResult state;

    Tcl_SaveResult(magicinterp, &state);
    Tcl_EvalEx(magicinterp, "::tcl_flush stdout", 18, 0);
    Tcl_RestoreResult(magicinterp, &state);
}

/*--------------------------------------------------------------*/

void
TxFlush()
{
    TxFlushOut();
    TxFlushErr();
}

/*--------------------------------------------------------------*/
/* Tcl_printf() replaces vfprintf() for use by every Tx output	*/
/* function (namely, TxError() for stderr and TxPrintf() for	*/
/* stdout).  It changes the result to a Tcl "puts" call, which	*/
/* can be changed inside Tcl, as, for example, by TkCon.	*/
/*								*/
/* 6/17/04---Routine extended to escape double-dollar-sign '$$'	*/
/* which is used by some tools when generating via cells.	*/
/*								*/
/* 12/23/16---Noted that using consoleinterp simply prevents	*/
/* the output from being redirected to another window such as	*/
/* the command entry window.  Split off another bit TxTkOutput	*/
/* from TxTkConsole and set it to zero by default.  The		*/
/* original behavior can be restored using the *flags wizard	*/
/* command (*flags printf true).				*/
/*--------------------------------------------------------------*/

int
Tcl_printf(FILE *f, char *fmt, va_list args_in)
{
    va_list args;
    static char outstr[128] = "puts -nonewline std";
    char *outptr, *bigstr = NULL, *finalstr = NULL;
    int i, nchars, result, escapes = 0, limit;
    Tcl_Interp *printinterp = (TxTkOutput) ? consoleinterp : magicinterp;

    strcpy (outstr + 19, (f == stderr) ? "err \"" : "out \"");

    va_copy(args, args_in);
    outptr = outstr;
    nchars = vsnprintf(outptr + 24, 102, fmt, args);
    va_end(args);

    if (nchars >= 102)
    {
	va_copy(args, args_in);
	bigstr = Tcl_Alloc(nchars + 26);
	strncpy(bigstr, outptr, 24);
	outptr = bigstr;
	vsnprintf(outptr + 24, nchars + 2, fmt, args);
	va_end(args);
    }
    else if (nchars == -1) nchars = 126;

    for (i = 24; *(outptr + i) != '\0'; i++)
    {
	if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    	*(outptr + i) == ']' || *(outptr + i) == '\\')
	    escapes++;
	else if (*(outptr + i) == '$' && *(outptr + i + 1) == '$')
	    escapes += 2;
    }

    if (escapes > 0)
    {
	/* "+ 4" required to process "$$...$$"; haven't figured out why. */ 
	finalstr = Tcl_Alloc(nchars + escapes + 26 + 4);
	strncpy(finalstr, outptr, 24);
	escapes = 0;
	for (i = 24; *(outptr + i) != '\0'; i++)
	{
	    if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    		*(outptr + i) == ']' || *(outptr + i) == '\\')
	    {
	        *(finalstr + i + escapes) = '\\';
		escapes++;
	    }
	    else if (*(outptr + i) == '$' && *(outptr + i + 1) == '$')
	    {
		*(finalstr + i + escapes) = '\\';
		*(finalstr + i + escapes + 1) = '$';
		*(finalstr + i + escapes + 2) = '\\';
		escapes += 2;
		i++;
	    }
	    *(finalstr + i + escapes) = *(outptr + i);
	}
        outptr = finalstr;
    }

    *(outptr + 24 + nchars + escapes) = '\"';
    *(outptr + 25 + nchars + escapes) = '\0';

    result = Tcl_EvalEx(printinterp, outptr, -1, 0);

    if (bigstr != NULL) Tcl_Free(bigstr);
    if (finalstr != NULL) Tcl_Free(finalstr);

    return result;
}
    
/*--------------------------------------------------------------*/
/* Tcl_escape() takes a string as input and produces a string	*/
/* in which characters are escaped as necessary to make them	*/
/* printable from Tcl.  The new string is allocated by		*/
/* Tcl_Alloc() which needs to be free'd with Tcl_Free().	*/
/*								*/
/* 6/17/04---extended like Tcl_printf to escape double-dollar-	*/
/* sign ('$$') in names.					*/
/*--------------------------------------------------------------*/

char *
Tcl_escape(instring)
    char *instring;
{
    char *newstr;
    int nchars = 0;
    int escapes = 0;
    int i;

    for (i = 0; *(instring + i) != '\0'; i++)
    {
	nchars++;
	if (*(instring + i) == '\"' || *(instring + i) == '[' ||
	    	*(instring + i) == ']')
	    escapes++;

	else if (*(instring + i) == '$' && *(instring + i + 1) == '$')
	    escapes += 2;
    }

    newstr = Tcl_Alloc(nchars + escapes + 1);
    escapes = 0;
    for (i = 0; *(instring + i) != '\0'; i++)
    {
	if (*(instring + i) == '\"' || *(instring + i) == '[' ||
	    		*(instring + i) == ']')
	{
	    *(newstr + i + escapes) = '\\';
	    escapes++;
	}
	else if (*(instring + i) == '$' && *(instring + i + 1) == '$')
	{
	    *(newstr + i + escapes) = '\\';
	    *(newstr + i + escapes + 1) = '$';
	    *(newstr + i + escapes + 2) = '\\';
	    escapes += 2;
	    i++;
	}
	*(newstr + i + escapes) = *(instring + i);
    }
    *(newstr + i + escapes) = '\0';
    return newstr;
}

/*--------------------------------------------------------------*/

int
TerminalInputProc(instanceData, buf, toRead, errorCodePtr)
    ClientData instanceData;
    char *buf;
    int toRead;
    int *errorCodePtr;
{
    FileState *fsPtr = (FileState *)instanceData;
    int bytesRead, i, tlen;
    char *locbuf;

    *errorCodePtr = 0;

    TxInputRedirect = TX_INPUT_NORMAL;
    if (TxBuffer != NULL) {
       tlen = strlen(TxBuffer);
       if (tlen < toRead) {
          strcpy(buf, TxBuffer);
	  Tcl_Free(TxBuffer);
	  TxBuffer = NULL;
	  return tlen;
       }
       else {
	  strncpy(buf, TxBuffer, toRead);
	  locbuf = Tcl_Alloc(tlen - toRead + 1);
	  strcpy(locbuf, TxBuffer + toRead);
	  Tcl_Free(TxBuffer);
	  TxBuffer = locbuf;
	  return toRead;
       }
    }

    while (1) {
	bytesRead = read(fsPtr->fd, buf, (size_t) toRead);
	if (bytesRead > -1)
	    return bytesRead;

	// Ignore interrupts, which may be generated by new
	// terminal windows (added by Tim, 9/30/2014)

	if (errno != EINTR) break;
    }
    *errorCodePtr = errno;
	
    return -1;
}

/*--------------------------------------------------------------*/

int
Tclmagic_Init(interp)
    Tcl_Interp *interp;
{
    char *cadroot;

    /* Sanity check! */
    if (interp == NULL) return TCL_ERROR;

    /* Remember the interpreter */
    magicinterp = interp;

    if (Tcl_InitStubs(interp, "8.1", 0) == NULL) return TCL_ERROR;

    /* Initialization and Startup commands */
    Tcl_CreateCommand(interp, "magic::initialize", (Tcl_CmdProc *)_magic_initialize,
			(ClientData)NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "magic::startup", (Tcl_CmdProc *)_magic_startup,
			(ClientData)NULL, (Tcl_CmdDeleteProc *) NULL);

    /* Initialize the command-tag callback feature */

    HashInit(&txTclTagTable, 10, HT_STRINGKEYS);
    Tcl_CreateCommand(interp, "magic::tag", (Tcl_CmdProc *)AddCommandTag,
			(ClientData)NULL, (Tcl_CmdDeleteProc *) NULL);

    /* Add "*flags" command for manipulating run-time flags */
    Tcl_CreateObjCommand(interp, "magic::*flags", (Tcl_ObjCmdProc *)_magic_flags,
			(ClientData)NULL, (Tcl_CmdDeleteProc *) NULL);

    /* Add the magic TCL directory to the Tcl library search path */

    Tcl_Eval(interp, "lappend auto_path " TCL_DIR );

    /* Set $CAD_ROOT as a Tcl variable */

    cadroot = getenv("CAD_ROOT");
    if (cadroot == NULL) cadroot = CAD_DIR;

    Tcl_SetVar(interp, "CAD_ROOT", cadroot, TCL_GLOBAL_ONLY);

    Tcl_PkgProvide(interp, "Tclmagic", MAGIC_VERSION);
    return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Define a "safe init" function for those platforms that	*/
/* require it.							*/
/*--------------------------------------------------------------*/

int
Tclmagic_SafeInit(interp)
    Tcl_Interp *interp; 
{
    return Tclmagic_Init(interp);
}
