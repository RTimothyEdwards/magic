/*
 * wireTech.c --
 *
 * This file contains procedures that parse the wiring sections of
 * technology files.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/wiring/wireTech.c,v 1.2 2009/05/13 15:03:19 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "wiring/wiring.h"
#include "utils/malloc.h"

/* Linked list to store contact information collected by this module: */
Contact *WireContacts;


/*
 * ----------------------------------------------------------------------------
 *	WireTechInit --
 *
 * 	Called once at beginning of technology file read-in to initialize
 *	data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out the contact table.
 * ----------------------------------------------------------------------------
 */

void
WireTechInit()
{
    Contact *contact;
    int i;

    while (WireContacts != NULL)
    {
	freeMagic((char *) WireContacts);
	WireContacts = WireContacts->con_next;
    }
}

/*
 * ----------------------------------------------------------------------------
 *	WireTechLine --
 *
 * 	This procedure is invoked by the technology module once for
 *	each line in the "wiring" section of the technology file.
 *
 * Results:
 *	Always returns TRUE (otherwise the technology module would
 *	abort Magic with a fatal error).
 *
 * Side effects:
 *	Builds up the contact table, prints error messages if necessary.
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
bool
WireTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section (unused). */
    int argc;			/* Number of arguments on line. */
    char *argv[];		/* Pointers to fields of line. */
{
    Contact *new;

    if (strcmp(argv[0], "contact") != 0)
    {
	TechError("Unknown wiring keyword: %s.  Line ignored.\n", argv[0]);
	return TRUE;
    }
    if (argc != 7)
    {
	TechError("\"contact\" lines must have exactly 7 arguments.\n");
	return TRUE;
    }

    new = (Contact *) mallocMagic(sizeof(Contact));
    new->con_type = DBTechNoisyNameType(argv[1]);
    new->con_layer1 = DBTechNoisyNameType(argv[3]);
    new->con_layer2 = DBTechNoisyNameType(argv[5]);
    if ((new->con_type < 0) || (new->con_layer1 < 0) || (new->con_layer2 < 0))
    {
	errorReturn:
	freeMagic((char *) new);
	return TRUE;
    }

    if (!StrIsInt(argv[2]))
    {
	TechError("3rd field must be an integer.\n");
	goto errorReturn;
    }
    else new->con_size = atoi(argv[2]);
    if (!StrIsInt(argv[4]))
    {
	TechError("5th field must be an integer.\n");
	goto errorReturn;
    }
    else new->con_surround1 = atoi(argv[4]);
    if (!StrIsInt(argv[6]))
    {
	TechError("6th field must be an integer.\n");
	goto errorReturn;
    }
    else new->con_surround2 = atoi(argv[6]);

    new->con_next = WireContacts;
    WireContacts = new;

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *	WireTechFinal --
 *
 * 	This procedure is called by the technology module after all the
 *	lines of the tech file have been read.  It doesn't do anything
 *	right now.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
WireTechFinal()
{
    /* Debugging code to print info about layers: */

/*
    Contact *con;

    for (con = WireContacts; con != NULL; con = con->con_next)
    {
	TxPrintf("Contact type \"%s\", size %d connects\n",
	    DBTypeLongName(con->con_type), con->con_size);
	TxPrintf("    \"%s\" (overlap %d) and\n",
	    DBTypeLongName(con->con_layer1), con->con_surround1);
	TxPrintf("    \"%s\" (overlap %d)\n",
	    DBTypeLongName(con->con_layer2), con->con_surround2);
    }
*/
}

/*
 *----------------------------------------------------------------------------
 * WireTechScale --
 *
 *	Change parameters of the wiring section as required when
 *	redefining magic's internal grid relative to the technology lambda.
 *
 *----------------------------------------------------------------------------
 */

void
WireTechScale(scalen, scaled)
    int scalen, scaled;
{
    Contact *con;

    for (con = WireContacts; con != NULL; con = con->con_next)
    {
	con->con_size *= scaled;
	con->con_size /= scalen;

	con->con_surround1 *= scaled;
	con->con_surround1 /= scalen;

	con->con_surround2 *= scaled;
	con->con_surround2 /= scalen;
    }
}
