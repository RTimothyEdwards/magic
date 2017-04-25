/*
 * txInput.c --
 *
 * 	Handles 'stdin' and terminal driver settings.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/textio/txInput.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>


#include "utils/magsgtty.h"
#include "utils/magic.h"
#include "utils/magsgtty.h"
#include "utils/main.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "textio/textioInt.h"
#include "utils/dqueue.h"
#include "utils/macros.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "database/databaseInt.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"

#ifdef USE_READLINE
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#include "readline/readline/readline.h"
#include "readline/readline/history.h"
#endif

int    TxPrefix(void);

/* Globally keep track of the imacro string, if any; */
/* the readline callback takes no parameters.	     */
char *_rl_prefix;

/* Custom completer function */
char **magic_completion_function(char *, int, int);

/* match generators */
char  *list_completion_function(char *, int);
char  *macro_completion_function(char *, int);
char  *cellname_completion_function(char *, int);
char  *istyle_completion_function(char *, int);
char  *ostyle_completion_function(char *, int);

/* match list generators */
void   make_techtype_list(void);
void   update_cellname_hash(void);

char **completion_list;
char **magic_command_list  = (char **)NULL;
char **magic_techtype_list = (char **)NULL;
HashTable cellname_hash;

static int interactive_flag;

char *magic_direction_list[] = {
  "bottom", "down", "e", "east", "left", "n", "north", "right",
  "s", "south", "top", "up", "w", "west", (char *)NULL };

#define COMMAND_COMPL 0
#define MACRO_COMPL   1
#define IMACRO_COMPL  2
#define LAYER_COMPL   3
#define PLANE_COMPL   4
#define FILE_COMPL    5
#define CELL_COMPL    6
#define ARGS_COMPL    7
#define ISTYLE_COMPL  8
#define OSTYLE_COMPL  9
#define DIRECT_COMPL  10

