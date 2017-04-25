/*
 * irCommand.c --
 *
 * Command interface for interactive router.  This file processes command
 * lines beginning with the `iroute' command.  
 * 
 * (The "wizard" command, `*iroute', for testing, debugging, etc., 
 *  is processed in irTestCmd.c.)
 * 
 *     ********************************************************************* 
 *     * Copyright (C) 1987, 1990 Michael H. Arnold, Walter S. Scott, and  *
 *     * the Regents of the University of California.                      * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/irouter/irCommand.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "dbwind/dbwtech.h"
#include "textio/txcommands.h"
#include "utils/main.h"
#include "utils/utils.h"
#include "commands/commands.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "mzrouter/mzrouter.h"
#include "irouter/irouter.h"
#include "irouter/irInternal.h"

/* window command issued to */
static MagWindow *irWindow;

/* Subcommand table - declared here since its referenced before defined */
typedef struct
{
    char	*sC_name;	/* name of iroute subcommand */
    void	(*sC_proc)();	/* Procedure implementing this subcommand */
    char 	*sC_commentString;	/* describes subcommand */
    char 	*sC_usage;		/* command syntax */

} SubCmdTableE;
extern SubCmdTableE irSubcommands[]; 


/*
 * ----------------------------------------------------------------------------
 *
 * irSetNoisyAutoInt
 *
 * Set integer parameter, interpeting the string "AUTOMATIC" (or prefix)
 * as -1.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If valueS is a nonnull string, interpret and set parm
 *      accordingly.
 *
 *      If valueS is null, the parameter value is left unaltered.
 *
 *      If file is nonnull parameter value is written to file.
 *
 *      If file is null, parameter value is written to magic text window via
 *      TxPrintf
 *
 *	If file is (FILE *)1, parameter value is returned as a Tcl object
 *	
 * ----------------------------------------------------------------------------
 */

void
irSetNoisyAutoInt(parm, valueS, file)
    int *parm;
    char *valueS;
    FILE *file;
{
    int which;

    /* special value Table */
#define V_AUTOMATIC	-1
    static struct
    {
	char	*sv_name;	/* name */
	int	 sv_type;
    } specialValues[] = {
	"automatic",	V_AUTOMATIC,
	0
    };
    
    /* If value non-null set parm */
    if(valueS!=NULL)
    {
	int i;

    	/* check if special value */
	which = LookupStruct(
	    valueS,
	    (char **) specialValues, 
	    sizeof specialValues[0]);

	if(which == -1)
	{
	    TxError("Ambiguous value: '%s'\n",valueS);
	    TxError("Value must be 'AUTOMATIC', or a nonnegative integer\n");
	    return;
	}
	else if (which >= 0 )
	{
	    /* special value */
	    int type = specialValues[which].sv_type;

	    if(type == V_AUTOMATIC)
	    {
	        *parm = -1;
	    }
	    else
	    {
		/* should not ever get here */
		ASSERT(FALSE,"irSetNoisyAutoInt");
	    }
	}
	else if(StrIsInt(valueS) && (i=atoi(valueS))>=0)
	{
	    *parm = i;
	}
	else
	{
	    TxError("Bad value: \"%s\"\n", valueS);
	    TxError("Value must be 'AUTOMATIC', or a nonnegative integer\n");
	    return;
	}
    }

    /* Print parm value */
    if(file)
    {
	if(*parm == -1)
	{
	    fprintf(file,"AUTOMATIC");
	}
	else
	{
	    fprintf(file, "%8d ",*parm);
	}
    }
    else
    {
	if(*parm == -1)
	{
	    TxPrintf("AUTOMATIC");
	}
	else
	{
	    TxPrintf("%8d ",*parm);
	}   
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irLSet<parm> --
 *
 * Set and display Route Layer parameter <parm>.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Call low level routine of appropriate type to set/display parameter.
 *	Value of parm can be displayed either in Magic text window (file is
 *      NULL) or to a file.
 *	
 * ----------------------------------------------------------------------------
 */

#ifdef MAGIC_WRAPPER

/* irLSetActive --  */
Tcl_Obj *
irLSetActive(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewBooleanObj(rL->rl_routeType.rt_active);
    SetNoisyBool(&(rL->rl_routeType.rt_active), s, file);
    return NULL;
}

/* irLSetWidth -- */
Tcl_Obj *
irLSetWidth(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_routeType.rt_width);
    SetNoisyInt(&(rL->rl_routeType.rt_width),s,file);
    return NULL;
}

/* irLSetLength -- */
Tcl_Obj *
irLSetLength(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_routeType.rt_length);
    SetNoisyInt(&(rL->rl_routeType.rt_length),s,file);
    return NULL;
}

/* irLSetHCost -- */
Tcl_Obj *
irLSetHCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_hCost);
    SetNoisyInt(&(rL->rl_hCost),s,file);
    return NULL;
}
	
/* irLSetVCost -- */
Tcl_Obj *
irLSetVCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_vCost);
    SetNoisyInt(&(rL->rl_vCost),s,file);
    return NULL;
}

/* irLSetJogCost -- */
Tcl_Obj *
irLSetJogCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_jogCost);
    SetNoisyInt(&(rL->rl_jogCost),s,file);
    return NULL;
}

/* irLSetHintCost  -- */
Tcl_Obj *
irLSetHintCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_hintCost);
    SetNoisyInt(&(rL->rl_hintCost),s,file);
    return NULL;
}

/* irLSetOverCost  -- */
Tcl_Obj *
irLSetOverCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rL->rl_overCost);
    SetNoisyInt(&(rL->rl_overCost),s,file);
    return NULL;
}

#else

/* irLSetActive --  */
void
irLSetActive(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyBool(&(rL->rl_routeType.rt_active),s,file);
}

/* irLSetWidth -- */
void
irLSetWidth(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_routeType.rt_width),s,file);
}

/* irLSetLength -- */
void
irLSetLength(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_routeType.rt_length),s,file);
}

/* irLSetHCost -- */
void
irLSetHCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_hCost),s,file);
}
	
/* irLSetVCost -- */
void
irLSetVCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_vCost),s,file);
}

/* irLSetJogCost -- */
void
irLSetJogCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_jogCost),s,file);
}

 
/* irLSetHintCost  -- */
void
irLSetHintCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_hintCost),s,file);
}

/* irLSetOverCost  -- */
void
irLSetOverCost(rL,s,file)
    RouteLayer *rL;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rL->rl_overCost),s,file);
}

#endif


/*
 * ----------------------------------------------------------------------------
 *
 * irCSet<parm> --
 *
 * Set and display contact parameter <parm>.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Call low level routine of appropriate type to set/display parameter.
 *	Value of parm can be displayed either in Magic text window (file is
 *      NULL) or to a file.
 *	
 * ----------------------------------------------------------------------------
 */
	
#ifdef MAGIC_WRAPPER

/* irCSetActive -- */
Tcl_Obj *
irCSetActive(rC,s, file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewBooleanObj(rC->rc_routeType.rt_active);
    SetNoisyBool(&(rC->rc_routeType.rt_active),s,file);
    return NULL;
}

/* irCSetWidth --  */
Tcl_Obj *
irCSetWidth(rC,s,file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rC->rc_routeType.rt_width);
    SetNoisyInt(&(rC->rc_routeType.rt_width),s,file);
    return NULL;
}
	
/* irCSetLength--  */
Tcl_Obj *
irCSetLength(rC,s,file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rC->rc_routeType.rt_length);
    SetNoisyInt(&(rC->rc_routeType.rt_length),s,file);
    return NULL;
}

/* irCSetCost -- */
Tcl_Obj *
irCSetCost(rC,s,file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    if (file == (FILE *)1)
	return Tcl_NewIntObj(rC->rc_cost);
    SetNoisyInt(&(rC->rc_cost),s,file);
    return NULL;
}

#else

/* irCSetActive -- */
void
irCSetActive(rC,s, file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    SetNoisyBool(&(rC->rc_routeType.rt_active),s,file);
}

/* irCSetWidth --  */
void
irCSetWidth(rC,s,file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rC->rc_routeType.rt_width),s,file);
}
	
