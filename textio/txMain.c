/*
 * txMain.c --
 *
 * 	This module handles output to the text terminal as well as
 *	collecting input and sending the commands to the window package.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/textio/txMain.c,v 1.2 2010/03/08 13:33:34 tim Exp $";
#endif  /* not lint */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "utils/magsgtty.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "textio/textioInt.h"
#include "windows/windows.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwind.h"

/* Global variables that indicate if we are reading or writing to a tty.
 */
global bool TxStdinIsatty;
global bool TxStdoutIsatty;

#ifdef USE_READLINE
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#include "readline/readline.h"
#include "readline/history.h"
#endif

int TxPrefix(void);
char **magic_completion_function(char *, int, int);
extern HashTable cellname_hash;

/* The readline completion function requires a command list containing	*/
/* just the command name (without the accompanying help text line)	*/
extern char **magic_command_list;

#endif


/*
 * ----------------------------------------------------------------------------
 * TxInit:
 *
 *	Initialize this module.
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	misc.
 * ----------------------------------------------------------------------------
 */ 

void
TxInit()
{
    static char sebuf[BUFSIZ];

    setbuf(stderr, sebuf);
    setbuf(stdin, (char *) NULL);  /* required for LPENDIN in textio to work */
    TxStdinIsatty = (isatty(fileno(stdin)));

#ifdef MAGIC_WRAPPER
    TxStdoutIsatty = 0;	/* Tx loop is non-interactive */
#else
    TxStdoutIsatty = (isatty(fileno(stdout)));
#endif

    txCommandsInit();
}

#ifdef USE_READLINE

void
TxInitReadline()
{
    int i, j;
    char **commandTable;
    char nobell[] = "set bell-style none";

    rl_getc_function = TxGetChar;
    rl_pre_input_hook = TxPrefix;
    rl_readline_name = "magic";

    /* the default behavior is for no terminal bell to ever ring */
    rl_parse_and_bind(nobell);

    /* read ~/.inputrc (or whatever INPUTRC is set to) to allow users to override */
    rl_read_init_file(NULL);

    /* removed "=" and "(" because styles contain them */
    rl_completer_word_break_characters = " \t\n\"\\'`@$><;|&{";

    rl_attempted_completion_function = (CPPFunction *)magic_completion_function;
    HashInit(&cellname_hash, 128, HT_STRINGKEYS);

    i = j = 0;
    commandTable = WindGetCommandTable(DBWclientID);
    while(commandTable[i++] != (char *)NULL ) {
      j++;
    }
    i = 0;
    commandTable = WindGetCommandTable(windClientID);
    while(commandTable[i++] != (char *)NULL ) {
      j++;
    }

    magic_command_list = (char **)mallocMagic(sizeof(char *) * (j + 1));

    i = j = 0;
    commandTable = WindGetCommandTable(DBWclientID);
    while( commandTable[i] != (char *)NULL ) {
      int k = 0;
      while( !isspace(commandTable[i][k]) && (commandTable[i][k] != '\0') ) {
        k++;
      }
      if( k > 0 ) {
        magic_command_list[j] = (char *)mallocMagic((k+1)*sizeof(char));
        strncpy(magic_command_list[j], commandTable[i], k);
        magic_command_list[j][k] = '\0';
        j++;
      }
      i++;
    }
    i = 0;
    commandTable = WindGetCommandTable(windClientID);
    while( commandTable[i] != (char *)NULL ) {
      int k = 0;
      while( !isspace(commandTable[i][k]) && (commandTable[i][k] != '\0') ) {
        k++;
      }
      if( k > 0 ) {
        magic_command_list[j] = (char *)mallocMagic((k+1)*sizeof(char));
        strncpy(magic_command_list[j], commandTable[i], k);
        magic_command_list[j][k] = '\0';
        j++;
      }
      i++;
    }
    magic_command_list[j] = (char *)NULL;
    rl_completion_query_items = MAX(j+1, 250);
}

#endif