/* Can only accommodate 32 arg options, increase as necessary */
static struct cmd_spec {
  char *cmd;
  int   type;
  char *args[32];
} magic_cmd_spec[] = {
  { "", COMMAND_COMPL, {(char *)NULL} },
  { "help", COMMAND_COMPL, {(char *)NULL} },

  { "shell", FILE_COMPL, {(char *)NULL} },
  { "addpath", FILE_COMPL, {(char *)NULL} },
  { "path", FILE_COMPL, {(char *)NULL} },
  { "logcommands", FILE_COMPL, {(char *)NULL} },
  { "startrsim", FILE_COMPL, {(char *)NULL} },
  { "rsim", FILE_COMPL, {(char *)NULL} },
  { "source", FILE_COMPL, {(char *)NULL} },
  { "save", FILE_COMPL, {(char *)NULL} },

  { "getcell", CELL_COMPL, {(char *)NULL} },
  { "child", CELL_COMPL, {(char *)NULL} },
  { "parent", CELL_COMPL, {(char *)NULL} },
  { "dump", CELL_COMPL, {(char *)NULL} },
  { "list", CELL_COMPL, {(char *)NULL} },
  { "load", CELL_COMPL, {(char *)NULL} },
  { "xload", CELL_COMPL, {(char *)NULL} },

  { "macro", MACRO_COMPL, {(char *)NULL} },
  { "imacro", IMACRO_COMPL, {(char *)NULL} },
  { "see*", LAYER_COMPL, {(char *)NULL} },
  { "paint", LAYER_COMPL, {(char *)NULL} },

  { "*extract", ARGS_COMPL, {"clrdebug", "clrlength", "driver", "interactions", 
                             "intercount", "parents", "receiver", "setdebug", 
                             "showdebug", "showparents", "showtech", "stats", 
                             "step", "times", (char *)NULL} },

  { "*garoute", ARGS_COMPL, {"clrdebug", "setdebug", "showdebug", (char *)NULL} },
  { "*groute", ARGS_COMPL, {"clrdebug", "onlynet", "setdebug", "showdebug", 
                            "sides", (char *)NULL} },
  { "*iroute", ARGS_COMPL, {"debug", "help", "parms", (char *)NULL} },
  { "*malloc", ARGS_COMPL, {"all", "off", "on", "only", "watch", "unwatch", (char *)NULL} },
  { "*mzroute", ARGS_COMPL, {"debug", "dumpEstimates", "dumpTags", "help", 
                             "numberLine", "parms", "plane", "version", (char *)NULL} },

  { "*plow", ARGS_COMPL, {"help", "clrdebug", "jogreduce", "lwidth", "lshadow", 
                          "mergedown", "mergeup", "move", "outline", "plow", 
                          "print", "random", "setdebug", "shadow", "showdebug", 
                          "split", "techshow", "trail", "whenbot", "whentop", 
                          "width", (char *)NULL} },
  { "*plow lwidth", LAYER_COMPL, { (char *)NULL } },
  { "*plow width", LAYER_COMPL, { (char *)NULL } },
  { "*plow lshadow", LAYER_COMPL, { (char *)NULL } },
  { "*plow shadow", LAYER_COMPL, { (char *)NULL } },
  { "*plow techshow", FILE_COMPL, { (char *)NULL } },
  { "*plow outline", DIRECT_COMPL, { (char *)NULL } },
  { "*plow plow", DIRECT_COMPL, { (char *)NULL } },

  { "*profile", ARGS_COMPL, {"on", "off", (char *)NULL} },
  { "*watch", PLANE_COMPL, {(char *)NULL} },
  { "*watch*", ARGS_COMPL, {"demo", "types", (char *)NULL} },

  { "calma", ARGS_COMPL, {"help", "flatten", "labels", "lower", "noflatten", 
                          "nolabels", "nolower", "read", "write", (char *)NULL} },
  { "calma read", FILE_COMPL, { (char *)NULL } },
  { "calma write", FILE_COMPL, { (char *)NULL } },

  { "cif", ARGS_COMPL, {"help", "arealabels", "idcell", "istyle", "prefix", 
                        "ostyle", "read", "see", "statistics", "write", "flat", (char *)NULL} },
  { "cif read", FILE_COMPL, { (char *)NULL } },
  { "cif flat", FILE_COMPL, { (char *)NULL } },
  { "cif write", FILE_COMPL, { (char *)NULL } },
  { "cif idcell", ARGS_COMPL, { "yes", "no", (char *)NULL } },
  { "cif arealabels", ARGS_COMPL, { "yes", "no", (char *)NULL } },
  { "cif prefix", FILE_COMPL, { (char *)NULL } },
  { "cif see", LAYER_COMPL, { (char *)NULL } },
  { "cif istyle", ISTYLE_COMPL, { (char *)NULL } },
  { "cif ostyle", OSTYLE_COMPL, { (char *)NULL } },

  { "copy", DIRECT_COMPL, { (char *)NULL } },
  { "move", DIRECT_COMPL, { (char *)NULL } },
  { "stretch", DIRECT_COMPL, { (char *)NULL } },

  { "drc", ARGS_COMPL, {"help", "catchup", "check", "count", "find", "off", 
                        "on", "printrules", "rulestats", "statistics", "why", (char *)NULL} },
  { "drc printrules", FILE_COMPL, { (char *)NULL } },

  { "ext", ARGS_COMPL, {"help", "all", "cell", "do", "length", "no", "parents", 
                        "showparents", "style", "unique", "warn", (char *)NULL} },
  { "ext style", OSTYLE_COMPL, { (char *)NULL } },

  { "feedback", ARGS_COMPL, {"help", "add", "clear", "count", "find", "save", 
                             "why", "fill", (char *)NULL} },
  { "feedback save", FILE_COMPL, { (char *)NULL } },

  { "garoute", ARGS_COMPL, {"help", "channel", "generate", "nowarn", "route", 
                            "reset", "warn", (char *)NULL} },
  { "getnode", ARGS_COMPL, {"alias", "fast", "abort", (char *)NULL} },

  { "iroute", ARGS_COMPL, {"help", "contacts", "layers", "route", 
                           "saveParameters", "search", "spacings", "verbosity", 
                           "version", "wizard", (char *)NULL} },
  { "iroute help", ARGS_COMPL, {"contacts", "layers", "route", 
                           "saveParameters", "search", "spacings", "verbosity", 
                           "version", "wizard", (char *)NULL} },

  { "plot", ARGS_COMPL, {"help", "postscript", "gremlin", "versatec", "pnm", 
                         "parameters", (char *)NULL} },
  { "plot postscript", FILE_COMPL, { (char *)NULL } },
  { "plot gremlin", FILE_COMPL, { (char *)NULL } },
  { "plot versatec", FILE_COMPL, { (char *)NULL } },
  { "plot pnm", FILE_COMPL, { (char *)NULL } },

  { "plow", ARGS_COMPL, {"help", "boundary", "horizon", "jogs", "selection", 
                         "straighten", "noboundary", "nojogs", "nostraighten", (char *)NULL} },
  { "plow selection", DIRECT_COMPL, { (char *)NULL } },

  { "route", ARGS_COMPL, {"help", "end", "jog", "metal", "netlist", "obstacle", 
                          "origin", "stats", "settings", "steady", "tech", 
                          "vias", "viamin", (char *)NULL} },
  { "route netlist", FILE_COMPL, { (char *)NULL } },

  { "select", ARGS_COMPL, {"help", "more", "less", "area", "visible", "cell", 
                           "clear", "save", "box", (char *)NULL} },
  { "select save", FILE_COMPL, { (char *)NULL } },
  { "select more", ARGS_COMPL, { "area", "visible", "cell", "box", (char *)NULL } },
  { "select less", ARGS_COMPL, { "area", "visible", "cell", "box", (char *)NULL } },
  { "select more area", LAYER_COMPL, { (char *)NULL } },
  { "select less area", LAYER_COMPL, { (char *)NULL } },
  { "select more visible", LAYER_COMPL, { (char *)NULL } },
  { "select less visible", LAYER_COMPL, { (char *)NULL } },
  { "select more cell", CELL_COMPL, { (char *)NULL } },
  { "select less cell", CELL_COMPL, { (char *)NULL } },
  { "select more box", LAYER_COMPL, { (char *)NULL } },
  { "select less box", LAYER_COMPL, { (char *)NULL } },

  { "send", ARGS_COMPL, {"netlist", "color", "layout", 
#ifdef THREE_D
	"wind3d",
#endif
		 (char *)NULL} },
  { "snap", ARGS_COMPL, {"on", "off", (char *)NULL} },
  { "tool", ARGS_COMPL, {"box", "wiring", "netlist", "rsim", (char *)NULL} },
  { "wire", ARGS_COMPL, {"help", "horizontal", "leg", "switch", "type", 
                         "vertical", (char *)NULL} },
  { "wire type", LAYER_COMPL, { (char *)NULL } },
  { "wire switch", LAYER_COMPL, { (char *)NULL } },

#ifdef USE_READLINE
  { "history", ARGS_COMPL, {"n", "r", "help", (char *)NULL} },
#endif
  { (char *)NULL , 0, {(char *)NULL} }
};
#endif


