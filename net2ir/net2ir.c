/*
 * net2ir --
 *
 * Given a feedback file produced by the Magic :find command (from
 * the netlist menu) followed by :feed save, giving label locations and 
 * layers, and a netlist
 * file, produce a set of irouter commands to route the two-point
 * nets in the order in which they appear in the netlist file.
 *
 * Usage:
 *	net2ir feedfile netfile
 *
 * Produces the commands on its standard output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/net2ir/net2ir.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif /* lint */

#define	INITHASHSIZE	128
#define	LINESIZE	1024

/*
 * Hash table of all feedback information giving label locations.
 * Keyed by the label name; the contents are a structure giving
 * the label location and its layer.
 */
HashTable feedHash;

typedef struct
{
    char *li_layer;
    char *li_label;
    Rect  li_area;
} LabInfo;

int
main(argc, argv)
    char *argv[];
{
    char line1[LINESIZE], line2[LINESIZE], layer[LINESIZE], label[LINESIZE];
    HashEntry *he;
    LabInfo *li;
    int nterms;
    FILE *fp;
    char *cp;
    Rect r;

    if (argc != 3)
    {
	fprintf(stderr, "Usage: net2ir feedfile netfile\n");
	exit (1);
    }

    /* Process the feedback file, building the hash table of label locs */
    HashInit(&feedHash, INITHASHSIZE, HT_STRINGKEYS);
    fp = fopen(argv[1], "r");
    if (fp == NULL)
    {
	perror(argv[1]);
	exit (1);
    }

    while (fgets(line1, sizeof line1, fp))
    {
getfirst:
	if (sscanf(line1, "box %d %d %d %d", &r.r_xbot, &r.r_ybot,
		&r.r_xtop, &r.r_ytop) != 4)
	    continue;
	if (fgets(line2, sizeof line2, fp) == NULL)
	    break;
	if (sscanf(line2, "feedback add \"%[^;];%[^\"]", layer, label) != 2)
	{
	    strcpy(line1, line2);
	    goto getfirst;
	}

	he = HashFind(&feedHash, label);
	if (HashGetValue(he))
	{
	    fprintf(stderr,
		"Warning: multiple locs for label %s; 2nd loc ignored.\n",
		label);
	    continue;
	}
	li = (LabInfo *) mallocMagic((unsigned) (sizeof (LabInfo)));
	GEO_EXPAND(&r, -1, &li->li_area);
	li->li_label = StrDup((char **) NULL, label);
	li->li_layer = StrDup((char **) NULL, layer);
	HashSetValue(he, (ClientData) li);
    }
    (void) fclose(fp);

    /* Process the net file */
    fp = fopen(argv[2], "r");
    if (fp == NULL)
    {
	perror(argv[2]);
	exit (1);
    }

    nterms = 0;
    while (fgets(line1, sizeof line1, fp))
    {
	if (isspace(line1[0]) || line1[0] == '\0')
	{
	    nterms = 0;
	    continue;
	}

	if (cp = strchr(line1, '\n'))
	    *cp = '\0';

	if (nterms >= 2)
	{
	    fprintf(stderr, "Net with >2 terms ignored: %s\n", line1);
	    continue;
	}

	he = HashLookOnly(&feedHash, line1);
	if (he == NULL || (li = (LabInfo *) HashGetValue(he)) == NULL)
	{
	    fprintf(stderr, "No location for terminal %s\n", line1);
	    continue;
	}

	if(nterms == 0)
	{
	    printf(":iroute route -slayers %s -sPoint %d %d ",
		   li->li_layer, 
		   li->li_area.r_xbot, 
		   li->li_area.r_ybot);
	}
	else
	{
	    printf("-dlayers %s -dRect %d %d %d %d\n",
		   li->li_layer, 
		   li->li_area.r_xbot, 
		   li->li_area.r_ybot,
		   li->li_area.r_xtop,
		   li->li_area.r_ytop);
	}
	nterms++;
    }
    exit(0);
}

