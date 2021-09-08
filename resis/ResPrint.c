
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResPrint.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "tcltk/tclmagic.h"
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
#include "cif/cif.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "utils/stack.h"
#include "utils/styles.h"
#include "resis/resis.h"

#define MAXNAME			1000
#define KV_TO_mV		1000000

extern ResSimNode *ResInitializeNode();


/*
 *-------------------------------------------------------------------------
 *
 * ResPrintExtRes-- Print resistor network to output file.
 *
 * Results:none
 *
 * Side Effects:prints network.
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintExtRes(outextfile, resistors, nodename)
    FILE	*outextfile;
    resResistor *resistors;
    char	*nodename;

{
    int	        nodenum=0;
    char	newname[MAXNAME];
    HashEntry  *entry;
    ResSimNode *node, *ResInitializeNode();

    for (; resistors != NULL; resistors = resistors->rr_nextResistor)
    {
	/*
	 * These names shouldn't be null; they should either be set by
	 * the device name or by the node printing routine.  This
	 * code is included in case the resistor network is printed
	 * before the nodes.
	 */

	if (resistors->rr_connection1->rn_name == NULL)
	{
     	    (void)sprintf(newname, "%s%s%d", nodename, ".r", nodenum++);
     	    entry = HashFind(&ResNodeTable, newname);
	    node = ResInitializeNode(entry);
	    resistors->rr_connection1->rn_name = node->name;
	    node->oldname = nodename;
	}
	if (resistors->rr_connection2->rn_name == NULL)
	{
     	    (void)sprintf(newname, "%s%s%d", nodename, ".r", nodenum++);
     	    entry = HashFind(&ResNodeTable, newname);
	    node = ResInitializeNode(entry);
	    resistors->rr_connection2->rn_name = node->name;
	    node->oldname = nodename;
	}
	if (ResOptionsFlags & ResOpt_DoExtFile)
	{
     	    fprintf(outextfile, "resist \"%s\" \"%s\" %g\n",
		    resistors->rr_connection1->rn_name,
		    resistors->rr_connection2->rn_name,
		    resistors->rr_value / (float)ExtCurStyle->exts_resistScale);
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResPrintExtDev-- Print out all devices that have had at least
 *	one terminal changed.
 *
 * Results:none
 *
 * Side Effects:prints device lines to output file
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintExtDev(outextfile, devices)
    FILE	*outextfile;
    RDev	*devices;
{
    TileType t;
    char *subsName;
    ExtDevice *devptr;

    for (; devices != NULL; devices = devices->nextDev)
    {
	if (devices->status & TRUE)
	{
	    if (ResOptionsFlags & ResOpt_DoExtFile)
	    {
		t = devices->layout->rd_devtype;
		devptr = ExtCurStyle->exts_device[t];
		subsName = devptr->exts_deviceSubstrateName;

#ifdef MAGIC_WRAPPER
		/* Substrate variable name substitution */
		if (subsName && subsName[0] == '$' && subsName[1] != '$')
		{
		    char *varsub = (char *)Tcl_GetVar(magicinterp, &subsName[1],
				TCL_GLOBAL_ONLY);
		    if (varsub != NULL) subsName = varsub;
		}
#endif
		/* Output according to device type and class. */
		/* Code largely matches what's in ExtBasic.c extOutputDevices() */

		if (devptr->exts_deviceClass != DEV_FET)
		    fprintf(outextfile,"device ");

		fprintf(outextfile,"%s %s %d %d %d %d ",
			extDevTable[devptr->exts_deviceClass],
			devptr->exts_deviceName,
			devices->layout->rd_inside.r_ll.p_x,
			devices->layout->rd_inside.r_ll.p_y,
			devices->layout->rd_inside.r_ll.p_x + 1,
			devices->layout->rd_inside.r_ll.p_y + 1);

		switch (devptr->exts_deviceClass)
		{
		    case DEV_FET:
			fprintf(outextfile," %d %d",
				devices->layout->rd_area,
				devices->layout->rd_perim);
			break;

		    case DEV_MOSFET:
		    case DEV_ASYMMETRIC:
		    case DEV_BJT:
			fprintf(outextfile," %d %d",
				devices->layout->rd_length,
				devices->layout->rd_width);
			break;
		}

		if (devices->subs != NULL)
		    fprintf(outextfile, " \"%s\"", devices->subs->name);
		else if (subsName != NULL)
		    fprintf(outextfile, " \"%s\"", subsName);
		else
		    fprintf(outextfile, " \"None\"");

		if (devices->gate != NULL)
		    fprintf(outextfile, " \"%s\" %d %s",
			    devices->gate->name,
			    devices->layout->rd_length * 2,
			    devices->rs_gattr);

		if (devices->source != NULL)
		    fprintf(outextfile, " \"%s\" %d %s",
			    devices->source->name,
			    devices->layout->rd_width,
			    devices->rs_sattr);

		if (devices->drain != NULL)
		    fprintf(outextfile, " \"%s\" %d %s",
			    devices->drain->name,
			    devices->layout->rd_width,
			    devices->rs_dattr);

		fprintf(outextfile, "\n");
	    }
	}
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResPrintExtNode-- Prints out all the nodes in the extracted net.
 *
 * Results:none
 *
 * Side Effects: Prints out extracted net. It may add new nodes to the
 *	node hash table.
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintExtNode(outextfile, nodelist, nodename)
	FILE	*outextfile;
	resNode	*nodelist;
	char	*nodename;
{
    int		nodenum = 0;
    char	newname[MAXNAME], tmpname[MAXNAME], *cp;
    HashEntry  *entry;
    ResSimNode *node, *ResInitializeNode();
    bool	DoKillNode = TRUE;
    resNode	*snode = nodelist;

    /* If any of the subnode names match the original node name, then	*/
    /* we don't want to rip out that node with a "killnode" statement.	*/

    for (; nodelist != NULL; nodelist = nodelist->rn_more)
    {
	if (nodelist->rn_name != NULL)
	    if (!strcmp(nodelist->rn_name, nodename))
	    {
		DoKillNode = FALSE;
		break;
	    }
    }

    if ((ResOptionsFlags & ResOpt_DoExtFile) && DoKillNode)
    {
          fprintf(outextfile, "killnode \"%s\"\n", nodename);
    }

    /* Create "rnode" entries for each subnode */

    for (; snode != NULL; snode = snode->rn_more)
    {
	if (snode->rn_name == NULL)
	{
	    (void)sprintf(tmpname,"%s",nodename);

	    cp = tmpname + strlen(tmpname) - 1;
            if (*cp == '!' || *cp == '#') *cp = '\0';

     	    (void)sprintf(newname, "%s%s%d", tmpname, ".n", nodenum++);
     	    entry = HashFind(&ResNodeTable, newname);
	    node = ResInitializeNode(entry);
	    snode->rn_name = node->name;
	    node->oldname = nodename;
	}

	if (ResOptionsFlags & ResOpt_DoExtFile)
	{
	    /* rnode name R C x y  type (R is always 0) */
     	    fprintf(outextfile, "rnode \"%s\" 0 %g %d %d %d\n",
		    snode->rn_name,
		    (snode->rn_float.rn_area / ExtCurStyle->exts_capScale),
		    snode->rn_loc.p_x,
		    snode->rn_loc.p_y,
		    /* the following is TEMPORARILY set to 0 */
		    0);
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResPrintStats -- Prints out the node name, the number of devices,
 *	and the number of nodes for each net added.  Also keeps a running
 *	track of the totals.
 *
 * Results:
 *
 * Side Effects:
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintStats(goodies, name)
    ResGlobalParams	*goodies;
    char		*name;
{
    static int	totalnets = 0, totalnodes = 0, totalresistors = 0;
    int nodes, resistors;
    resNode	*node;
    resResistor *res;

    if (goodies == NULL)
    {
     	  TxError("nets:%d nodes:%d resistors:%d\n",
	  	  totalnets, totalnodes, totalresistors);
	totalnets = 0;
	totalnodes = 0;
	totalresistors = 0;
	return;
    }
    nodes = 0;
    resistors = 0;
    totalnets++;
    for (node = ResNodeList; node != NULL; node=node->rn_more)

    {
     	nodes++;
	totalnodes++;
    }
    for (res = ResResList; res != NULL; res=res->rr_nextResistor)
    {
     	resistors++;
	totalresistors++;
    }
    TxError("%s %d %d\n", name, nodes, resistors);
}

/*
 *-------------------------------------------------------------------------
 *
 * Write the nodename to the output.  If the name does not exist, the node
 * ID number is used as the name.  Assumes that node has either a valid
 * name or valid ID record.
 *
 *-------------------------------------------------------------------------
 */

void
resWriteNodeName(fp, nodeptr)
   FILE		*fp;
   resNode	*nodeptr;
{
    if (nodeptr->rn_name == NULL)
	fprintf(fp, "N%d", nodeptr->rn_id);
    else
	fprintf(fp, "N%s", nodeptr->rn_name);
}

/*
 *-------------------------------------------------------------------------
 *
 * Write a description of the resistor network geometry, compatible
 * with FastHenry (mainly for doing inductance extraction)
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintFHNodes(fp, nodelist, nodename, nidx, celldef)
    FILE	*fp;
    resNode	*nodelist;
    char	*nodename;
    int		*nidx;
    CellDef	*celldef;
{
    char 	newname[16];
    resNode	*nodeptr;
    resResistor	*resptr, *contact_res;
    resElement	*elemptr;
    float	oscale, height;
    int		np;

    if (fp == NULL) return;

    oscale = CIFGetOutputScale(1000);   /* 1000 for conversion to um */

    fprintf(fp, "\n* List of nodes in network\n");
    for (nodeptr = nodelist; nodeptr; nodeptr = nodeptr->rn_more)
    {
	if (nodeptr->rn_name == NULL)
	{
	    nodeptr->rn_id = (*nidx);
	    (*nidx)++;
	}
	else
	{
	    HashEntry  *entry;
	    ResSimNode *simnode;

	    /* If we process another sim file node while doing this	*/
	    /* one, mark it as status "REDUNDANT" so we don't duplicate	*/
	    /* the entry.						*/

     	    entry = HashFind(&ResNodeTable, nodeptr->rn_name);
	    simnode = (ResSimNode *)HashGetValue(entry);
	    if (simnode != NULL)
		simnode->status |= REDUNDANT;
	}
	resWriteNodeName(fp, nodeptr);

	/* Height of the layer is the height of the first non-contact   */
	/* layer type connected to any resistor connected to this node.	*/

	contact_res = (resResistor *)NULL;
	for (elemptr = nodeptr->rn_re; elemptr; elemptr = elemptr->re_nextEl)
	{
	    resptr = elemptr->re_thisEl;
	    if (!DBIsContact(resptr->rr_tt))
	    {
		height = ExtCurStyle->exts_height[resptr->rr_tt];
		if (height == 0)
		{
		    int pnum = DBPlane(resptr->rr_tt);
		    int hnum = ExtCurStyle->exts_planeOrder[pnum];
		    height = 0.1 * hnum;
		}
	    }
	    else
		contact_res = resptr;
	}
	height *= oscale;

	fprintf(fp, " x=%1.2f y=%1.2f z=%1.2f\n",
		(float)nodeptr->rn_loc.p_x * oscale,
		(float)nodeptr->rn_loc.p_y * oscale,
		height);

	/* If it's a contact region and has more than one contact, add	*/
	/* contact points as individual nodes and connect to the main 	*/
	/* node with an "equiv" record.					*/

	if (contact_res != (resResistor *)NULL &&
		(contact_res->rr_cl > 1 ||
		contact_res->rr_width > 1))
	{
	    int i, j, edge, spacing;
	    float del, cx, cy, cxb, cyb;

	    CIFGetContactSize(contact_res->rr_tt, &edge, &spacing, NULL);

	    del = (float)(spacing + edge) / (oscale * 100);

	    cxb = (float)(contact_res->rr_cl - 1) / 2;
	    for (i = 0; i < contact_res->rr_cl; i++)
	    {
		cx = del * ((float)i - cxb);
	        cyb = (float)(contact_res->rr_width - 1) / 2;
		for (j = 0; j < contact_res->rr_width; j++)
		{
		    cy = del * ((float)j - cyb);
		    resWriteNodeName(fp, nodeptr);
		    fprintf(fp, "_%d_%d ", i, j);
		    fprintf(fp, "x=%1.2f y=%1.2f z=%1.2f\n",
			((float)nodeptr->rn_loc.p_x + cx) * oscale,
			((float)nodeptr->rn_loc.p_y + cy) * oscale,
			height);
		}
	    }

	    /* Short all the contact nodes together with .equiv records */

	    fprintf(fp, ".equiv ");
	    resWriteNodeName(fp, nodeptr);
	    for (i = 0; i < contact_res->rr_cl; i++)
	    {
		for (j = 0; j < contact_res->rr_width; j++)
		{
		    fprintf(fp, " ");
		    resWriteNodeName(fp, nodeptr);
		    fprintf(fp, "_%d_%d", i, j);
		}
	    }
	    fprintf(fp, "\n");
	}
    }

    fprintf(fp, "\n* List of externally-connected ports\n.external");
    np = 0;
    for (nodeptr = nodelist; nodeptr; nodeptr = nodeptr->rn_more)
    {
	if (nodeptr->rn_name != NULL)
	{
	    if (np < 2)
	    {
		Label *lab;

		fprintf(fp, " N%s", nodeptr->rn_name);

		/* This part is sort of a hack---need a better hook to	*/
		/* the original label this external port connects to,	*/
		/* rather than search for it every time we write an	*/
		/* external connection.					*/

		for (lab = celldef->cd_labels; lab != NULL; lab = lab->lab_next)
		    if (lab->lab_flags & PORT_DIR_MASK)
			if (!strcmp(lab->lab_text, nodeptr->rn_name))
			{
			    if ((lab->lab_flags & PORT_NUM_MASK) != ResPortIndex)
			    {
				lab->lab_flags &= (~(PORT_NUM_MASK));
				lab->lab_flags |= ResPortIndex;
				TxPrintf("Port %s reassigned index %d\n",
					lab->lab_text,
					lab->lab_flags & PORT_NUM_MASK);
				celldef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
			    }
			    ResPortIndex++;
			}
	    }
	    else
	    {
		if (np == 2)
		    fprintf(fp, "\n* Warning! external nodes not recorded:");
		fprintf(fp, " N%s", nodeptr->rn_name);
	    }
	    np++;
	}
    }
    fprintf(fp, "\n\n");

    /* Shouldn't this work? */

/*
    fprintf(fp, "\n* List of externally-connected ports\n");
    for (nodeptr = nodelist; nodeptr; nodeptr = nodeptr->rn_more)
	if (nodeptr->rn_name != NULL)
	    fprintf(fp, ".external N%s Nsub\n", nodeptr->rn_name);

    fprintf(fp, "\n");
*/
}

/*
 *-------------------------------------------------------------------------
 * ResPrintFHRects --
 *	Generate FastHenry segment output to the FastHenry data file
 *	"fp".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff written to the stream file "fp".
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintFHRects(fp, reslist, nodename, eidx)
    FILE	*fp;
    resResistor	*reslist;
    char	*nodename;
    int		*eidx;		/* element (segment) index */
{
    resResistor	*resistors;
    float	oscale, thick, cwidth;
    int		edge;

    if (fp == NULL) return;

    oscale = CIFGetOutputScale(1000);   /* 1000 for conversion to um */

    fprintf(fp, "* Segments connecting nodes in network\n");
    for (resistors = reslist; resistors; resistors = resistors->rr_nextResistor)
    {
	if (DBIsContact(resistors->rr_tt) &&
		(resistors->rr_cl > 1 || resistors->rr_width > 1))
	{
	    int i, j;

	    CIFGetContactSize(resistors->rr_tt, &edge, NULL, NULL);

	    /* 100 is for centimicrons to microns conversion */
	    cwidth = (float)edge / 100;

	    /* for contacts, rr_cl = squares in x, rr_width = squares in y */

	    for (i = 0; i < resistors->rr_cl; i++)
	    {
		for (j = 0; j < resistors->rr_width; j++)
		{
		    fprintf(fp, "E%d ", *eidx);
		    resWriteNodeName(fp, resistors->rr_connection1);
		    fprintf(fp, "_%d_%d ", i, j);
		    resWriteNodeName(fp, resistors->rr_connection2);
		    fprintf(fp, "_%d_%d ", i, j);

		    /* Vias are vertical and so w and h are the dimensions of	*/
		    /* the via hole.  For other layers, h is layer thickness.	*/

		    fprintf(fp, "w=%1.2f h=%1.2f\n", cwidth, cwidth);

		    (*eidx)++;
		}
	    }
	}
	else
	{
	    fprintf(fp, "E%d ", *eidx);
	    resWriteNodeName(fp, resistors->rr_connection1);
	    fprintf(fp, " ");
	    resWriteNodeName(fp, resistors->rr_connection2);

	    if (DBIsContact(resistors->rr_tt))
	    {
		CIFGetContactSize(resistors->rr_tt, &edge, NULL, NULL);
		/* 100 for centimicrons to microns conversion */
		cwidth = (float)edge / 100;
		fprintf(fp, " w=%1.2f h=%1.2f\n", cwidth, cwidth);
	    }
	    else
	    {
		/* For non-via layers, h is layer thickness. */

		thick = ExtCurStyle->exts_thick[resistors->rr_tt];
		if (thick == 0) thick = 0.05;
		fprintf(fp, " w=%1.2f h=%1.2f\n",
			(float)resistors->rr_width * oscale,
			thick * oscale);

	    }
	    (*eidx)++;
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResPrintReference --
 *
 *	Write the reference plane (substrate) definition to the geometry
 *	(FastHenry) file output.
 *
 *	NOTE:  For now, I am assuming that substrate = ground (GND).
 *	However, a device list is passed, and it should be parsed
 *	for substrate devices, allowing the creation of VDD and GND
 *	reference planes for both substrate and wells.
 *
 *	Another note:  For now, I am assuming a uniform reference
 *	plane of the size of the cell bounding box.  It may be
 *	preferable to search tiles and generate multiple, connected
 *	reference planes.  Or it may be desirable to have an effectively
 *	infinite reference plane by extending it far out from the
 *	subcircuit bounding box.
 *
 *-------------------------------------------------------------------------
 */

void
ResPrintReference(fp, devices, cellDef)
    FILE	*fp;
    RDev	*devices;
    CellDef	*cellDef;
{
    char 	*outfile = cellDef->cd_name;
    Rect	*bbox = &(cellDef->cd_bbox);
    int		numsegsx, numsegsy;
    float	oscale, llx, lly, urx, ury;

    oscale = CIFGetOutputScale(1000);   /* 1000 for conversion to um */
    llx = (float)bbox->r_xbot * oscale;
    lly = (float)bbox->r_ybot * oscale;
    urx = (float)bbox->r_xtop * oscale;
    ury = (float)bbox->r_ytop * oscale;

    fprintf(fp, "* FastHenry output for magic cell %s\n\n", outfile);
    fprintf(fp, ".Units um\n");
    fprintf(fp, ".Default rho=0.02 nhinc=3 nwinc=3 rh=2 rw=2\n\n");
    fprintf(fp, "* Reference plane (substrate, ground)\n");

    fprintf(fp, "Gsub x1=%1.2f y1=%1.2f z1=0 x2=%1.2f y2=%1.2f z2=0\n",
		llx, lly, urx, lly);
    fprintf(fp, "+ x3=%1.2f y3=%1.2f z3=0\n", urx, ury);

    /* Grid the reference plane at 20 lambda intervals.  This	*/
    /* may warrant a more rigorous treatment.  20 is arbitrary.	*/
    /* Minimum number of segments is 4 (also arbitrary).	*/

    numsegsx = (bbox->r_xtop - bbox->r_xbot) / 20;
    numsegsy = (bbox->r_ytop - bbox->r_ybot) / 20;
    if (numsegsx < 4) numsegsx = 4;
    if (numsegsy < 4) numsegsy = 4;

    fprintf(fp, "+ thick=0.1 seg1=%d seg2=%d\n", numsegsx, numsegsy);

    fprintf(fp, "+ Ngp (%1.2f,%1.2f,0)\n", llx, lly);

    fprintf(fp, "\nNsub x=%1.2f y=%1.2f z=0\n", llx, lly);
    fprintf(fp, ".Equiv Nsub Ngp\n");
}

/*
 *-------------------------------------------------------------------------
 * ResCreateCenterlines --
 *	Generate centerline markers on the layout that correspond to
 * 	network routes.  Use the "DBWelement" mechanism.
 *
 * Results:
 *	0 on success, -1 if a window cannot be found.
 *
 * Side effects:
 *	Database "line" elements are generated in the layout.
 *
 *-------------------------------------------------------------------------
 */

int
ResCreateCenterlines(reslist, nidx, def)
    resResistor	*reslist;
    int		*nidx;
    CellDef *def;
{
    resResistor	*resistors;
    resNode *nodeptr;
    Rect r, rcanon;
    MagWindow *w;	/* should be passed from up in CmdExtResis. . . */
    char name[128];

    w = ToolGetBoxWindow (&r,  (int *)NULL);
    if (w == (MagWindow *)NULL)
	windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID)) {
        TxError("Put the cursor in a layout window.\n");
        return -1;
    }

    for (resistors = reslist; resistors; resistors = resistors->rr_nextResistor)
    {
	/* Ignore vias */

	if (!DBIsContact(resistors->rr_tt))
	{
	    nodeptr = resistors->rr_connection1;
	    r.r_xbot = nodeptr->rn_loc.p_x;
	    r.r_ybot = nodeptr->rn_loc.p_y;
	    if (nodeptr->rn_name == NULL)
	    {
		nodeptr->rn_id = (*nidx);
		(*nidx)++;
		sprintf(name, "N%d_", nodeptr->rn_id);
	    }
	    else
		sprintf(name, "N%s_", nodeptr->rn_name);

	    nodeptr = resistors->rr_connection2;
	    r.r_xtop = nodeptr->rn_loc.p_x;
	    r.r_ytop = nodeptr->rn_loc.p_y;
	    GeoCanonicalRect(&r, &rcanon);
	    if (nodeptr->rn_name == NULL)
	    {
		nodeptr->rn_id = (*nidx);
		(*nidx)++;
		sprintf(name + strlen(name), "%d", nodeptr->rn_id);
	    }
	    else
		strcat(name, nodeptr->rn_name);

	    /* Note that if any element exists with name "name"	*/
	    /* it will be deleted (overwritten).		*/
	    DBWElementAddLine(w, name, &rcanon, def, STYLE_YELLOW1);
	}
    }
    return 0;
}

