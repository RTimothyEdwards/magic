
/*
 *-------------------------------------------------------------------------
 *
 * Write.c -- Dumps network 
 *
 *
 *-------------------------------------------------------------------------
 */

#define	MILLITOKILO	1e-6


#ifndef lint
static char sccsid[] = "@(#)Write.c	4.10 MAGIC (Stanford Addition) 07/86";
#endif  /* not lint */
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

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
#include "utils/stack.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"

#define	RT_SIZE	0xf
#define MAXTOKEN 256
#define TRANFLATSIZE 1024
#define RESGROUPSIZE  256

void
ResPrintNetwork(filename, reslist)
    char	*filename;
    resResistor	*reslist;

{
    char	bigname[255],name1[255],name2[255];
     
    FILE	*fp;
    int	i=1;
     
    sprintf(bigname,"%s.%s",filename,"res");
    fp = fopen(bigname,"w");
    if (fp != NULL)
    {
     	  for (;reslist;reslist=reslist->rr_nextResistor)
	  {
	       if  (reslist->rr_connection1->rn_id == 0)
	       {
	       	    reslist->rr_connection1->rn_id = i++;
	       }
	       if  (reslist->rr_connection2->rn_id == 0)
	       {
	       	    reslist->rr_connection2->rn_id = i++;
	       }
	       if (reslist->rr_connection1->rn_why == RES_NODE_ORIGIN)
	       {
	            sprintf(name1,"gnd");
	       }
	       else
	       {
	            sprintf(name1,"n%d_%d_%d",
		        reslist->rr_connection1->rn_id,
		        reslist->rr_connection1->rn_loc.p_x,
	       		reslist->rr_connection1->rn_loc.p_y);
	       }
	       if (reslist->rr_connection2->rn_why == RES_NODE_ORIGIN)
	       {
	            sprintf(name2,"gnd");
	       }
	       else
	       {
	            sprintf(name2,"n%d_%d_%d",
		        reslist->rr_connection2->rn_id,
		        reslist->rr_connection2->rn_loc.p_x,
	       		reslist->rr_connection2->rn_loc.p_y);
	       }
	       fprintf(fp,"r %s %s %f\n",name1,name2,
			(float)(reslist->rr_value)*MILLITOKILO);
	  }
    }
    fclose(fp);
}

void
ResPrintCurrents(filename,extension,node)
	char	*filename;
	float	extension;
	resNode	*node;

{
     char	bigname[255];
     FILE	*fp;
     int	resCurrentPrintFunc();
     
     sprintf(bigname,"%s.%d",filename,abs((int)(extension)));
     
     fp = fopen(bigname,"w");
     if (fp != NULL)
     {
     	  (void) ResTreeWalk(node,NULL,resCurrentPrintFunc,
	  		RES_DO_FIRST,RES_NO_LOOP,RES_NO_FLAGS,fp);
     }
     fclose(fp);
}
	


/*
 *-------------------------------------------------------------------------
 *
 * resCurrentPrintFunc -- prints out a node of network in form compatible with
 *	the linear network solver. Designed for use with ResTreeWalk.
 *
 *
 *-------------------------------------------------------------------------
 */

void
resCurrentPrintFunc(node,resistor,filename)
	resNode		*node;
	resResistor	*resistor;
	FILE		*filename;

{
     tElement	 *workingTran;
     float	i_sum=0.0;
     
     for (workingTran = node->rn_te; workingTran != NULL;
     				workingTran=workingTran->te_nextt)
     {
          if ((workingTran->te_thist->rt_status & RES_TRAN_PLUG) ||
	  	workingTran->te_thist->rt_gate != node)
	  i_sum += workingTran->te_thist->rt_i;
     }
     if (i_sum != 0.0)
     {
     	  if (node->rn_why == RES_NODE_ORIGIN)
	  {
	       fprintf(filename,"i gnd %f\n",i_sum);
	       
	  }
	  else
	  {
	       fprintf(filename,"i n%d_%d %f\n",node->rn_loc.p_x,
	  					node->rn_loc.p_y,i_sum);
	  }
     }

}

void
ResDeviceCounts()
{
         int i,j,k;
         resNode *n;
         resTransistor *t;
         resResistor *r;

	 for (n=ResNodeList,i=0;n!=NULL;n=n->rn_more,i++);
         for (t=ResTransList,j=0;t!=NULL;t=t->rt_nextTran,j++);
         for (r=ResResList,k=0;r!=NULL;r=r->rr_nextResistor,k++);
         TxError("n=%d t=%d r=%d\n",i,j,k);
	 TxFlushErr();
}


void
ResWriteECLFile(filename,reslist,nodelist)
	char	*filename;
	resResistor	*reslist;
	resNode		*nodelist;

{
     char	newname[100],*tmpname,*per;
     FILE	*fp;
     int	nodenum = 0;

     strcpy(newname,filename);
     if (per = strrchr(newname,'.')) *per = '\0';
     strcat(newname,".res");
     
     if ((fp = fopen(newname,"w")) == NULL)
     {
     	  TxError("Can't open %s\n",newname);
	  return;
     }
     for (;nodelist;nodelist=nodelist->rn_more)
     {
	  if (nodelist->rn_name == NULL)
	  {
     	       if (nodelist->rn_noderes == 0)
	       {
	       	    strcpy(newname,"gnd");
	       }
	       else
	       {
	            (void)sprintf(newname,"n%d_%d_%d",nodelist->rn_loc.p_x,
		    	nodelist->rn_loc.p_y,nodenum++);
	       }
	       tmpname = (char *) mallocMagic((unsigned) (strlen(newname)+1));
	       strcpy(tmpname,newname);
	       nodelist->rn_name = tmpname;
	  }
     }
     for (;reslist;reslist = reslist->rr_nextResistor)
     {
	  
     	  fprintf(fp,"r %s %s %f %s %d\n",
	  	reslist->rr_node[0]->rn_name,reslist->rr_node[1]->rn_name,
		 /* /1000.0 gets ohms from milliohms */
		(float)(reslist->rr_value)/1000.0,
		DBTypeShortName(reslist->rr_tt),reslist->rr_csArea); 
     }
     fclose(fp);
}
