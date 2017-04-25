/*
 * macros.h --
 *
 * Routines to define and retrieve macros
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/utils/macros.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _MACROS_H
#define _MACROS_H

#include "utils/magic.h"
#include "utils/hash.h"

/*
 * Macro data structure
 */
 
typedef struct {
   char *macrotext;
   bool interactive;
   char *helptext;
} macrodef;

/* macro directory */
extern HashTable MacroClients;

/* procedures */
extern void MacroInit();
extern void MacroDefine();
extern void MacroDefineHelp();
extern void MacroDefineInt();
extern char *MacroRetrieve();	/* returns a malloc'ed string */
extern char *MacroRetrieveHelp(); /* returns a malloc'ed string */
extern char *MacroSubstitute();	/* returns a malloc'ed string */
extern void MacroDelete();
extern char *MacroName();	/* returns a malloc'ed string */
extern int MacroKey();
extern int MacroCode();

#endif /* _MACROS_H */
