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
#include "utils/utils.h"
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
    int option, i, cargs, units = 1000;	    /* Default nanometers */
    char **msg, *namep;
    CellUse *selectedUse;
    CellDef *selectedDef;
    bool is_lef;
    bool lefImport = FALSE;		/* Indicates whether or not we
					 * read cells from .mag files
					 * when the FOREIGN statement
					 * is encountered in a macro.
					 */
    bool lefAnnotate = FALSE;		/* Indicates that no celldefs should be
					 * created from any LEF files, which
					 * will be used for annotation only.
					 */
    bool lefTopCell = TRUE;		/* Indicates whether or not we
					 * write the top-level cell to
					 * LEF, or just the subcells.
					 */
    bool allSpecial = FALSE;		/* Indicates whether we should
					 * treat all geometry as "special"
					 * nets in DEF format output.
					 */
    bool lefTech = FALSE;		/* Indicates that technology info
					 * will be output along with the
					 * lef macro.
					 */
    int lefHide = -1;			/* If >= 0, hide all details of the macro
					 * other than pin area surrounding labels,
					 * with the indicated setback distance.
					 */
    bool lefTopLayer = FALSE;		/* If TRUE, only output the topmost
					 * layer used by a pin, and make
					 * all layers below it obstructions.
					 */
    bool lefDoMaster = TRUE;		/* If TRUE, output masterslice layers;
					 * If FALSE, ignore masterslice layers.
					 */
    bool recurse = FALSE;		/* If TRUE, recurse on all subcells
					 * during "writeall".  By default,
					 * only the immediate children of the
					 * top level cell are output.
					 */
    bool defLabelNets = FALSE;		/* If TRUE, attach a label to the
					 * center of the first rectangle
					 * found on that net.
					 */

    static char *cmdLefOption[] =
    {
	"read [filename]		read a LEF file filename[.lef]\n"
	"    read [filename] -import	read a LEF file; import cells from .mag files\n"
	"    read [filename] -annotate	read a LEF file for cell annotation only."
	"write [filename] [-tech]	write LEF for current cell\n"
	"    write [filename] -hide	hide all details other than ports\n"
	"    write [filename] -hide <d>	hide details in area set back distance <d>",
	"writeall			write all cells including the top-level cell\n"
	"    writeall -notop		write all children of the top-level cell\n"
	"    writeall -all		recurse on all subcells of the top-level cell\n"
	"    writeall -hide		hide all details other than ports\n"
	"    writeall -hide [dist]	hide details in area set back distance dist",
	"help                   	print this help information",
	NULL
    };

    static char *cmdDefOption[] =
    {
	"read [filename]		read a DEF file filename[.def]",
	"write [cell] [-allspecial]	write DEF for current or indicated cell\n",
	"write -labels			label every net in NETS with the net name",
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
            if (cmd->tx_argc > 3)
	    {
		for (i = 3; i < cmd->tx_argc; i++)
		{
		    if (*(cmd->tx_argv[i]) == '-')
		    {
			if (!strncmp(cmd->tx_argv[i], "-import", 7))
			    lefImport = TRUE;
			else if (!strncmp(cmd->tx_argv[i], "-anno", 5))
			    lefAnnotate = TRUE;
			else if (!strncmp(cmd->tx_argv[i], "-label", 6))
			{
			    if (is_lef)
				TxPrintf("The \"-labels\" option is only for def read\n");
			    else
				defLabelNets = TRUE;
			}
		    }
		}
	    }
	    else if (cmd->tx_argc < 3)
		goto wrongNumArgs;

            namep = cmd->tx_argv[2];
	    if (is_lef)
		LefRead(namep, lefImport, lefAnnotate);
	    else
		DefRead(namep, defLabelNets);
	    break;
	case LEF_WRITEALL:
	    if (!is_lef)
	    {
		TxError("Sorry, can't write hierarchical DEF at this time.\n");
		TxError("Try \"def write\"\n");
	    }
	    else
	    {
		for (i = 2; i < cmd->tx_argc; i++)
		{
		    if (*(cmd->tx_argv[i]) == '-')
		    {
			if (!strncmp(cmd->tx_argv[i], "-notop", 6))
			    lefTopCell = FALSE;
			else if (!strncmp(cmd->tx_argv[i], "-tech", 5))
			    lefTech = TRUE;
			else if (!strncmp(cmd->tx_argv[i], "-hide", 5))
			{
			    lefHide = 0;
			    if ((i < (cmd->tx_argc - 1)) &&
				    StrIsNumeric(cmd->tx_argv[i + 1]))
			    {
				lefHide = cmdParseCoord(w, cmd->tx_argv[i + 1],
					    FALSE, TRUE);
				i++;
			    }
			}
			else if (!strncmp(cmd->tx_argv[i], "-toplayer", 9))
			    lefTopLayer = TRUE;
			else if (!strncmp(cmd->tx_argv[i], "-nomaster", 9))
			    lefDoMaster = FALSE;
			else if (!strncmp(cmd->tx_argv[i], "-all", 4))
			    recurse = TRUE;
			else goto wrongNumArgs;
		    }
		    else goto wrongNumArgs;
		}
		LefWriteAll(selectedUse, lefTopCell, lefTech, lefHide, lefTopLayer,
			    lefDoMaster, recurse);
	    }
	    break;
	case LEF_WRITE:
	    allSpecial = FALSE;
	    cargs = cmd->tx_argc;
	    for (i = 2; i < cmd->tx_argc; i++)
	    {
		if (*(cmd->tx_argv[i]) == '-')
		{
		    if (!strncmp(cmd->tx_argv[i], "-allspec", 8))
		    {
			if (!is_lef)
			    allSpecial = TRUE;
			else
			    TxPrintf("The \"-allspec\" option is only for def write\n");
		    }
		    else if (!strncmp(cmd->tx_argv[i], "-tech", 5))
		    {
			if (is_lef)
			    lefTech = TRUE;
			else
			    TxPrintf("The \"-tech\" option is only for lef write\n");
		    }
		    else if (!strncmp(cmd->tx_argv[i], "-hide", 5))
		    {
			if (is_lef)
			{
			    lefHide = 0;
			    if ((i < (cmd->tx_argc - 1)) &&
				    StrIsNumeric(cmd->tx_argv[i + 1]))
			    {
				lefHide = cmdParseCoord(w, cmd->tx_argv[i + 1],
					    FALSE, TRUE);
				cargs--;
				i++;
			    }
			}
			else
			    TxPrintf("The \"-hide\" option is only for lef write\n");
		    }
		    else if (!strncmp(cmd->tx_argv[i], "-toplayer", 9))
		    {
			if (is_lef)
			    lefTopLayer = TRUE;
			else
			    TxPrintf("The \"-toplayer\" option is only for lef write\n");
		    }
		    else if (!strncmp(cmd->tx_argv[i], "-nomaster", 9))
		    {
			if (is_lef)
			    lefDoMaster = FALSE;
			else
			    TxPrintf("The \"-nomaster\" option is only for lef write\n");
		    }
		    else if (!strncmp(cmd->tx_argv[i], "-units", 5))
		    {
			if (is_lef)
			    TxPrintf("The \"-units\" option is only for def write\n");
			else
			{
			    i++;
			    cargs--;
			    if ((cmd->tx_argc < i) || (!StrIsInt(cmd->tx_argv[i])))
			    {
				TxPrintf("The \"-units\" option requires an argument.\n");
			    }
			    else
			    {
				units = atoi(cmd->tx_argv[i]);
				// To do:  Check range of units
			    }
			}
		    }
		    else goto wrongNumArgs;
		    cargs--;
		}
		else if (i != 2)	    /* Is argument a filename? */
		    goto wrongNumArgs;
	    }
            if (cargs != 2 && cargs != 3) goto wrongNumArgs;
            if (selectedUse == NULL)
            {
                TxError("No cell selected\n");
                return;
            }
            if (cargs == 2)
		namep = selectedUse->cu_def->cd_name;
	    else
		namep = cmd->tx_argv[2];
	    if (!is_lef)
		DefWriteCell(selectedUse->cu_def, namep, allSpecial, units);
	    else
		LefWriteCell(selectedUse->cu_def, namep, selectedUse->cu_def
			== EditRootDef, lefTech, lefHide, lefTopLayer, lefDoMaster);
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