/* irCSetLength --  */
void
irCSetLength(rC,s,file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rC->rc_routeType.rt_length),s,file);
}
	
/* irCSetCost -- */
void
irCSetCost(rC,s,file)
    RouteContact *rC;
    char *s;
    FILE *file;
{
    SetNoisyInt(&(rC->rc_cost),s,file);
}


#endif


/*
 * ----------------------------------------------------------------------------
 *
 * irSrSet<parm> --
 *
 * Set and display search parameter <parm>.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Call low level routine of appropriate type to set/display parameter.
 *	Value of parm can be displayed either in Magic text window (file is
 *      NULL) or to a file.
 *	
 * ----------------------------------------------------------------------------
 */


/* irSrSetRate -- */
void
irSrSetRate(s,file)
    char *s;
    FILE *file;
{
    SetNoisyDI(&irMazeParms->mp_wRate,s,file);
}

/* irSrSetWidth -- */
void
irSrSetWidth(s,file)
    char *s;
    FILE *file;
{
    SetNoisyDI(&(irMazeParms->mp_wWidth),s,file);
}


/*
 * ----------------------------------------------------------------------------
 *
 * irWzdSet<parm> --
 *
 * Set and display wizard parameter <parm>.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Call low level routine of appropriate type to set/display parameter.
 *	Value of parm can be displayed either in Magic text window (file is
 *      NULL) or to a file.
 *	
 * ----------------------------------------------------------------------------
 */

/* irWzdSetBloomCost -- */
void
irWzdSetBloomCost(s,file)
    char *s;
    FILE *file;
{
    SetNoisyDI(&(irMazeParms->mp_bloomDeltaCost),s,file);
}

/* irWzdSetBloomLimit -- */
void
irWzdSetBloomLimit(s,file)
    char *s;
    FILE *file;
{
    SetNoisyDI(&(irMazeParms->mp_bloomLimit),s,file);
}

/* irWzdSetBoundsIncrement -- */
void
irWzdSetBoundsIncrement(s,file)
    char *s;
    FILE *file;
{
    irSetNoisyAutoInt(&(irMazeParms->mp_boundsIncrement),s,file);
}

/* irWzdSetEstimate -- */
void
irWzdSetEstimate(s,file)
    char *s;
    FILE *file;
{
    SetNoisyBool(&(irMazeParms->mp_estimate),s,file);
}

/* irWzdSetExpandEndpoints-- */
void
irWzdSetExpandEndpoints(s, file)
char *s;
FILE *file;
{
    SetNoisyBool(&(irMazeParms->mp_expandEndpoints),s,file);
}

/* irWzdSetPenalty -- */
void
irWzdSetPenalty(s, file)
    char *s;
    FILE *file;
{
    if(s)
    {
        /* arg given, set penalty to it */
	float value;
	if(sscanf(s,"%f",&value)==1)
	{
	    irMazeParms->mp_penalty.rf_mantissa = 
		    (int) (value * (1<<irMazeParms->mp_penalty.rf_nExponent));
	}
	else
	{
	    TxError("Bad penalty value: %s\n",s);
	}
    }

    /* print the current penalty factor. */
    if(file)
    {
	fprintf(file,"%f",
		       (double)irMazeParms->mp_penalty.rf_mantissa /
		       (double)(1<<irMazeParms->mp_penalty.rf_nExponent));
    }
    else
    {
	TxPrintf("%f",
		 (double)irMazeParms->mp_penalty.rf_mantissa /
		 (double)(1<<irMazeParms->mp_penalty.rf_nExponent));
    }

    return;
}

/* irWzdSetPenetration-- */
void
irWzdSetPenetration(s, file)
char *s;
FILE *file;
{
    irSetNoisyAutoInt(&(irMazeParms->mp_maxWalkLength),s,file);
}

