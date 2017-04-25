
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResDebug.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "utils/stack.h"
#include "resis/resis.h"

#define MAXNAME			1000
#define KV_TO_mV		1000000


/*
 *-------------------------------------------------------------------------
 *
 * ResPrintNodeList--  Prints out all the nodes in nodelist.
 *
 *
 *  Results:
 *	None.
 *
 *  Side effects:
 *	prints out the 'nodes' in list to fp.
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintNodeList(fp,list)
	FILE *fp;
	resNode *list;

{

     for (; list != NULL; list = list->rn_more)
     {
	  fprintf(fp, "node %p: (%d %d) r= %d\n",
	  	list,list->rn_loc.p_x,list->rn_loc.p_y,list->rn_noderes);
     }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResPrintResistorList--
 *
 *
 * results: none
 *
 *
 * side effects: prints out Resistors in list to file fp.
 *
 *-------------------------------------------------------------------------
 */
void
ResPrintResistorList(fp,list)
    FILE *fp;
    resResistor *list;

{
    for (; list != NULL; list = list->rr_nextResistor)
    {
	if (fp == stdout)
	    TxPrintf("r (%d,%d) (%d,%d) r=%d\n",
	          list->rr_connection1->rn_loc.p_x,
	          list->rr_connection1->rn_loc.p_y,
	          list->rr_connection2->rn_loc.p_x,
	          list->rr_connection2->rn_loc.p_y,
		  list->rr_value);
	else
	    fprintf(fp, "r (%d,%d) (%d,%d) r=%d\n",
	          list->rr_connection1->rn_loc.p_x,
	          list->rr_connection1->rn_loc.p_y,
	          list->rr_connection2->rn_loc.p_x,
	          list->rr_connection2->rn_loc.p_y,
		  list->rr_value);
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResPrintTransistorList--
 *
 *
 *  Results: none
 *
 *  Side effects: prints out transistors in list to file fp.
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintTransistorList(fp,list)
	FILE *fp;
	resTransistor *list;

{
    static char termtype[] = {'g','s','d','c'};
    int i;
    for (; list != NULL; list = list->rt_nextTran)
    {
     	if (list->rt_status & RES_TRAN_PLUG) continue;
	if (fp == stdout)
	    TxPrintf("t w %d l %d ", list->rt_width, list->rt_length);
	else
	    fprintf(fp, "t w %d l %d ", list->rt_width, list->rt_length);
	for (i=0; i!= RT_TERMCOUNT;i++)
	{
	    if (list->rt_terminals[i] == NULL) continue;
	    if (fp == stdout)
		TxPrintf("%c (%d,%d) ",termtype[i],
	       		list->rt_terminals[i]->rn_loc.p_x,
			list->rt_terminals[i]->rn_loc.p_y);
	    else
		fprintf(fp, "%c (%d,%d) ",termtype[i],
	       		list->rt_terminals[i]->rn_loc.p_x,
			list->rt_terminals[i]->rn_loc.p_y);
	       
	}
	if (fp == stdout)
	    TxPrintf("\n");
	else
	    fprintf(fp,"\n");
    }
}
