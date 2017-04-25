/*
 * lefCmd.c --
 *
 * Commands for the LEF module only.
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/lef/lefCmd.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "textio/txcommands.h"
#include "commands/commands.h"


/*
 * ----------------------------------------------------------------------------
 *
 * CmdLef --
 *
 * Implement the "lef" and "def" commands:  generate LEF-format output
 * for a layout, or read LEF- or DEF-format files.
 *
 * Usage:
 *	lef [options]
 *	def [options]
 *
 * Results:
 *	Always return 0.
 *
 * Side effects:
 *	Generates LEF-format output on disk somewhere, or reads
 *	LEF- or DEF-format files, generating cell definitions and
 *	uses in the process.
 *
 * ----------------------------------------------------------------------------
 */

/* These definitions must correspond to the ordering in cmdLefOption[] below */

#define LEF_READ		0
#define LEF_WRITE		1
#define LEF_WRITEALL		2
#define LEF_HELP		3

void
CmdLef(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int option;
    char **msg, *namep;
    CellUse *selectedUse;
    CellDef *selectedDef;
    bool is_lef;
    bool lefImport = FALSE;		/* Indicates whether or not we
					 * read cells from .mag files
					 * when the FOREIGN statement
					 * is encountered in a macro.
					 */
    bool lefTopCell = TRUE;		/* Indicates whether or not we
					 * write the top-level cell to
					 * LEF, or just the subcells.
					 */
    bool allSpecial = FALSE;		/* Indicates whether we should
					 * treat all geometry as "special"
					 * nets in DEF format output.
					 */

    static char *cmdLefOption[] =
    {	
	"read [filename]		read a LEF file filename[.lef]\n"
	"    read [filename] -import	read a LEF file; import cells from .mag files",
	"write [cell]			write LEF for current or indicated cell",
	"writeall			write all cells including the top-level cell\n"
	"    writeall -notop		write all subcells of the top-level cell",
	"help                   	print this help information",
	NULL
    };

    static char *cmdDefOption[] =
    {	
	"read [filename]		read a DEF file filename[.def]",
	"write [cell] [-allspecial]	write DEF for current or indicated cell",
	"writeall			(use \"flatten -nosubckt\" + \"def"
					" write\" instead)",
	"help                   	print this help information",
	NULL
    };

    /* Determine whether this function has been called via the "lef" or
     * the "def" command.
     */
    is_lef = (cmd->tx_argv[0][0] == 'd') ? FALSE : TRUE;

    if (cmd->tx_argc < 2)
	option = LEF_HELP;
    else
    {
	option = Lookup(cmd->tx_argv[1], cmdLefOption);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid %s option.\n", cmd->tx_argv[1],
			cmd->tx_argv[0]);
	    option = LEF_HELP;
	}
    }

    if (option != LEF_HELP)
    {
        selectedUse = CmdGetSelectedCell((Transform *)NULL);
	if (selectedUse == NULL)
	{
	    windCheckOnlyWindow(&w, DBWclientID);
	    if (w == (MagWindow *) NULL)
	    {
		if (ToolGetBox(&selectedDef,NULL) == FALSE)
		{
		    TxError("Point to a window first\n");
		    return;
		}
		selectedUse = selectedDef->cd_parents;
	    }
	    else
		selectedUse = (CellUse *)w->w_surfaceID;
	}
    }

    switch (option)
    {
	case LEF_READ:
            if (cmd->tx_argc != 3)
	    {
		if (cmd->tx_argc == 4)
		{
		    if (*(cmd->tx_argv[3]) == '-')
			if (!strncmp(cmd->tx_argv[3], "-import", 7))
			    lefImport = TRUE;
		}
		else
		    goto wrongNumArgs;
	    }
            namep = cmd->tx_argv[2];
	    if (is_lef)
		LefRead(namep, lefImport);
	    else
		DefRead(namep);
	    break;
	case LEF_WRITEALL:
	    if (!is_lef)
	    {
		TxError("Sorry, can't write hierarchical DEF at this time.\n");
		TxError("Try \"def write\"\n");
	    }
	    else
	    {
		if (cmd->tx_argc == 3)
		    if (*(cmd->tx_argv[2]) == '-')
			if (!strncmp(cmd->tx_argv[2], "-notop", 6))
			    lefTopCell = FALSE;

		LefWriteAll(selectedUse, lefTopCell);
	    }
	    break;
	case LEF_WRITE:
	    if (!is_lef)
	    {
		allSpecial = FALSE;
		if (cmd->tx_argc == 4)
		{
		    if (*(cmd->tx_argv[3]) == '-')
		    {
			if (!strncmp(cmd->tx_argv[3], "-allspec", 8))
			    allSpecial = TRUE;
			else goto wrongNumArgs;
		    }
		    else goto wrongNumArgs;
		}
		else if (cmd->tx_argc != 3) goto wrongNumArgs;
	    }
            else if (cmd->tx_argc != 2) goto wrongNumArgs;
            if (selectedUse == NULL)
            {
                TxError("No cell selected\n");
                return;
            }
            if (cmd->tx_argc == 2)
		namep = selectedUse->cu_def->cd_name;
	    else
		namep = cmd->tx_argv[2];
	    if (!is_lef)
		DefWriteCell(selectedUse->cu_def, namep, allSpecial);
	    else
		LefWriteCell(selectedUse->cu_def, namep, selectedUse->cu_def
			== EditRootDef);
	    break;
	case LEF_HELP:
wrongNumArgs:
	    TxPrintf("The \"%s\" options are:\n", cmd->tx_argv[0]);
	    msg = (is_lef) ? &(cmdLefOption[0]) : &(cmdDefOption[0]);
	    for (; *msg != NULL; msg++)
	    {
		TxPrintf("    %s %s\n", cmd->tx_argv[0], *msg);
	    }
	    break;
    }
}