/* irWzdSetWindow -- */
void
irWzdSetWindow(s, file)
char *s;
FILE *file;
{
    int which;
    int i, type;

    /* special arg Table */
#define SP_COMMAND	-1
#define SP_DOT		-2
    static struct
    {
	char	*sp_name;	/* name */
	int	 sp_type;
    } specialArgs[] = {
	"command",	SP_COMMAND,
	".",		SP_DOT,
	0
    };

    if(s!=NULL)
    /* set parameter */
    {
	/* check if special arg */
	which = LookupStruct(
	    s,
	    (char **) specialArgs, 
	    sizeof specialArgs[0]);

	if(which == -1)
	{
	    TxError("Ambiguous argument: '%s'\n",s);
	    TxError("Argument must 'COMMAND', '.', or a nonneg. integer\n");
	    return;
	}
	else if (which >= 0 )
	{
	    /* special argument */
	    type = specialArgs[which].sp_type;
	    if(type == SP_COMMAND)
	    {
	        irRouteWid = -1;
	    }
	    else
	    {
		ASSERT(type==SP_DOT,"wzdSetWindow");
		if(irWindow==NULL)
		{
		    TxError("Point to a layout window first!\n");
		    return;
		}
		else
		    irRouteWid = irWindow->w_wid;
	    }
	}
	else if(StrIsInt(s) && (i=atoi(s))>=0)
	{
	    irRouteWid = i;
	}
	else
	{
	    TxError("Bad argument: \"%s\"\n", s);
	    TxError("Argument must be 'COMMAND', '.', or a nonneg. integer\n");
	    return;
	}
    }

    /* Print current value of parm */
    if(file)
    {
	if(irRouteWid == -1)
	{
	    fprintf(file,"COMMAND");
	}
	else
	{
	    fprintf(file,"%d",irRouteWid);
	}
    }
    else
    {
	if(irRouteWid == -1)
	{
	    TxPrintf("COMMAND");
	}
	else
	{
	    TxPrintf("%d",irRouteWid);
	}
    }
    
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irContactsCmd --
 *
 * Irouter subcommand to set and display parameters on contacts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modify contact parameters and display them.
 *	
 * ----------------------------------------------------------------------------
 */

/* Contact Parameter Table */
static struct
{
    char	*cP_name;	/* name of parameter */
#ifdef MAGIC_WRAPPER
    Tcl_Obj	*(*cP_proc)();	/* Procedure processing this parameter */
#else
    void    	(*cP_proc)();	/* Procedure processing this parameter */
#endif
} cParms[] = {
    "active",	irCSetActive,
    "width",	irCSetWidth,
    "length",	irCSetLength,
    "cost",	irCSetCost,
    0
};

/* NEXTVALUE - returns pointer to next value arg (string). */
#define NEXTVALUE \
( \
  (argc <= 4) ? NULL : \
    (nV_i >= argc-1) ? cmd->tx_argv[nV_i=4] : cmd->tx_argv[++nV_i] \
)

void
irContactsCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileType tileType;
    RouteContact *rC;

    int argc = cmd->tx_argc;
    int which, n;
    int nV_i; 			/* Used by NEXTVALUE, must be initialized */
    bool doList = FALSE;

#ifdef MAGIC_WRAPPER
    /* Check for "-list" option */
    if (!strncmp(cmd->tx_argv[argc - 1], "-list", 5))
    {
	doList = TRUE;
	argc--;
    }
#endif

    nV_i = argc - 1;
    
    /* Process by case */
    if(argc == 2 ||
	(argc == 3 && (strcmp(cmd->tx_argv[2],"*")==0)) ||
	(argc >= 4 && 
	    (strcmp(cmd->tx_argv[2],"*")==0) &&
	    (strcmp(cmd->tx_argv[3],"*")==0)))
    {
	/* PROCESS ALL PARMS FOR ALL CONTACT TYPES */

#ifdef MAGIC_WRAPPER
	if (doList)
	{
	    Tcl_Obj *alllist, *rlist, *rname, *robj;
	    alllist = Tcl_NewListObj(0, NULL);

	    /* Process contact parms */
	    for (rC = irRouteContacts; rC != NULL; rC = rC->rc_next)
	    {
		rlist = Tcl_NewListObj(0, NULL);
		rname = Tcl_NewStringObj(
			DBTypeLongNameTbl[rC->rc_routeType.rt_tileType], -1);
		Tcl_ListObjAppendElement(magicinterp, rlist, rname);
		for (n = 0; cParms[n].cP_name; n++)
		{
		    robj = (*cParms[n].cP_proc)(rC, NEXTVALUE, (FILE *)1);
		    Tcl_ListObjAppendElement(magicinterp, rlist, robj);
		}
		Tcl_ListObjAppendElement(magicinterp, alllist, rlist);
	    }
	    Tcl_SetObjResult(magicinterp, alllist);
	}
	else
#endif
	{
	    /* Print Contact Heading */
	    TxPrintf("%-12.12s ", "contact");
	    for(n=0; cParms[n].cP_name; n++)
	    {
		TxPrintf("%8.8s ",cParms[n].cP_name);
	    }
	    TxPrintf("\n");

	    TxPrintf("%-12.12s ", irRepeatChar(strlen("contact"),'-'));
	    for(n=0; cParms[n].cP_name; n++)
	    {
		TxPrintf("%8.8s ",irRepeatChar(strlen(cParms[n].cP_name),'-'));
	    }
	    TxPrintf("\n");

	    /* Process contact parms */
	    for (rC=irRouteContacts; rC!= NULL; rC=rC->rc_next)
	    {
		TxPrintf("%-12.12s ",
			DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
		for(n=0; cParms[n].cP_name; n++)
		{
		    (*cParms[n].cP_proc)(rC,NEXTVALUE,NULL);
		}
		TxPrintf("\n");
	    }
	}
    }
    else if(argc==3 ||
	(argc >= 4 && (strcmp(cmd->tx_argv[3],"*")==0)))
    {
	/* PROCESS ALL PARMS ASSOCIATED WITH CONTACT */
	/* convert layer string to tileType */
	tileType = DBTechNameType(cmd->tx_argv[2]);
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	if(rC=irFindRouteContact(tileType))
	{
	    /* Print Contact Heading */
	    TxPrintf("%-12.12s ", "contact");
	    for(n=0; cParms[n].cP_name; n++)
	    {
		TxPrintf("%8.8s ",cParms[n].cP_name);
	    }
	    TxPrintf("\n");

	    TxPrintf("%-12.12s ", irRepeatChar(strlen("contact"),'-'));
	    for(n=0; cParms[n].cP_name; n++)
	    {
		TxPrintf("%8.8s ",irRepeatChar(strlen(cParms[n].cP_name),'-'));
	    }
	    TxPrintf("\n");

	    /* Process contact parms */
	    TxPrintf("%-12.12s ",
		DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
	    for(n=0; cParms[n].cP_name; n++)
	    {
		(*cParms[n].cP_proc)(rC,NEXTVALUE,NULL);
	    }
	    TxPrintf("\n");
	}
	else 
	{
	    TxError("Unrecognized route-contact: \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}
    }
    else if(argc>=4 && strcmp(cmd->tx_argv[2],"*")==0)
    {
	/* PROCESS A COLUMN (THE VALUES OF A PARAMETER FOR ALL CONTACTS) */
     
	/* Lookup parameter name in contact parm table */
	which = LookupStruct(
	    cmd->tx_argv[3], 
	    (char **) cParms, 
	    sizeof cParms[0]);

	/* Process table lookup */
	if (which == -1)
	{
	    /* AMBIGUOUS PARAMETER */
	    TxError("Ambiguous parameter: \"%s\"\n", 
		cmd->tx_argv[3]);
	    return;
	}
	else if (which<0)
	{
	    /* PARAMETER NOT FOUND */
	    TxError("Unrecognized parameter: %s\n", cmd->tx_argv[3]);
	    TxError("Valid contact parameters are:  ");
	    for (n = 0; cParms[n].cP_name; n++)
		TxError(" %s", cParms[n].cP_name);
	    TxError("\n");
	    return;
	}

	else 
	{
	    /* CONTACT PARAMETER FOUND */

	    /* Print Heading */
	    TxPrintf("%-12.12s ", "contact");
	    TxPrintf("%8.8s ",cParms[which].cP_name);
	    TxPrintf("\n");

	    TxPrintf("%-12.12s ", irRepeatChar(strlen("contact"),'-'));
	    TxPrintf("%8.8s ",
		irRepeatChar(strlen(cParms[which].cP_name),'-'));
	    TxPrintf("\n");

	    /* Process contact parm */
	    for (rC=irRouteContacts; rC!= NULL; rC=rC->rc_next)
	    {
		TxPrintf("%-12.12s ",
		    DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
		(*cParms[which].cP_proc)(rC,NEXTVALUE,NULL);
		TxPrintf("\n");
	    }
	}
    }
    else if(argc>=4)
    {
	/* PROCESS PARAMETER ASSOCIATED WITH CONTACT */
	/* convert layer string to tileType */
	tileType = DBTechNameType(cmd->tx_argv[2]);
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	if(rC=irFindRouteContact(tileType))
	{
	    /* Lookup contact parameter name in table */
	    which = LookupStruct(
		cmd->tx_argv[3], 
		(char **) cParms, 
		sizeof cParms[0]);

	    /* Process result of lookup */
	    if (which >= 0)
	    {
		/* parameter found - call proc that processes it 
		 * NULL second arg means display only
		 */
		(*cParms[which].cP_proc)(rC,NEXTVALUE,NULL);
		TxPrintf("\n");
	    }
	    else if (which == -1)
	    {
		/* ambiguous parameter - complain */
		TxError("Ambiguous parameter: \"%s\"\n", 
		    cmd->tx_argv[3]);
		return;
	    }
	    else
	    {
		/* unrecognized parameter - complain */
		TxError("Unrecognized parameter: %s\n", cmd->tx_argv[3]);
		TxError("Valid contact parameters are:  ");
		for (n = 0; cParms[n].cP_name; n++)
		    TxError(" %s", cParms[n].cP_name);
		TxError("\n");
		return;
	    }
	}
	else 
	{
	    TxError("Unrecognized route-contact: \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

    }

    /* Give warning if number of parm values didn't come out even */
    if (nV_i != argc-1)
    {
	TxError("Warning:  Number of parameter values didn't match number of parameters.\n");
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irHelpCmd --
 *
 * Irouter subcommand to describe available commands.  (Driven by command
 * table, defined above IRCommand()).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Display requested help info.
 *	
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
void
irHelpCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    int which;

    if(cmd->tx_argc == 2)
    {
	/* No arg, so print summary of commands */
	TxPrintf("\niroute - route from cursor to box\n\n");
	for(n=0; irSubcommands[n].sC_name!=NULL; n++)
	{
	    TxPrintf("iroute %s - %s\n",
		    irSubcommands[n].sC_name,
		    irSubcommands[n].sC_commentString);
	}
	TxPrintf("\niroute help <subcmd>");
	TxPrintf(" - print usage info for subcommand.\n\n");
    }
    else
    {
	/* Lookup subcommand in table, and printed associated help info */
	which = LookupStruct(
	    cmd->tx_argv[2], 
	    (char **) irSubcommands, 
	    sizeof irSubcommands[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* subcommand found - call print out its comment string and usage */
	    TxPrintf("\niroute %s - %s\n",
		    irSubcommands[which].sC_name,
		    irSubcommands[which].sC_commentString);
	    TxPrintf("\nusage:\niroute %s\n",
		    irSubcommands[which].sC_usage);
	}
	else if (which == -1)
	{
	    /* ambiguous subcommand - complain */
	    TxError("Ambiguous iroute subcommand: \"%s\"\n", cmd->tx_argv[2]);
	}
	else
	{
	    /* unrecognized subcommand - complain */
	    TxError("Unrecognized iroute subcommand: \"%s\"\n", 
		    cmd->tx_argv[2]);
	    TxError("Valid iroute irSubcommands are:  ");
	    for (n = 0; irSubcommands[n].sC_name; n++)
		TxError(" %s", irSubcommands[n].sC_name);
	    TxError("\n");
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irLayersCmd --
 *
 * Irouter subcommand to set and display parameters on route layers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modify contact parameters and display them.
 *	
 * ----------------------------------------------------------------------------
 */

/* Layer Parameter Table */
static struct
{
    char	*lP_name;	/* name of parameter */
#ifdef MAGIC_WRAPPER
    Tcl_Obj	*(*lP_proc)();	/* procedure processing this parameter */
#else
    void	(*lP_proc)();	/* procedure processing this parameter */
#endif
} lParms[] = {
    "active",	irLSetActive,
    "width",	irLSetWidth,
    "length",	irLSetLength,
    "hCost",	irLSetHCost,
    "vCost",	irLSetVCost,
    "jogCost",	irLSetJogCost,
    "hintCost", irLSetHintCost,
    "overCost", irLSetOverCost,
    0
};

/* NEXTVALUE - returns pointer to next value arg (string). */
#define NEXTVALUE \
( \
  (argc <= 4) ? NULL : \
    (nV_i >= argc-1) ? cmd->tx_argv[nV_i=4] : cmd->tx_argv[++nV_i] \
)

    /*ARGSUSED*/	
void
irLayersCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileType tileType;
    RouteLayer *rL;
    bool doList = FALSE;

    int argc = cmd->tx_argc;
    int which, n;
    int nV_i;		/* Used by NEXTVALUE, must be initialized */

#ifdef MAGIC_WRAPPER
    /* Check for "-list" option */
    if (!strncmp(cmd->tx_argv[argc - 1], "-list", 5))
    {
	doList = TRUE;
	argc--;
    }
#endif

    nV_i = argc - 1;
    
    /* Process by case */
    if(argc == 2 ||
	(argc == 3 && (strcmp(cmd->tx_argv[2],"*")==0)) ||
	(argc >= 4 && 
	    (strcmp(cmd->tx_argv[2],"*")==0) &&
	    (strcmp(cmd->tx_argv[3],"*")==0)))
    {
	/* PROCESS ALL PARMS FOR ALL ROUTE LAYERS */

#ifdef MAGIC_WRAPPER
	if (doList)
	{
	    Tcl_Obj *alllist, *rlist, *robj, *rname;
	    alllist = Tcl_NewListObj(0, NULL);

	    /* Process parms for each route layer */
	    for (rL = irRouteLayers; rL != NULL; rL = rL->rl_next)
	    {
		rlist = Tcl_NewListObj(0, NULL);
		rname = Tcl_NewStringObj(
			DBTypeLongNameTbl[rL->rl_routeType.rt_tileType], -1);
		Tcl_ListObjAppendElement(magicinterp, rlist, rname);
		for (n = 0; lParms[n].lP_name; n++)
		{
		    robj = (*lParms[n].lP_proc)(rL, NEXTVALUE, (FILE *)1);
		    Tcl_ListObjAppendElement(magicinterp, rlist, robj);
		}
	        Tcl_ListObjAppendElement(magicinterp, alllist, rlist);
	    }
	    Tcl_SetObjResult(magicinterp, alllist);
	}
	else
#endif
	{
	    /* Print Route Layer Heading */
	    TxPrintf("%-12.12s ", "layer");
	    for(n=0; lParms[n].lP_name; n++)
	    {
		TxPrintf("%8.8s ",lParms[n].lP_name);
	    }
	    TxPrintf("\n");

	    TxPrintf("%-12.12s ", irRepeatChar(strlen("layer"),'-'));
	    for(n=0; lParms[n].lP_name; n++)
	    {
		TxPrintf("%8.8s ", irRepeatChar(strlen(lParms[n].lP_name),'-'));
	    }
	    TxPrintf("\n");

	    /* Process parms for each route layer */
	    for (rL=irRouteLayers; rL!= NULL; rL=rL->rl_next)
	    {
		TxPrintf("%-12.12s ", 
			DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
		for(n=0; lParms[n].lP_name; n++)
		{
		    (*lParms[n].lP_proc)(rL,NEXTVALUE,NULL);
		}
		TxPrintf("\n");
	    }
	}
    }
    else if(argc==3 ||
	(argc >= 4 && (strcmp(cmd->tx_argv[3],"*")==0)))
    {
	/* PROCESS ALL PARMS ASSOCIATED WITH ROUTE LAYER */
	/* convert layer string to tileType */
	tileType = DBTechNameType(cmd->tx_argv[2]);
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	if (rL=irFindRouteLayer(tileType))
	{
	    /* Print Route Layer Heading */
	    TxPrintf("%-12.12s ", "layer");
	    for(n=0; lParms[n].lP_name; n++)
	    {
		TxPrintf("%8.8s ",lParms[n].lP_name);
	    }
	    TxPrintf("\n");

	    TxPrintf("%-12.12s ", irRepeatChar(strlen("layer"),'-'));
	    for(n=0; lParms[n].lP_name; n++)
	    {
		TxPrintf("%8.8s ", irRepeatChar(strlen(lParms[n].lP_name),'-'));
	    }
	    TxPrintf("\n");

	    /* Process parms for route layer */
	    TxPrintf("%-12.12s ", 
		DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
	    for(n=0; lParms[n].lP_name; n++)
	    {
		(*lParms[n].lP_proc)(rL,NEXTVALUE,NULL);
	    }
	    TxPrintf("\n");
	}
	else 
	{
	    TxError("Unrecognized route layer or contact: \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}
    }
    else if(argc>=4 && strcmp(cmd->tx_argv[2],"*")==0)
    {
	/* PROCESS A COLUMN (THE VALUES OF A PARAMETER FOR ALL LAYERS) */
     
	/* Lookup parameter name in layer parm table */
	which = LookupStruct(
	    cmd->tx_argv[3], 
	    (char **) lParms, 
	    sizeof lParms[0]);

	/* Process table lookup */
	if (which == -1)
	{
	    /* AMBIGUOUS PARAMETER */
	    TxError("Ambiguous parameter: \"%s\"\n", 
		cmd->tx_argv[3]);
	    return;
	}

	else if (which<0)
	{
	    /* PARAMETER NOT FOUND */
	    TxError("Unrecognized parameter: %s\n", cmd->tx_argv[3]);
	    TxError("Valid layer parameters are:  ");
	    for (n = 0; lParms[n].lP_name; n++)
		TxError(" %s", lParms[n].lP_name);
	    TxError("\n");
	    return;
	}

	else
	{
	    /* LAYER PARAMETER FOUND */

	    /* Print Heading */
	    TxPrintf("%-12.12s ", "layer");
	    TxPrintf("%8.8s ",lParms[which].lP_name);
	    TxPrintf("\n");

	    TxPrintf("%-12.12s ", irRepeatChar(strlen("layer"),'-'));
	    TxPrintf("%8.8s ", 
		irRepeatChar(strlen(lParms[which].lP_name),'-'));
	    TxPrintf("\n");

	    /* Process parm for each route layer */
	    for (rL=irRouteLayers; rL!= NULL; rL=rL->rl_next)
	    {
		TxPrintf("%-12.12s ", 
		    DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
		(*lParms[which].lP_proc)(rL,NEXTVALUE,NULL);
		TxPrintf("\n");
	    }

	}
    }
    else if(argc>=4)
    {
	/* PROCESS PARAMETER ASSOCIATED WITH ROUTE LAYER */
	/* convert layer string to tileType */
	tileType = DBTechNameType(cmd->tx_argv[2]);
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	if (rL=irFindRouteLayer(tileType))
	{
	    /* Lookup route layer parameter name in table */
	    which = LookupStruct(
		cmd->tx_argv[3], 
		(char **) lParms, 
		sizeof lParms[0]);

	    /* Process result of lookup */
	    if (which >= 0)
	    {
		/* parameter found - call proc that processes it 
		 * NULL second arg means display only
		 */
		(*lParms[which].lP_proc)(rL,NEXTVALUE,NULL);
		TxPrintf("\n");
	    }
	    else if (which == -1)
	    {
		/* ambiguous parameter - complain */
		TxError("Ambiguous parameter: \"%s\"\n", 
		    cmd->tx_argv[3]);
		return;
	    }
	    else
	    {
		/* unrecognized parameter - complain */
		TxError("Unrecognized parameter: %s\n", cmd->tx_argv[3]);
		TxError("Valid route layer parameters are:  ");
		for (n = 0; lParms[n].lP_name; n++)
		    TxError(" %s", lParms[n].lP_name);
		TxError("\n");
		return;
	    }
	}
	else 
	{
	    TxError("Unrecognized layer: \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}
    }

    /* Give warning if number of parm values didn't come out even */
    if (nV_i != argc-1)
    {
	TxError("Warning:  Number of parameter values didn't match number of parameters.\n");
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irRouteCmd --
 *
 * Irouter subcommand to actually do a route. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Compute a route and paint it into edit cell.
 *	
 * ----------------------------------------------------------------------------
 */

/* Route options Table */
static char* rOptions[] = {
    "-dbox",	/* 0 */
    "-dlabel",  /* 1 */
    "-dlayers", /* 2 */
    "-drect",   /* 3 */
    "-dselection",/* 4 */
    "-scursor",	/* 5 */
    "-slabel",	/* 6 */
    "-slayers", /* 7 */
    "-spoint",	/* 8 */
#ifdef MAGIC_WRAPPER
    "-timeout",	/* 9 */
#endif
    NULL
};

void
irRouteCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Point *startPtArg = NULL;	/* pts to start point, if given on command */
    Rect  *destRectArg = NULL;	/* pts to dest rect, if given on command */
    char *startLabel = NULL;	/* pts to labelname, if given on command */
    char *destLabel = NULL;	/* pts to labelname, if given on command */
    List *startLayers = NULL;
    List *destLayers = NULL;
    int startType;		/* type of start specifier */
    int destType;		/* type of destination specifier */
    Point startPt;
    Rect destRect;
    int irResult = MZ_NO_ACTION;
    int i;			/* index of arg being processed */
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv;

    TileTypeBitMask layerMask;
    RouteLayer *rL;

    /* set startType and destType to defaults */
    startType = ST_CURSOR;
    destType = DT_BOX;

    /* skip over cmd name args */
    i = 2;

    /* process options */
    while(i<argc)
    {
	int which = Lookup(argv[i],&(rOptions[0]));
	switch (which) 
	{
	    case -2:
		/* not found */
		TxError("Bad option to 'iroute route':  '%s'\n",argv[i]);
		goto leaveClean;
	    case -1:
		/* ambiguous */
		TxError("Ambiguous option to 'iroute route':  '%s'\n",argv[i]);
		goto leaveClean;
	    case 0:
		/* dBox */
		destType = DT_BOX;
		break;
	    case 1:
		/* dLabel */
		destType = DT_LABEL;
		if(++i>=argc)
		{
		    TxError("Missing label.\n");
		    goto leaveClean;
		}
		destLabel = argv[i];
		break;
	    case 2:
		/* dLayers */
		if(++i>=argc)
		{
		    TxError("Missing layer list.\n");
		    goto leaveClean;
		}
		(void) CmdParseLayers(argv[i],&layerMask);
		for(rL=irRouteLayers; rL!=NULL; rL=rL->rl_next)
		{
		    if(TTMaskHasType(&layerMask,rL->rl_routeType.rt_tileType) &&
			    rL->rl_routeType.rt_active)
		    {
			LIST_ADD(rL, destLayers);
		    }
		}
		if(destLayers==NULL)
		{
		    TxError("No active route layers in destination list!\n");
		    goto leaveClean;
		}
		break;
	    case 3:
		/* dRect */
		destType = DT_RECT;
		if(++i>=argc)
		{
		    TxError("Incomplete coordinates.\n");
		    goto leaveClean;
		}
		if(!StrIsNumeric(argv[i]))
		{
		    TxError("Non coordinate: %s\n",argv[i]);
		    goto leaveClean;
		}
		destRect.r_xbot = cmdParseCoord(w, argv[i], FALSE, TRUE);

		if(++i>=argc)
		{
		    TxError("Incomplete coordinates.\n");
		    goto leaveClean;
		}
		if(!StrIsNumeric(argv[i]))
		{
		TxError("Nonnumeric coordinate: %s\n",argv[i]);
		goto leaveClean;
		}
		destRect.r_ybot = cmdParseCoord(w, argv[i], FALSE, FALSE);

		if(++i>=argc)
		{
		    TxError("Incomplete coordinates.\n");
		    goto leaveClean;
		}
		if(!StrIsNumeric(argv[i]))
		{
		    TxError("Nonnumeric coordinate: %s\n",argv[i]);
		    goto leaveClean;
		}
		destRect.r_xtop = cmdParseCoord(w, argv[i], FALSE, TRUE);

		if(++i>=argc)
		{
		    TxError("Incomplete coordinates.\n");
		    goto leaveClean;
		}
		if(!StrIsNumeric(argv[i]))
		{
		TxError("Nonnumeric coordinate: %s\n",argv[i]);
		goto leaveClean;
		}
		destRect.r_ytop = cmdParseCoord(w, argv[i], FALSE, FALSE);

		destRectArg = &destRect;
		break;	    
	    case 4:
		/* dSelection */
		destType = DT_SELECTION;
		break;
	    case 5:
		/* sCursor */
		startType = ST_CURSOR;
		break;
	    case 6:
		/* sLabel */
		startType = ST_LABEL;
		if(++i>=argc)
		{
		    TxError("Missing label.\n");
		    goto leaveClean;
		}
		startLabel = argv[i];
		break;
	    case 7:
		/* sLayers */
		if(++i>=argc)
		{
		    TxError("Missing layer list.\n");
		    goto leaveClean;
		}
		(void) CmdParseLayers(argv[i],&layerMask);
		for(rL=irRouteLayers; rL!=NULL; rL=rL->rl_next)
		{
		    if(TTMaskHasType(&layerMask,
				     rL->rl_routeType.rt_tileType) &&
			    rL->rl_routeType.rt_active)
		    {
			LIST_ADD(rL, startLayers);
		    }
		}
		if(startLayers==NULL)
		{
		    TxError("No active route layers in start list!\n");
		    goto leaveClean;
		}
	        break;
	    case 8:
		/* sPoint */
		startType = ST_POINT;
		if(++i>=argc)
		{
		    TxError("Incomplete coordinates.\n");
		    goto leaveClean;
		}
		if(!StrIsNumeric(argv[i]))
		{
		    TxError("Nonnumeric coordinate: %s\n",argv[i]);
		    goto leaveClean;
		}
		startPt.p_x = cmdParseCoord(w, argv[i], FALSE, TRUE);
		if(++i>=argc)
		{
		    TxError("Incomplete coordinates.\n");
		    goto leaveClean;
		}
		if(!StrIsNumeric(argv[i]))
		{
		TxError("Nonnumeric coordinate: %s\n",argv[i]);
		goto leaveClean;
		}
		startPt.p_y = cmdParseCoord(w, argv[i], FALSE, FALSE);

		startPtArg = &startPt;
		break;
#ifdef MAGIC_WRAPPER
	    case 9:
		if(++i>=argc)
		{
		    TxError("No timeout value given.\n");
		    goto leaveClean;
		}
		if(!StrIsInt(argv[i]))
		{
		    TxError("Noninteger timeout value: %s\n",argv[i]);
		    goto leaveClean;
		}
		SigRemoveTimer();
		SigTimerInterrupts();
		SigSetTimer(atoi(argv[i]));
		break;
#endif
	    default:
		/* shouldn't happen */
		ASSERT(FALSE,"irRouteCmd");
		break;
	}

	/* advance to next option */
	++i;
    }

    /* We're done parsing the command, call irRoute to do the real work */
    irResult = irRoute(w, startType, startPtArg, startLabel, startLayers,
			destType, destRectArg, destLabel, destLayers);

#ifdef MAGIC_WRAPPER
    SigTimerDisplay();

    /* Set Tcl Result to irResult */
    switch (irResult)
    {
	case MZ_SUCCESS:
	    Tcl_SetResult(magicinterp, "Route success", 0);
	    break;
	case MZ_CURRENT_BEST:
	    Tcl_SetResult(magicinterp, "Route best before interrupt", 0);
	    break;
	case MZ_FAILURE:
	    Tcl_SetResult(magicinterp, "Route failure", 0);
	    break;
	case MZ_UNROUTABLE:
	    Tcl_SetResult(magicinterp, "Route unroutable", 0);
	    break;
	case MZ_INTERRUPTED:
	    Tcl_SetResult(magicinterp, "Route interrupted", 0);
	    break;
	case MZ_ALREADY_ROUTED:
	    Tcl_SetResult(magicinterp, "Route already routed", 0);
	    break;
    }
#endif

leaveClean:
    ListDealloc(startLayers);
    ListDealloc(destLayers);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irSearchCmd --
 *
 * Irouter subcommand to set and display search parameters.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modify search parameters and display them.
 *	
 * ----------------------------------------------------------------------------
 */

/* Search Parameter Table */
static struct
{
    char	*srP_name;	/* name of parameter */
    void	(*srP_proc)();	/* Procedure processing this parameter */
} srParms[] = {
    "rate",		irSrSetRate,
    "width",		irSrSetWidth,
    0
};

void
irSearchCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    
    /* Process by case */
    if(cmd->tx_argc == 2)
    /* print values of all parms */
    {
	int n;

	for(n=0; srParms[n].srP_name; n++)
	{
	    TxPrintf("  %s=", srParms[n].srP_name); 
	    (*srParms[n].srP_proc)(NULL,NULL);
	}
	TxPrintf("\n");
    }
    else if(cmd->tx_argc == 3 || cmd->tx_argc == 4)
    /* process single parameter */
    {
	int which;

	/* Lookup parameter name in contact parm table */
	which = LookupStruct(
	    cmd->tx_argv[2], 
	    (char **) srParms, 
	    sizeof srParms[0]);

	/* Process table lookup */
	if (which == -1)
	/* parameter ambiguous */
	{
	    TxError("Ambiguous parameter: \"%s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}
	else if (which<0)
	/* parameter not found */
	{
	    int n;

	    TxError("Unrecognized parameter: %s\n", cmd->tx_argv[2]);
	    TxError("Valid search parameters are:  ");
	    for (n = 0; srParms[n].srP_name; n++)
		TxError(" %s", srParms[n].srP_name);
	    TxError("\n");
	    return;
	}
	else 
	/* parameter found - process it */
	{
	    char *arg;

	    if(cmd->tx_argc == 3)
	    /* just want current value - use null argument */
	    {
		arg = NULL;
	    }
	    else
	    /* setting parameter, arg = value string */
	    {
		arg = cmd->tx_argv[3];
	    }

	    TxPrintf("  %s=", srParms[which].srP_name); 
	    (*srParms[which].srP_proc)(arg,NULL);
	    TxPrintf("\n");
	}

    }
    else
    /* Too many arguments */
    {
	TxError("Too many args on 'iroute search'\n");
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irSpacingsCmd --
 *
 * Irouter subcommand to set and display minimum spacings between 
 * routetypes. 
 * 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modify and display spacing parameters. 
 *	
 * ----------------------------------------------------------------------------
 */

void
irSpacingsCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileType tileType;
    RouteType *rT;
    char *s;
    int which, value, argI, i, n;

    /* Special Value Table */
    static struct
    {
	char	*sV_name;	/* name of value */
	int     sV_value;	/* corresponding interger value */
    } sValue[] = {
	"n",		-1,
	"nil",		-1,
	"none",		-1,
	"null",		-1,
	0
    };

    /* Subcell Table */
    static struct
    {
	char	*sT_name;	/* name of value */
	int     sT_value;	/* corresponding interger value */
    } subcellTable[] = {
	"subcell",		TT_SUBCELL,
	0
    };

    /* Process by case */
    if(cmd->tx_argc == 2)
    {
	/* NO ARGS TO SPACING REQUEST - DISPLAY ALL SPACINGS */

	/* Print spacings for each route type */
	for (rT=irRouteTypes; rT!= NULL; rT=rT->rt_next)
	{
	    TxPrintf("%s:  ", 
	        DBTypeLongNameTbl[rT->rt_tileType]);
	    for (i=0;i<TT_MAXTYPES;i++)
		if(rT->rt_spacing[i]>=0)
		    TxPrintf("%s=%d ",DBTypeLongNameTbl[i],rT->rt_spacing[i]);
	    if(rT->rt_spacing[TT_SUBCELL]>=0)
		TxPrintf("%s=%d ","SUBCELL",rT->rt_spacing[TT_SUBCELL]);
	    TxPrintf("\n\n");
	}
    }
    else if(cmd->tx_argc==3)
    {
	/* ONE ARG TO SPACING REQUEST */

	if (strcmp(cmd->tx_argv[2],"CLEAR")==0)
	{
	    /* CLEAR ALL SPACINGS */
	    for (rT=irRouteTypes; rT!= NULL; rT=rT->rt_next)
		/* <=TT_MAXTYPES below includes TT_SUBCELL */
		for (i=0;i<=TT_MAXTYPES;i++)
		    rT->rt_spacing[i]= -1;
	}
	else
	{
	    /* PRINT SPACINGS FOR GIVEN ROUTE LAYER */

	    /* convert layer string to tileType */
	    tileType = DBTechNameType(cmd->tx_argv[2]);
	    if(tileType<0) 
	    {
		TxError("Unrecognized layer (type): \"%.20s\"\n", 
		    cmd->tx_argv[2]);
		return;
	    }

	    if (rT=irFindRouteType(tileType))
	    {

		TxPrintf("%s:  ", 
		    DBTypeLongNameTbl[rT->rt_tileType]);
		for (i=0;i<TT_MAXTYPES;i++)
		    if(rT->rt_spacing[i]>=0)
			TxPrintf("%s=%d ",
				DBTypeLongNameTbl[i],rT->rt_spacing[i]);
		if(rT->rt_spacing[TT_SUBCELL]>=0)
		    TxPrintf("%s=%d ","SUBCELL",rT->rt_spacing[TT_SUBCELL]);
		TxPrintf("\n\n");
	    }
	    else 
	    {
		TxError("Unrecognized route layer or contact: \"%.20s\"\n", 
		    cmd->tx_argv[2]);
		return;
	    }
	}
    }
    else if(cmd->tx_argc==4)
    {
	/* PRINT VALUE OF GIVEN SPACING */
     
	/* convert layer string to tileType */
	tileType = DBTechNameType(cmd->tx_argv[2]);
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	if ((rT=irFindRouteType(tileType))==NULL)
	{
	    TxError("Unrecognized route layer or contact: \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	/* convert type-string spacing is "to" to tiletype */
	tileType = DBTechNameType(cmd->tx_argv[3]);
	if(tileType<0) 
	{
	    /* if not a real type, check to see if "SUBCELL" */
	    which = LookupStruct(
		cmd->tx_argv[3],
		(char **) subcellTable,
		sizeof subcellTable[0]);
	    if ( which>= 0)
	        tileType = TT_SUBCELL;
	}
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[3]);
	    return;
	}

	/* Print current value of spacing */
	if(rT->rt_spacing[tileType] >= 0)
	    TxPrintf("\t%d\n",rT->rt_spacing[tileType]);
	else
	    TxPrintf("\tNIL\n");
    }
    else if(EVEN(cmd->tx_argc))
    {
	/* TYPE PARMS DON'T PAIR EVENLY WITH VALUE PARMS */
	TxError("Type and value args don't pair evenly.\n");
	TxError("Usage:  *iroute spacing [routeType] [type1] [value1] [type2 value2] ... [typen valuen]\n");
    }
    else
    {
	/* SET SPACINGS */
     
	/* convert layer string to tileType */
	tileType = DBTechNameType(cmd->tx_argv[2]);
	if(tileType<0) 
	{
	    TxError("Unrecognized layer (type): \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

	if ((rT=irFindRouteType(tileType))==NULL)
	{
	    TxError("Unrecognized route layer or contact: \"%.20s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}

 	/* Prepend tab to echo of new values */
	TxPrintf("\t");

        /* process type/value pairs */
	for (argI=3; argI<cmd->tx_argc; argI +=2)
	{
	    /* convert type-string spacing is "to" to tiletype */
	    tileType = DBTechNameType(cmd->tx_argv[argI]);
	    if(tileType<0) 
	    {
		/* if not a real type, check to see if "SUBCELL" */
		which = LookupStruct(
		    cmd->tx_argv[argI],
		    (char **) subcellTable,
		    sizeof subcellTable[0]);
		if ( which>= 0)
	        tileType = TT_SUBCELL;
	    }
	    if(tileType<0) 
	    {
		TxError("\nUnrecognized layer (type): \"%.20s\"\n", 
		    cmd->tx_argv[argI]);
		continue;
	    }

	    /* convert value-string to integer */
	    s = cmd->tx_argv[argI+1];
	    if (StrIsNumeric(s))
	    {
		value = cmdParseCoord(w, s, TRUE, FALSE);
		if (value < -1)
		{
		    TxError("\nBad spacing value: %d\n",value);
		    TxError("Valid spacing values are:  ");
		    TxError("<a nonnegative integer> -1");
		    for (n = 0; sValue[n].sV_name; n++)
			TxError(" %s", sValue[n].sV_name);
		TxError("\n");
		return;
		}
	    }
	    else
	    {
		/* Lookup in special value table */
		which = LookupStruct(
		    s, 
		    (char **) sValue, 
		    sizeof sValue[0]);

		/* Process result of lookup */
		if (which >= 0)
		{
		    /* special value found, set string accordingly */
		    value = sValue[which].sV_value;
		}
		else if (which == -1)
		{
		    /* ambiguous value - complain */
		    TxError("\nAmbiguous value: \"%s\"\n",s);
		    continue;
		}
		else
		{
		    /* unrecognized value - complain */
		    TxError("Bad spacing value: %s\n",s);
		    TxError("Valid spacing values are:  ");
		    TxError("<a nonnegative integer> -1");
		    for (n = 0; sValue[n].sV_name; n++)
			TxError(" %s", sValue[n].sV_name);
		    TxError("\n");
		    continue;
		}
	    }

	    /* Set value in route type */
	    rT->rt_spacing[tileType]=value;

	    /* Print new value */
	    if(rT->rt_spacing[tileType] != -1)
		TxPrintf(" %s=%d",
		    (tileType == TT_SUBCELL ? 
			"SUBCELL" : DBTypeLongNameTbl[tileType]),
			rT->rt_spacing[tileType]);
	    else
		TxPrintf(" %s=NIL",
		    (tileType==TT_SUBCELL ? 
			"SUBCELL" : DBTypeLongNameTbl[tileType]));
	}
	TxPrintf("\n");
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irVerbosityCmd --
 *
 * Irouter subcommand to set amount of messages given by irouter and mzrouter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets verbosity parameter
 *	
 * ----------------------------------------------------------------------------
 */

void
irVerbosityCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if(cmd->tx_argc >3)
    {
	TxError("'iroute verbosity' only takes one arg!\n");
	return;
    }
    
    if(cmd->tx_argc == 3)
    {
	int i;

	/* ONE ARG */
	if(StrIsInt(cmd->tx_argv[2]) && (i=atoi(cmd->tx_argv[2]))>=0)
	{
	    irMazeParms->mp_verbosity = i;
	}
	else
	{
	    TxError("Bad argument: \"%s\"\n", cmd->tx_argv[2]);
	    TxError("Argument must be a nonnegative integer\n");
	    return;
	}
    }

    /* Print current value of verbosity */
    switch (irMazeParms->mp_verbosity)
    {
	case 0:
	/* shhhhh! we're in silent mode */
	break;
	    
	case 1:
	TxPrintf("\t1 (Brief messages)\n");
	break;
	    
	default:
	ASSERT(irMazeParms->mp_verbosity>=2,"irVerbosityCmd");
	TxPrintf("\t%d (Lots of statistics)\n",
		 irMazeParms->mp_verbosity);
    }
    
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irVersionCmd --
 *
 * Irouter subcommand to display irouter version string.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Displays version string.
 *	
 * ----------------------------------------------------------------------------
 */

void
irVersionCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{

    if(cmd->tx_argc == 2)
    {
	/* Print out version string */
        TxPrintf("\tIrouter version %s\n", IROUTER_VERSION);
    }
    else
    {
	TxError("Too many args on 'iroute version'\n");
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irWizardCmd --
 *
 * Irouter subcommand to set and display less frequently modified  parameters.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *	
 * ----------------------------------------------------------------------------
 */

/* Wizard Parameter Table */
static struct
{
    char	*wzdP_name;	/* name of parameter */
    void	(*wzdP_proc)();	/* Procedure processing this parameter */
} wzdParms[] = {
    "bloom",		irWzdSetBloomCost,
    "bloomLimit",	irWzdSetBloomLimit,
    "boundsIncrement",	irWzdSetBoundsIncrement,
    "estimate",		irWzdSetEstimate,
    "expandEndpoints",	irWzdSetExpandEndpoints,
    "penalty",		irWzdSetPenalty,
    "penetration",	irWzdSetPenetration,
    "window",		irWzdSetWindow,
    0
};

void
irWizardCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    
    /* Process by case */
    if(cmd->tx_argc == 2)
    /* print values of all parms */
    {
	int n;

	for(n=0; wzdParms[n].wzdP_name; n++)
	{
	    TxPrintf("  %s=", wzdParms[n].wzdP_name); 
	    (*wzdParms[n].wzdP_proc)(NULL,NULL);
	    TxPrintf("\n");
	}
    }
    else if(cmd->tx_argc == 3 || cmd->tx_argc == 4)
    /* process single parameter */
    {
	int which;

	/* Lookup parameter name in contact parm table */
	which = LookupStruct(
	    cmd->tx_argv[2], 
	    (char **) wzdParms, 
	    sizeof wzdParms[0]);

	/* Process table lookup */
	if (which == -1)
	/* parameter ambiguous */
	{
	    TxError("Ambiguous parameter: \"%s\"\n", 
		cmd->tx_argv[2]);
	    return;
	}
	else if (which<0)
	/* parameter not found */
	{
	    int n;

	    TxError("Unrecognized parameter: %s\n", cmd->tx_argv[2]);
	    TxError("Valid wizard parameters are:  ");
	    for (n = 0; wzdParms[n].wzdP_name; n++)
		TxError(" %s", wzdParms[n].wzdP_name);
	    TxError("\n");
	    return;
	}
	else 
	/* parameter found - process it */
	{
	    char *arg;

	    if(cmd->tx_argc == 3)
	    /* just want current value - use null argument */
	    {
		arg = NULL;
	    }
	    else
	    /* setting parameter, arg = value string */
	    {
		arg = cmd->tx_argv[3];
	    }

	    TxPrintf("  %s=", wzdParms[which].wzdP_name); 
	    (*wzdParms[which].wzdP_proc)(arg,NULL);
	    TxPrintf("\n");
	}

    }
    else
    /* Too many arguments */
    {
	TxError("Too many args on 'iroute wizard'\n");
    }

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * irSaveParametersCmd --
 *
 * Irouter subcommand to create "source" file setting all irouter parameters
 * (except ref. window) to current value - to reset to these values just
 * source the file with `:source'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Write file of Magic commands to set parms. to current values.
 *	
 * NOTE:
 *      Note defined after all other commands, so it can make use of 
 *      datastructures defined just in front of other commands, 
 *      such as the search parameter table.
 * ----------------------------------------------------------------------------
 */

void
irSaveParametersCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    FILE *saveFile;
    RouteContact *rC;
    RouteLayer *rL;

    /* make sure exactly one arg given */
    if(cmd->tx_argc !=3)
    {
	if(cmd->tx_argc == 2)
	    TxError("Must specify save file!\n");
	else
	    TxError("Too many args on ':iroute saveParameter'\n");
	return;
    }

    /* open save file */
    saveFile = fopen(cmd->tx_argv[2], "w");
    if (saveFile == NULL) 
    {
	TxError("Could not open file '%s' for writing.\n", cmd->tx_argv[2]);
	return;
    }

    /* write header comment */
    fprintf(saveFile,"# Irouter version %s\n", IROUTER_VERSION);
    fprintf(saveFile,"#\n");
    fprintf(saveFile,
	    "# This is a Magic command file generated by the Magic command\n");
    fprintf(saveFile,"#\t:iroute saveParameters\n");
    fprintf(saveFile,"# To restore these parameter settings,");
    fprintf(saveFile," use the Magic `:source' command.\n\n");

    /* turn verbosity down to errors and warnings, to avoid lots of
     * gibberish when the file is sourced.
     */
    fprintf(saveFile,":iroute verbosity 0\n");
	    
    /* save CONTACT parameters */
    for (rC=irRouteContacts; rC!= NULL; rC=rC->rc_next)
    {
	int n;

	fprintf(saveFile,":iroute contact %s * ",
		       DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
	for(n=0; cParms[n].cP_name; n++)
	{
	    (*cParms[n].cP_proc)(rC,NULL,saveFile);
	}
	fprintf(saveFile,"\n");
    }

    /* save LAYER parameters */
    for (rL=irRouteLayers; rL!= NULL; rL=rL->rl_next)
    {
	int n;

	fprintf(saveFile,":iroute layer %s * ", 
	    DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
	for(n=0; lParms[n].lP_name; n++)
	{
	    (*lParms[n].lP_proc)(rL,NULL,saveFile);
	}
	fprintf(saveFile,"\n");
    }

    /* save SEARCH parameters */
    {
	int n;

	for(n=0; srParms[n].srP_name; n++)
	{
	    fprintf(saveFile,":iroute search %s ", 
			   srParms[n].srP_name); 
	    (*srParms[n].srP_proc)(NULL,saveFile);
	    fprintf(saveFile,"\n");
	}
    }

    /* save SPACINGS */
    {
	RouteType *rT;
	int i;

	fprintf(saveFile,":iroute spacings CLEAR\n");
	for (rT=irRouteTypes; rT!= NULL; rT=rT->rt_next)
	{
	    for (i=0;i<TT_MAXTYPES;i++)
		if(rT->rt_spacing[i]>=0)
		    fprintf(saveFile,":iroute spacings %s %s %d\n", 
				   DBTypeLongNameTbl[rT->rt_tileType],
				   DBTypeLongNameTbl[i],
				   rT->rt_spacing[i]);
	    if(rT->rt_spacing[TT_SUBCELL]>=0)
		fprintf(saveFile,":iroute spacings %s %s %d\n", 
			       DBTypeLongNameTbl[rT->rt_tileType],
			       "SUBCELL",
			       rT->rt_spacing[TT_SUBCELL]);
	}
    }

    /* save WIZARD parameters */
    {
	int n;

	for(n=0; wzdParms[n].wzdP_name; n++)
	{
	    fprintf(saveFile,":iroute wizard %s ", 
			   wzdParms[n].wzdP_name); 
	    (*wzdParms[n].wzdP_proc)(NULL,saveFile);
	    fprintf(saveFile,"\n");
	}
    }

    /* save VERBOSITY parameter (done last so :source is silent) */
    fprintf(saveFile,":iroute verbosity %d\n",
	    irMazeParms->mp_verbosity);
    
    (void) fclose(saveFile);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * IRCommand --
 *
 * Command interface for interactive router.  Processes `:iroute' command
 * lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the command; see below.
 *
 * Organization:
 *	We select a procedure based on the first keyword (argv[0])
 *	and call it to do the work of implementing the subcommand.  Each
 *	such procedure is of the following form:
 *
 *	int	
 *	proc(argc, argv)
 *	    int argc;
 *	    char *argv[];
 *	{
 *	}
 *
 * ----------------------------------------------------------------------------
 */

/*--- Subcommand Table ---*/

SubCmdTableE irSubcommands[] = {
    "contacts",	irContactsCmd,
    "set route-contact parameters",
    "contacts [type] [parameter] [value1] ... [valuen]\n\
\t(can use '*' for type or parameter)",

    "help",		irHelpCmd,
    "summarize iroute subcommands",
    "help [subcommand]",

    "layers",	irLayersCmd,
    "set route-layer parameters",
    "layers [type] [parameter] [value1] ... [valuen]\n\
\t(can use '*' for type or parameter)",

    "route",	irRouteCmd,
    "connect point to node(s)",
    "route [options]\n\
\t-sLayers layers = layers route may start on.\n\
\t-sCursor = start route at cursor (DEFAULT)\n\
\t-sLabel name = start route at label of given name\n\
\t-sPoint x y = start route at given coordinates\n\
\t-dLayers layers = layers route may end on.\n\
\t-dBox = route to box (DEFAULT)\n\
\t-dLabel name = route to label of given name\n\
\t-dRect xbot ybot xtop ytop =  route to rectangle of given coordinates\n\
\t-dSelection = route to selection",

    "saveParameters",	irSaveParametersCmd,
    "write out all irouter parameters\n\
\t(can be read back with :source)",
    "saveParameters <filename>",

    "search", irSearchCmd,
    "set parameters controlling the internal search for routes",
    "search [searchParameter] [value]",

    "spacings",	irSpacingsCmd,
    "set minimum spacing between route-type and arbitrary type",
    "spacings [route-type] [type] [spacing] ... [typen spacingn]\n\
\t(types can be 'SUBCELL', spacing can be 'nil')\n\
iroute spacings CLEAR\n\
\t(sets all spacings to nil)",

    "verbosity", irVerbosityCmd,
    "control the amount of messages printed",
    "verbosity [level]\n\
\t(0 = errors and warnings only, 1 = brief, 2 = lots of statistics)",

    "version",	irVersionCmd,
    "identify irouter version",
    "version",

    "wizard", irWizardCmd,
    "set miscellaneous parameters",
    "wizard [wizardParameter] [value]",

    0
}, *subCmdP;

void
IRCommand(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    int which;

    /* make sure we have maze parameters */
    if(irMazeParms==NULL)
    {
	TxError("Need irouter style in mzrouter section of technology file");
	TxError(" to use irouter.\n");
	return;
    }

    /* make window available to all subroutines */
    irWindow = w;

    /* If in silent mode, turn printing off */
    if(irMazeParms->mp_verbosity==0)
    {
	TxPrintOff();
    }

    if(cmd->tx_argc == 1)
    {
	int irResult;

	/* No subcommand specified - so just route from cursor to box
	 * No endpts or layers explicitly given on cmd, so args are
	 * NULL below.
	 */
	irResult = irRoute(w, ST_CURSOR,
		(Point*)NULL, (char*)NULL, (List*)NULL,     
		DT_BOX, (Point*)NULL, (char*)NULL, (List*)NULL);

#ifdef MAGIC_WRAPPER
	/* Set Tcl Result to irResult */
	switch (irResult)
	{
	    case MZ_SUCCESS:
		Tcl_SetResult(magicinterp, "Route success", 0);
		break;
	    case MZ_CURRENT_BEST:
		Tcl_SetResult(magicinterp, "Route best before interrupt", 0);
		break;
	    case MZ_FAILURE:
		Tcl_SetResult(magicinterp, "Route failure", 0);
		break;
	    case MZ_UNROUTABLE:
		Tcl_SetResult(magicinterp, "Route unroutable", 0);
		break;
	    case MZ_INTERRUPTED:
		Tcl_SetResult(magicinterp, "Route interrupted", 0);
		break;
	    case MZ_ALREADY_ROUTED:
		Tcl_SetResult(magicinterp, "Route already routed", 0);
		break;
	}
#endif

    }
    else
    {
	/* Lookup subcommand in table */
	which = LookupStruct(
	    cmd->tx_argv[1], 
	    (char **) irSubcommands, 
	    sizeof irSubcommands[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* subcommand found - call proc that implements it */
	    subCmdP = &irSubcommands[which];
	    (*subCmdP->sC_proc)(w,cmd);
	}
	else if (which == -1)
	{
	    /* ambiguous subcommand - complain */
	    TxError("Ambiguous iroute subcommand: \"%s\"\n", cmd->tx_argv[1]);
	}
	else
	{
	    /* unrecognized subcommand - complain */
	    TxError("Unrecognized iroute subcommand: \"%s\"\n", 
		    cmd->tx_argv[1]);
	    TxError("Valid iroute irSubcommands are:  ");
	    for (n = 0; irSubcommands[n].sC_name; n++)
		TxError(" %s", irSubcommands[n].sC_name);
	    TxError("\n");
	}
    }

    /* turn printing back on, and return */
    TxPrintOn();
    return;
}
