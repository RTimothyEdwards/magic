/*
 * wiring.h --
 *
 * Contains definitions for things that are exported by the
 * wiring module.
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
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/wiring/wiring.h,v 1.2 2008/09/05 13:56:25 tim Exp $
 */

#ifndef _WIRING_H
#define _WIRING_H

#include "utils/magic.h"
#include "database/database.h"

/* Table that defines the shape of contacts and the layers that they
 * connect.  This definition allows some layers to extend around the
 * contact, to support technologies where the contact pads are different
 * sizes for the different layers that the contact connects.
 */

typedef struct _Contact *ContactPtr;

typedef struct _Contact
{
    TileType con_type;		/* Type of material that forms core of
				 * contact.
				 */
    int con_size;		/* Minimum size of this contact (size of
				 * minimum con_type area).
				 */
    TileType con_layer1;	/* First of two layers that the contact
				 * really isn't a contact.
				 */
    int con_surround1;		/* How much additional material of type
				 * con_layer1 must be painted around the
				 * edge of the contact.
				 */
    TileType con_layer2;	/* Same information for second layer that
				 * the contact connects.
				 */
    int con_surround2;

    ContactPtr con_next;	/* Pointer to next contact record */
} Contact;

extern Contact *WireContacts;	/* Points to all the contacts that are
				 * defined. */

/* Types defining the current state of the wiring tool */
extern TileType WireType;	/* Type of material currently selected
				 * for wiring.
				 */
extern int WireWidth;		/* Thickness of material to use for wiring. */

/* Procedures for placing wires: */

extern void WirePickType();
extern void WireAddLeg();
extern void WireAddContact();
extern void WireShowLeg();
extern int WireGetWidth();
extern TileType WireGetType();

/* Legal values for the "direction" parameter to WireAddLeg: */

#define WIRE_CHOOSE 0
#define WIRE_HORIZONTAL 1
#define WIRE_VERTICAL 2

/* Procedures for reading the technology file: */

extern void WireTechInit();
extern bool WireTechLine();
extern void WireTechFinal();
extern void WireTechScale();

/* Initialization: */

extern void WireInit();

#endif /* _WIRING_H */
