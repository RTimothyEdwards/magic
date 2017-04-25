/*
 * lefInt.h --
 *
 * This file defines things that are used by internal LEF routines in
 * various files.
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/lef/lefInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _LEFINT_H
#define _LEFINT_H

#include "utils/magic.h"

/* Some constants for LEF and DEF files */

#define LEF_LINE_MAX 2048  /* Maximum length fixed by LEF/DEF specifications */
#define LEF_MAX_ERRORS 100 /* Max # errors to report; limits output if */
                           /* something is really wrong about the file */

#define DEFAULT_WIDTH 3	   /* Default metal width for routes if undefined */
#define DEFAULT_SPACING 4  /* Default spacing between metal if undefined  */

/* Structure holding the counts of regular and special nets */

typedef struct
{
    int regular;
    int special;
    bool has_nets;
} NetCount;

/* Various modes for writing nets. */
#define DO_REGULAR  0
#define DO_SPECIAL  1
#define ALL_SPECIAL 2	/* treat all nets as SPECIALNETS */

/* For a linked list of rectangular areas, use the LinkedRect structure	*/
/* defined in utils/geometry.h.						*/

/* Structure used for returning information about polygon geometry */

typedef struct _linkedPoint {
   Point     pos;
   TileType  type;
   struct _linkedPoint *point_next;
} linkedPoint;

/* Structure used to maintain default routing information for each	*/
/* routable layer type.							*/

typedef struct {
    int width;		/* width, in lambda */
    int  spacing;	/* minimum spacing rule, in lambda */
    int	 pitch;		/* route pitch, in lambda */
    bool hdirection;	/* horizontal direction preferred */
} lefRoute;

/* Structure used to maintain default generation information for each	*/
/* via or viarule (contact) type.  If "cell" is non-NULL, then the via	*/
/* is saved in a cell (pointed to by "cell"), and "area" describes the	*/
/* bounding box.  Otherwise, the via is formed by magic type "type"	*/
/* with a minimum area "area" for a single contact.			*/

typedef struct {
    Rect	area;		/* Area of single contact, or cell bbox	*/
				/* in units of 1/2 lambda		*/
    CellDef	*cell;		/* Cell for fixed via def, or NULL	*/
    LinkedRect	*lr;		/* Extra information for vias with	*/
				/* more complicated geometry.		*/
    TileType	obsType;	/* Secondary obstruction type		*/
} lefVia;

/* Defined types for "lefClass" in the lefLayer structure */

#define CLASS_ROUTE	0	/* routing layer */
#define CLASS_VIA	1	/* via or cut layer */
#define CLASS_MASTER	2	/* masterslice layer */
#define CLASS_OVERLAP	3	/* overlap layer */
#define CLASS_BOUND	4	/* boundary-defining layer */
#define CLASS_IGNORE	5	/* inactive layer */

/* Structure defining a route or via layer and matching it to a magic	*/
/* layer type.  This structure is saved in the LefInfo hash table.	*/
/* To allow multiple names to refer to the same structure, we keep a	*/
/* reference count of the number of times a hash table value points to	*/
/* this structure.							*/

typedef struct {
    TileType	  type;		/* magic tile type, or -1 for none */
    TileType	  obsType;	/* magic type to use if this is an obstruction */
    short	  refCnt;	/* reference count for memory deallocation */
    char *	  canonName;	/* name to use when writing LEF output */
    unsigned char lefClass;	/* is this a via, route, or masterslice layer */
    union {
	lefRoute  route;	/* for route layers */
	lefVia	  via;		/* for contacts */
    } info;
} lefLayer;

/* Inverse mapping structure for returning the primary LEF	*/
/* layer corresponding to a magic tile type.			*/

typedef struct {
    char *lefName;              /* Primary name of LEF layer */
    lefLayer *lefInfo;          /* Pointer to information about the layer */
} LefMapping;

/* External declaration of global variables */
extern int lefCurrentLine;
extern HashTable LefInfo;

/* Forward declarations */

int lefDefInitFunc(), lefDefPushFunc();
FILE *lefFileOpen();

char *LefGetInput();
int LefParseEndStatement();
void LefSkipSection();
void LefEndStatement();
CellDef *lefFindCell();
char *LefNextToken();
char *LefLower();
LinkedRect *LefReadGeometry();
void LefEstimate();
lefLayer *LefRedefined();
void LefAddViaGeometry();
Rect *LefReadRect();
TileType LefReadLayer();

LefMapping *defMakeInverseLayerMap();

void LefError(char *, ...);	/* Variable argument procedure requires */
				/* parameter list.			*/

#endif /* _LEFINT_H */
