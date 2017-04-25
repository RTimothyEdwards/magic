/*
 * netmenu.h --
 *
 * Defines the interface provided by the netmenu module.
 * This module implements a menu-based system for editing
 * labels and netlists.
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
 *
 * rcsid = "$Header: /usr/cvsroot/magic-8.0/netmenu/netmenu.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
 */

#ifndef _NETMENU_H
#define _NETMENU_H

#include "utils/magic.h"

/* data structures */

/* procedures */

extern void NMinit();
extern void NMUnsetCell();
extern void NMNewNetlist();
extern void NMWriteNetlist();
extern char *NMAddTerm();
extern void NMDeleteTerm();
extern void NMJoinNets();
extern void NMDeleteNet();
extern int NMEnumNets();
extern int NMEnumTerms();
extern char *NMNthNet();
extern char *NMTermInList();
extern int NMVerify();
extern bool NMHasList();
extern void NMFlushNetlist();
extern char *NMNetlistName();

/* button functions, now exported to the command-line "netlist" command */
extern void NMButtonLeft();
extern void NMButtonMiddle();
extern void NMButtonRight();

#ifdef ROUTE_MODULE
extern void NMMeasureAll();
#endif

#endif /* _NETMENU_H */