/* Characters for input processing.  Set to -1 if not defined */

char *TxGetLinePfix();

char txEraseChar = -1;		/* Erase line (e.g. ^H) */
char txKillChar = -1;		/* Kill line (e.g. ^U or ^X) */
char txWordChar = -1;		/* Erase a word (e.g. ^W) */
char txReprintChar = -1;	/* Reprint the line (e.g. ^R */
char txLitChar = -1;		/* Literal next character (e.g. ^V */
char txBreakChar = -1;		/* Break to a new line (normally -1) */
char TxEOFChar = -1;		/* The current EOF character (e.g. ^D) */
char TxInterruptChar = -1;	/* The current interrupt character (e.g. ^C) */

static char txPromptChar;	/* the current prompt */
bool txHavePrompt = FALSE;	/* is a prompt on the screen? */

char *txReprint1 = NULL;
char *txReprint2 = NULL;

/*
 * ----------------------------------------------------------------------------
 *
 * TxReprint --
 *
 *	Reprint the current input line.  Used for ^R and after ^Z.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
TxReprint()
{
    (void) txFprintfBasic(stdout, "\n");
    if (txReprint1 != NULL) (void) txFprintfBasic(stdout, "%s", txReprint1);
    if (txReprint2 != NULL) (void) txFprintfBasic(stdout, "%s", txReprint2);
    (void) fflush(stdout);
}

#ifndef MAGIC_WRAPPER

/*
 * ----------------------------------------------------------------------------
 * TxDialog --
 *
 *	Interactive input with a prompt and a fixed set of responses
 *
 * Results:
 *	Returns the index of the response chosen.  If no response is given,
 *      returns the default value.
 *
 * Side effects:
 *	Terminal input and output (stdout, stdin).
 *
 * ----------------------------------------------------------------------------
 */

int
TxDialog(prompt, responses, deflt)
    char *prompt;
    char *(responses[]);
    int deflt;
{
    int code;
    int maxresp;
    char ans[100];

    /* Find size of valid response set */
    for (maxresp = 0; responses[maxresp] != NULL; maxresp++);

    /* Get input until something matches one of the responses */
    do
    {
	TxPrintf("%s ", prompt);
	if ((deflt >= 0) && (deflt < maxresp))
	    TxPrintf("[%s] ", responses[deflt]);
	TxFlushOut();

	if (TxGetLine(ans, sizeof ans) == NULL || (ans[0] == '\0'))
	{
	    code = deflt;
	    break;
	}
    } while ((code = Lookup(ans, responses)) < 0);

    return code;
}


/*
 * ----------------------------------------------------------------------------
 * TxSetPrompt --
 *
 *	Set the current prompt.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	You can guess.
 * ----------------------------------------------------------------------------
 */

void
TxSetPrompt(ch)
    char ch;
{
    txPromptChar = ch;
}

#endif


/*
 * ----------------------------------------------------------------------------
 * TxPrompt --
 *
 *	Put up the prompt which was set by TxSetPrompt.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	You can guess.
 * ----------------------------------------------------------------------------
 */

void
TxPrompt()
{
    static char lastPromptChar;
    static char prompts[2];

    if (txHavePrompt && (lastPromptChar == txPromptChar)) return;

    (void) fflush(stderr);
    if (txHavePrompt) TxUnPrompt();

    prompts[0] = txPromptChar;
    prompts[1] = '\0';

    txReprint1 = prompts;
    if (TxInteractive) txFprintfBasic(stdout, "%s", txReprint1);
    (void) fflush(stdout);
    txHavePrompt = TRUE;
    lastPromptChar = txPromptChar;
}


/*
 * ----------------------------------------------------------------------------
 * TxRestorePrompt --
 *
 *	The prompt was erased for some reason.  Restore it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	You can guess.
 * ----------------------------------------------------------------------------
 */

void
TxRestorePrompt()
{
    if (txHavePrompt)
    {
	txHavePrompt = FALSE;
	TxPrompt();
    }
}


/*
 * ----------------------------------------------------------------------------
 * TxUnPrompt --
 *
 *	Erase the prompt.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	You can guess.
 * ----------------------------------------------------------------------------
 */


void
TxUnPrompt()
{
    int i, tlen;

    if (txHavePrompt)
    {
	(void) fflush(stderr);
	if (TxInteractive)
	{
	    tlen = strlen(txReprint1);
	    for (i = 0; i < tlen; i++)
		fputc('\b', stdout);
	    for (i = 0; i < tlen; i++)
		fputc(' ', stdout);
	    for (i = 0; i < tlen; i++)
		fputc('\b', stdout);
	}
	(void) fflush(stdout);
	txReprint1 = NULL;
	txHavePrompt = FALSE;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * TxGetChar --
 *
 *	Get a single character from the input stream (terminal or whatever).
 *
 * Results:
 *	One character, or EOF.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
TxGetChar()
{
    int ch;
    extern DQueue txInputEvents, txFreeEvents;
    extern TxInputEvent txLastEvent;
    TxInputEvent *event;
    while (TRUE) {
	/* Get an input character.  Don't let TxGetInputEvent return
	 * because of a SigWinch or whatever.  It would be nice to process
	 * SigWinches in a middle of text input, but that is difficult
	 * to do since SigWinches are processed in the normal
	 * command-interpretation process.  Thus, we delay the processing
	 * until after the text is collected.
	 */
	if (DQIsEmpty(&txInputEvents)) TxGetInputEvent(TRUE, FALSE);
	event = (TxInputEvent *) DQPopFront(&txInputEvents);
	ASSERT(event != NULL, "TxGetChar");
	txLastEvent = *event;
	if (event->txe_button == TX_EOF) {
	    ch = EOF; 
	    goto gotone;
	}
	if (event->txe_button == TX_CHARACTER) {
	    ch = TranslateChar(event->txe_ch);
	    goto gotone;
	}
	DQPushRear(&txFreeEvents, (ClientData) event);
    }

gotone:
    DQPushRear(&txFreeEvents, (ClientData) event);
    return ch;
}


#ifdef USE_READLINE
#undef free
/*
 * ----------------------------------------------------------------------------
 * TxPrefix --
 *	Instantiation of the readline function *rl_pre_input_hook(), executed
 *	between the prompt and the input (readline v4.1).  Prepends the text
 *	of an interactive macro (if non-NULL) to the command line.
 * 
 * ----------------------------------------------------------------------------
 */
int
TxPrefix(void)
{
    if (_rl_prefix != NULL)
	rl_insert_text(_rl_prefix);
    rl_redisplay();
}

/*
 * ----------------------------------------------------------------------------
 *
 * magic_completion_function --
 *
 *	Command-name completion function for use with "readline" package.
 *
 * ----------------------------------------------------------------------------
 */

char **
magic_completion_function(char *text, int start, int end)
{
    CPFunction *completion_func = (CPFunction *)NULL;
    char **matches, **tokens;
    char *line = strdup(rl_line_buffer);
    int i;

    for (i = start; i >= 0; i--)
	if( isspace(line[i]) || line[i] == ';' )
	    break;
    line[i+1] = '\0';

    if ((tokens = history_tokenize(line)) != NULL )
    {
	int entry, start_token, num_tokens;

	num_tokens = 0;
	while (tokens[num_tokens] != (char *)NULL )
	    num_tokens++;

	for (start_token = num_tokens - 1; start_token >= 0; start_token--)
	    if (tokens[start_token][0] == ';')
		break;

	start_token++;

	line[0] = '\0';
	for (i = start_token; i < num_tokens; i++)
	{
	    strcat(line, tokens[i]);
	    if (i < (num_tokens - 1))
		strcat(line, " ");
	}

	entry = LookupStructFull(line, (char **)&magic_cmd_spec,
		sizeof(struct cmd_spec));

	if (entry == -1 && num_tokens >= 1)
	{
	    sprintf(line, "%s*", tokens[0]);
	    entry = LookupStructFull(line, (char **)&magic_cmd_spec,
			sizeof(struct cmd_spec));
	}

	if (entry >= 0)
	{
	    switch (magic_cmd_spec[entry].type)
	    {
		case COMMAND_COMPL:
		    completion_func = list_completion_function;
		    completion_list = magic_command_list;
		    break;
		case MACRO_COMPL:
		    completion_func = macro_completion_function;
		    interactive_flag = 0;
		    break;
		case IMACRO_COMPL:
		    completion_func = macro_completion_function;
		    interactive_flag = 1;
		    break;
		case LAYER_COMPL:
		    completion_func = list_completion_function;
		    if (magic_techtype_list == (char **)NULL)
			make_techtype_list();
		    completion_list = magic_techtype_list;
		    break;
		case PLANE_COMPL:
		    completion_func = list_completion_function;
		    completion_list = DBPlaneLongNameTbl;
		    break;
		case FILE_COMPL:
		    completion_func = (text[0] == '~') ?
				rl_username_completion_function :
                                rl_filename_completion_function;
		    break;
		case CELL_COMPL:
		    update_cellname_hash();
		    completion_func = cellname_completion_function;
		    break;
		case ARGS_COMPL:
		    completion_func = list_completion_function;
		    completion_list = magic_cmd_spec[entry].args;
		    break;
		case ISTYLE_COMPL:
		    completion_func = istyle_completion_function;
		    break;
		case OSTYLE_COMPL:
		    completion_func = ostyle_completion_function;
		    break;
		case DIRECT_COMPL:
		    completion_func = list_completion_function;
		    completion_list = magic_direction_list;
		    break;
	    }
	}
    
	for (i = 0; i < num_tokens; i++)
	    free(tokens[i]);

	free(tokens);

    }
    else
    {
	completion_func = list_completion_function;
	completion_list = magic_command_list;
    }

    free(line);

    matches = (completion_func) ? rl_completion_matches(text, completion_func)
		: (char **)NULL;

    /* If we match nothing, inhibit any matching, except when matching files */

   rl_attempted_completion_over = (matches == (char **)NULL &&
                                  completion_func != rl_username_completion_function &&
                                  completion_func != rl_filename_completion_function);
   return matches;
}

/*
 * ----------------------------------------------------------------------------
 *
 * update_cellname_hash --
 *
 *	Update the hash table of cell names so it can be used by the
 *	cellname completion function.
 *
 * ----------------------------------------------------------------------------
 */
void
update_cellname_hash(void)
{
    extern char *nextName(char **, char *, char *, int);
    extern char *Path;
    char *path    = Path;
    char *dirname = strdup(Path);

    while (nextName(&path, "", dirname, strlen(Path)))
    {
	DIR *dir;

	if (strlen(dirname) == 0)
	    strcpy(dirname, ".");

	dir = opendir(dirname);

	if (dir != NULL)
	{
	    struct dirent *dirent;
	    while ((dirent = readdir(dir)) != (struct dirent *)NULL)
	    {
		char *base;
		if ((base = (char *)strstr(dirent->d_name, DBSuffix))
			!= (char *)NULL && base[strlen(DBSuffix)] == '\0')
		{
		    *base = '\0';
		    if (strlen(dirent->d_name) > 0)
			HashFind(&cellname_hash, dirent->d_name);
		}
	    }
	    closedir(dir);
	}
    }
    free(dirname);
}

/*
 * ----------------------------------------------------------------------------
 *
 * istyle_completion_function --
 *
 *	CIF input style completion function for use with "readline" package.
 *
 * ----------------------------------------------------------------------------
 */
char *
istyle_completion_function(char *text, int state)
{
    extern CIFReadKeep *cifReadStyleList;
    static CIFReadKeep *style;
    static int len;

    if (state == 0)
    {
	style = cifReadStyleList;
	len = strlen(text);
    }

    for (; style != NULL; style = style->crs_next)
    {
	if (strncmp(style->crs_name, text, len) == 0)
	{
	    char *name = style->crs_name;
	    style = style->crs_next;
	    return strdup(name);
	}
    }
    return (char *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ostyle_completion_function --
 *
 *	CIF output style completion function for use with "readline" package.
 *
 * ----------------------------------------------------------------------------
 */
char *
ostyle_completion_function(char *text, int state)
{
    extern CIFKeep *CIFStyleList;
    static CIFKeep *style;
    static int len;

    if (state == 0)
    {
	style = CIFStyleList;
	len = strlen(text);
    }

    for (; style != NULL; style = style->cs_next)
    {
	if (strncmp(style->cs_name, text, len) == 0)
	{
	    char *name = style->cs_name;
	    style = style->cs_next;
	    return strdup(name);
	}
    }
    return (char *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cellname_completion_function --
 *
  name completion function for use with "readline" package.
 *
 * ----------------------------------------------------------------------------
 */
char *
cellname_completion_function(char *text, int state)
{
    extern HashTable dbCellDefTable;
    static int            len;
    static HashSearch     hs, db_hs;
    HashEntry            *he;

    if (state == 0)
    {
	len = strlen(text);
	HashStartSearch(&hs);
	HashStartSearch(&db_hs);
    }

    while ((he = HashNext(&cellname_hash, &hs)) != (HashEntry *)NULL )
    {
	if (strncmp(he->h_key.h_name, text, len) == 0)
	    return strdup(he->h_key.h_name);
    }

    /* Also complete cells that are in main memory only */

    while ((he = HashNext(&dbCellDefTable, &db_hs)) != (HashEntry *)NULL)
    {
	CellDef *cd = (CellDef *)HashGetValue(he);
	if (cd && !(cd->cd_flags & CDINTERNAL)
		&& strcmp(cd->cd_name, UNNAMED) != 0
		&& strncmp(cd->cd_name, text, len) == 0)
	    return strdup(cd->cd_name);
    }
    return (char *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * macro_completion_function --
 *
 *	Macro completion function for use with "readline" package.
 *
 * ----------------------------------------------------------------------------
 */
char *
macro_completion_function(char *text, int state)
{
    extern HashTable MacroClients;
    static HashSearch hs, mc_hs;
    static int len;
    macrodef *macro;
    HashEntry	*he;

    if (state == 0)
    {
	len = strlen(text);
	HashStartSearch(&hs);
    }

    while ((he = HashNext(&MacroClients, &hs)) != (HashEntry *)NULL )
    {
	HashTable *ht = (HashTable *)HashGetValue(he);
	HashEntry *hm;

	if (state == 0)
	    HashStartSearch(&mc_hs);

	while ((hm = HashNext(ht, &mc_hs)) != (HashEntry *)NULL )
	{
	    macro = (macrodef *)HashGetValue(hm);
	    if ((macro->interactive == TRUE && interactive_flag == 1) ||
        		(macro->interactive == FALSE && interactive_flag == 0))
	    {
		char *macro_name = MacroName(macro->macrotext);
		if (strncmp(macro_name, text, len) == 0)
		    return strdup(macro_name);
	    }
	}
    }
    return (char *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * list_completion_function --
 *
 *	List completion function for use with "readline" package.
 *
 * ----------------------------------------------------------------------------
 */
char *
list_completion_function(char *text, int state)
{
    static int list_index, len;
    char *match;

    /* If this is a new word to complete, initialize now.  This includes
     * saving the length of TEXT for efficiency, and initializing the index
     * variable to 0.
     */

    if (state == 0)
    {
	list_index = 0;
	len = strlen(text);
    }

    /* Return the next name which partially matches from the command list. */

    while ((match = completion_list[list_index]) != (char *)NULL)
    {
	list_index++;

	if (strncmp(match, text, len) == 0)
	    return strdup(match);
    }
    return (char *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * make_techtype_list --
 *
 *	Generate list of predefined types for use with completion function
 *
 * ----------------------------------------------------------------------------
 */
void
make_techtype_list(void)
{
    int i, techdep_layers = DBNumUserLayers - TT_TECHDEPBASE;

    magic_techtype_list = (char **)mallocMagic(sizeof(char *)
		* (techdep_layers + 8 + 2 + 1));

    for (i = 0; i < techdep_layers; i++)
	magic_techtype_list[i] = DBTypeLongNameTbl[i+TT_TECHDEPBASE];

    magic_techtype_list[i++] = (char *)"magnet";
    magic_techtype_list[i++] = (char *)"fence";
    magic_techtype_list[i++] = (char *)"rotate";
    magic_techtype_list[i++] = (char *)"subcircuit";
    magic_techtype_list[i++] = (char *)"$";
    magic_techtype_list[i++] = (char *)"*";
    magic_techtype_list[i++] = (char *)"errors";
    magic_techtype_list[i++] = (char *)"labels";
    magic_techtype_list[i++] = (char *)"subcell";

    magic_techtype_list[i++] = (char *)"no";
    magic_techtype_list[i++] = (char *)"allSame";

    magic_techtype_list[i]   = (char *)NULL;
}

#define free You_should_use_the_Magic_procedure_freeMagic
#endif

/*
 * ----------------------------------------------------------------------------
 *
 * TxGetLineWPrompt --
 *
 *	Just like the following TxGetLine proc, but it prompts first.  It
 *	is an advantage to have this proc put out the prompt, since that
 *	way if the user suspends Magic and then continues it the prompt will
 *	reappear in the proper fashion.
 *
 * Results:
 *	A char pointer or NULL (see TxGetLine).
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
TxGetLineWPrompt(dest, maxChars, prompt, prefix)
    char *dest;
    int maxChars;
    char *prompt;
    char *prefix;
{
    char *res, *hist_res, *tmp;
    int return_nothing = 0;

    if (txHavePrompt) TxUnPrompt();
#ifndef USE_READLINE
    /* without GNU readline */

    if (prompt != NULL) TxPrintf("%s", prompt);
    txReprint1 = prompt;

    res = TxGetLinePfix(&dest[0], maxChars, prefix);
    txReprint1 = NULL;
#else
    _rl_prefix = prefix;
    TxResetTerminal ();

    if (prompt != NULL) {
      res = readline (prompt);
    } else {
      res = readline (TX_CMD_PROMPT);
    }

    hist_res = (char *)NULL;

    if (res && *res && !StrIsWhite(res, FALSE) ) {
      /* The history_expand() function from Readline can return 4 values: */
      /* -1 - There was an error in expansion. Command not added to history. */
      /*      This is the same behavior as tcsh. */
      /*  0 - No expansion possible. Just a normal command. Add command to history, */
      /*      return and execute. */
      /*  1 - Expansion took place. Assign $command to expansion and proceed. */
      /*  2 - Expansion took place (eg. :p modifier was used). Display exspansion */
      /*      but don't execute. Expanded command implicitly added to history by */
      /*      history_expand(). */
      switch( history_expand(res, &hist_res) ) {
        case -1:
          TxError("%s\n", hist_res);
          return_nothing = 1;
          break;
        case 0:
          add_history(res);
          break;
        case 1:
          tmp = res;
          res = hist_res;
          hist_res = tmp;
          add_history(res);
          break;
        case 2:
          TxPrintf("%s\n", hist_res);
          return_nothing = 1;
          break;
      }
    }

    if (res) {
      if (strlen (res) >= maxChars) {
        TxPrintf ("WARNING: input string too long; truncated");
        res[maxChars-1] = '\0';
      }
      if( return_nothing ) {
        dest[0] = '\0';
      } else {
        strcpy (dest, res);
      }
#undef free
      free (res);
      if( hist_res != (char *)NULL ) free(hist_res);
#define free You_should_use_the_Magic_procedure_freeMagic
      res = dest;
    }
    TxSetTerminal ();
#endif
    return res;
}

/*
 * ----------------------------------------------------------------------------
 */

char *
TxGetLinePrompt(dest, maxChars, prompt)
    char *dest;
    int maxChars;
    char *prompt;
{
    return TxGetLineWPrompt(dest, maxChars, prompt, NULL);
}

#ifndef MAGIC_WRAPPER


/*
 * ----------------------------------------------------------------------------
 * TxGetLinePfix:
 *
 * 	Reads a line from the input queue (terminal or whatever).
 *
 * Results:
 *	A char pointer to the string is returned.
 *	If an end-of-file is typed, returns (char *) NULL and 
 *	stores in the string any characters recieved up to that point.
 *
 * Side effects:
 *	The input stream is read, and 'dest' is filled in with up to maxChars-1
 *	characters.  Up to maxChars of the 'dest' may be changed during the
 *	input process, however, since a '\0' is added at the end.  There is no 
 *	newline at the end of 'dest'.
 * ----------------------------------------------------------------------------
 */

char *
TxGetLinePfix(dest, maxChars, prefix)
    char *dest;
    int maxChars;
    char *prefix;
{
    int i;
    char *ret;
    int ch;
    bool literal;

    if (maxChars < 1) return dest;
    if (txHavePrompt) TxUnPrompt();
    ret = dest;
    dest[0] = '\0';
    txReprint2 = dest;

#define TX_ERASE()	( (i>0) ?(i--, TxPrintf("\b \b"), 0) :0)

    (void) fflush(stderr);
    literal = FALSE;
    i = 0;
    if (prefix != NULL)
    {
	while (prefix[i] != '\0') {
	    if (i >= maxChars - 1) break;
	    dest[i] = prefix[i];
	    txFprintfBasic(stdout, "%c", prefix[i]);
	    i++;
	}
    }
    while (TRUE) {
	dest[i] = '\0';
	if (i >= maxChars - 1) break;
	(void) fflush(stdout);
	ch = TxGetChar();
	if (ch == EOF || ch == -1 || ch == TxEOFChar) {
	    TxError("\nEOF encountered on input stream.\n");
	    ret = NULL;
	    break;
	} else if (literal) {
	    literal = FALSE;
	    dest[i] = ch;
	    txFprintfBasic(stdout, "%c", ch);
	    i++;
	} else if (ch == '\n' || ch == txBreakChar) {
	    break;
	} else if (ch == '\t') {
	    while (TRUE) {
		dest[i] = ' ';
		txFprintfBasic(stdout, " ");
		i++;
		if (((i + strlen(txReprint1)) % 8) == 0) break;
	    }
	} else if (ch == txEraseChar) {
	    TX_ERASE();
	} else if (ch == txKillChar) {
	    while (i > 0) TX_ERASE();
	} else if (ch == txWordChar) {
	    while (i > 0 && isspace(dest[i-1])) TX_ERASE();
	    while (i > 0 && !isspace(dest[i-1])) TX_ERASE();
	} else if (ch == txReprintChar) {
	    TxReprint();
	} else if (ch == txLitChar) {
	    literal = TRUE;
	} else {
	    dest[i] = ch;
	    txFprintfBasic(stdout, "%c", ch);
	    i++;
	}
    }
    txFprintfBasic(stdout, "\n");
    txHavePrompt = FALSE;
    txReprint2 = NULL;

    return ret;
}

#endif

/*
 * ----------------------------------------------------------------------------
 * TxGetLine:
 */

char *
TxGetLine(dest, maxChars)
    char *dest;
    int maxChars;
{
    return TxGetLinePfix(dest, maxChars, NULL);
}


/*
 * ----------------------------------------------------------------------------
 * txGetTermState:
 *
 *	Read the state of the terminal and driver.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	The passed structure is filled in.
 * ----------------------------------------------------------------------------
 */

#if defined(SYSV) || defined(CYGWIN)

void
txGetTermState(buf)
    struct termio *buf;

{
    ioctl( fileno( stdin ), TCGETA, buf);
}

#else

void
txGetTermState(buf)
    txTermState *buf;
{
    ASSERT(TxStdinIsatty, "txGetTermState");
    /* save the current terminal characteristics */
    (void) ioctl(fileno(stdin), TIOCGETP, (char *) &(buf->tx_i_sgtty) );
    (void) ioctl(fileno(stdin), TIOCGETC, (char *) &(buf->tx_i_tchars) );
}
#endif /* SYSV */


/*
 * ----------------------------------------------------------------------------
 * txSetTermState:
 *
 *	Set the state of the terminal and driver.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	The terminal driver is set up.
 * ----------------------------------------------------------------------------
 */

void
txSetTermState(buf)
#if defined(SYSV) || defined(CYGWIN)
    struct termio *buf;
#else
    txTermState *buf;
#endif /* SYSV */
{
#if defined(SYSV) || defined(CYGWIN)
    ioctl( fileno(stdin), TCSETAF, buf );
#else
    /* set the current terminal characteristics */
    (void) ioctl(fileno(stdin), TIOCSETN, (char *) &(buf->tx_i_sgtty) );
    (void) ioctl(fileno(stdin), TIOCSETC, (char *) &(buf->tx_i_tchars) );
#endif /* SYSV */
}



/*
 * ----------------------------------------------------------------------------
 * txInitTermRec:
 *
 * 	Sets the terminal record that it has echo turned off,
 *	cbreak on, and no EOFs the way we like it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The passed terminal record is modified.
 *	No terminal modes are actually changed.
 * ----------------------------------------------------------------------------
 */

void
txInitTermRec(buf)
#if defined(SYSV) || defined(CYGWIN)
    struct termio *buf;
#else
    txTermState *buf;
#endif /* SYSV */
{
#if defined(SYSV) || defined(CYGWIN)
    buf->c_lflag = ISIG;    /* raw: no echo and no processing, allow signals */
    buf->c_cc[ VMIN ] = 1;
    buf->c_cc[ VTIME ] = 0;
#else
    /* set things up for us, turn off echo, turn on cbreak, no EOF */
    buf->tx_i_sgtty.sg_flags |= CBREAK;
    buf->tx_i_sgtty.sg_flags &= ~ECHO;
    buf->tx_i_tchars.t_eofc = -1;

#endif /* SYSV */
}



#if defined(SYSV) || defined(CYGWIN)
struct termio closeTermState;
#else
static txTermState closeTermState;
#endif /* SYSV */

static bool haveCloseState = FALSE;

/*
 * ----------------------------------------------------------------------------
 * txSaveTerm:
 *
 * 	Save the terminal characteristics so they can be restored when
 *	magic leaves.
 *	
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	a global variable is set.
 * ----------------------------------------------------------------------------
 */

void
txSaveTerm()
{
#if defined(SYSV) || defined(CYGWIN)
    ioctl( fileno( stdin ), TCGETA, &closeTermState);
    txEraseChar = closeTermState.c_cc[VERASE];
    txKillChar =  closeTermState.c_cc[VKILL];
    TxEOFChar = closeTermState.c_cc[VEOF];
    TxInterruptChar = closeTermState.c_cc[VINTR];
    haveCloseState = TRUE;
#else
    struct ltchars lt;
    txGetTermState(&closeTermState);
    (void) ioctl(fileno(stdin), TIOCGLTC, (char *) &lt);
    txEraseChar = closeTermState.tx_i_sgtty.sg_erase;
    txKillChar = closeTermState.tx_i_sgtty.sg_kill;
    txWordChar = lt.t_werasc;
    txReprintChar = lt.t_rprntc;
    txLitChar = lt.t_lnextc;
    txBreakChar = closeTermState.tx_i_tchars.t_brkc;
    TxEOFChar = closeTermState.tx_i_tchars.t_eofc;
    TxInterruptChar = closeTermState.tx_i_tchars.t_intrc;
    haveCloseState = TRUE;
#endif /* SYSV */
}


/*
 * ----------------------------------------------------------------------------
 * TxSetTerminal:
 *
 * 	Sets the terminal up the way we like it.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	The terminal modes are changed.
 * ----------------------------------------------------------------------------
 */

void
TxSetTerminal()
{
#if defined(SYSV) || defined(CYGWIN)
    struct termio buf;
#else
    txTermState buf;
#endif /* SYSV */

#ifdef MAGIC_WRAPPER
    /* If using Tk console, don't mess with the terminal settings;	  */
    /* Otherwise, this prevents running magic in the terminal background. */
    if (TxTkConsole) return;
#endif

    if (TxStdinIsatty)
    {
	if (!haveCloseState) txSaveTerm();
	buf = closeTermState;
	txInitTermRec(&buf);
	txSetTermState(&buf);
    }
}


/*
 * ----------------------------------------------------------------------------
 * TxResetTerminal:
 *
 * 	Returns the terminal to the way it was when Magic started up.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	The terminal modes are changed.
 * ----------------------------------------------------------------------------
 */

void
TxResetTerminal()
{

#ifdef MAGIC_WRAPPER
    /* If using Tk console, don't mess with the terminal settings;	  */
    /* Otherwise, this prevents running magic in the terminal background. */
    if (TxTkConsole) return;
#endif

    if (TxStdinIsatty && haveCloseState) txSetTermState(&closeTermState);
}
