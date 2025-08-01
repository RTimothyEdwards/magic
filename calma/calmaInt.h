/*
 * calma.h --
 *
 * This file defines constants used internally by the calma
 * module, but not exported to the rest of the world.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/calma/calmaInt.h,v 1.2 2010/06/24 12:37:15 tim Exp $
 */

#ifndef _MAGIC__CALMA__CALMAINT_H
#define _MAGIC__CALMA__CALMAINT_H

#include "utils/magic.h"
#include "database/database.h"

/* Record data types */
#define CALMA_NODATA	0	/* No data present */
#define CALMA_BITARRAY	1	/* Bit array */
#define CALMA_I2	2	/* 2 byte integer */
#define CALMA_I4	3	/* 4 byte integer */
#define CALMA_R4	4	/* 4 byte real */
#define CALMA_R8	5	/* 8 byte real */
#define CALMA_ASCII	6	/* ASCII string */

/* Record types */
#define CALMA_HEADER		0
#define CALMA_BGNLIB		1
#define CALMA_LIBNAME		2
#define CALMA_UNITS		3
#define CALMA_ENDLIB		4
#define CALMA_BGNSTR		5
#define CALMA_STRNAME		6
#define CALMA_ENDSTR		7
#define CALMA_BOUNDARY		8
#define CALMA_PATH		9
#define CALMA_SREF		10
#define CALMA_AREF		11
#define CALMA_TEXT		12
#define CALMA_LAYER		13
#define CALMA_DATATYPE		14
#define CALMA_WIDTH		15
#define CALMA_XY		16
#define CALMA_ENDEL		17
#define CALMA_SNAME		18
#define CALMA_COLROW		19
#define CALMA_TEXTNODE		20
#define CALMA_NODE		21
#define CALMA_TEXTTYPE		22
#define CALMA_PRESENTATION	23
#define CALMA_SPACING		24
#define CALMA_STRING		25
#define CALMA_STRANS		26
#define CALMA_MAG		27
#define CALMA_ANGLE		28
#define CALMA_UINTEGER		29
#define CALMA_USTRING		30
#define CALMA_REFLIBS		31
#define CALMA_FONTS		32
#define CALMA_PATHTYPE		33
#define CALMA_GENERATIONS	34
#define CALMA_ATTRTABLE		35
#define CALMA_STYPTABLE		36
#define CALMA_STRTYPE		37
#define CALMA_ELFLAGS		38
#define CALMA_ELKEY		39
#define CALMA_LINKTYPE		40
#define CALMA_LINKKEYS		41
#define CALMA_NODETYPE		42
#define CALMA_PROPATTR		43
#define CALMA_PROPVALUE		44
#define CALMA_BOX		45
#define CALMA_BOXTYPE		46
#define CALMA_PLEX		47
#define CALMA_BGNEXTN		48
#define CALMA_ENDEXTN		49
#define CALMA_TAPENUM		50
#define CALMA_TAPECODE		51
#define CALMA_STRCLASS		52
#define CALMA_RESERVED		53
#define CALMA_FORMAT		54
#define CALMA_MASK		55
#define CALMA_ENDMASKS		56
#define CALMA_LIBDIRSIZE	57
#define CALMA_SRFNAME		58
#define CALMA_LIBSECUR		59

#define	CALMA_NUMRECORDTYPES	60	/* Number of above types */

/* Property types defined for magic */
#define CALMA_PROP_USENAME_STD	61	/* To record non-default cell use ids */
#define CALMA_PROP_USENAME	98	/* To record non-default cell use ids */
#define CALMA_PROP_ARRAY_LIMITS	99	/* To record non-default array limits */

/* Flags for transforms */
#define	CALMA_STRANS_UPSIDEDOWN	0x8000	/* Mirror about X axis before rot */
#define	CALMA_STRANS_ROTATE	0x0002	/* Rotate by absolute angle */

/* Path types */
#define	CALMAPATH_SQUAREFLUSH	0	/* Square end flush with endpoint */
#define	CALMAPATH_ROUND		1	/* Round end */
#define	CALMAPATH_SQUAREPLUS	2	/* Square end plus half-width extent */
#define	CALMAPATH_CUSTOM	4	/* Endcaps at specified lengths */

/* Largest calma layer or data type numbers */
#define	CALMA_LAYER_MAX	255

#define	CalmaIsValidLayer(n)	((n) >= 0 && (n) <= CALMA_LAYER_MAX)

/* Used to index hash tables of (layer, datatype) pairs */
typedef struct
{
    int		 clt_layer;
    int		 clt_type;
} CalmaLayerType;

/* Biggest calma string */
#define	CALMANAMELENGTH		32

/* Length of record header */
#define	CALMAHEADERLENGTH	4

/* Label types
 *  The intention is all the values can be stored/converted with unsigned char type,
 *  C23 allows us to be explicit with the type but C99 does not, so a comment for now.
 */
typedef enum /* : unsigned char */ { LABEL_TYPE_NONE, LABEL_TYPE_TEXT, LABEL_TYPE_PORT, LABEL_TYPE_CELLID } labelType;

/* ------------------------- Input macros ----------------------------- */

/* Globals for Calma reading */
extern FILETYPE calmaInputFile;
extern FILE *calmaInputFileNoCompression;
extern char *calmaFilename;
extern int  calmaReadScale1;
extern int  calmaReadScale2;
extern bool calmaLApresent;
extern int  calmaLAnbytes;
extern int  calmaLArtype;

/*
 * Macros for number representation conversion.
 */
#ifdef ibm032
#include <netinet/in.h>    /* as macros in in.h and don't exist as routines */
#endif

#ifndef	ntohl
# ifdef	WORDS_BIGENDIAN
# define ntohl(x)        (x)
# define ntohs(x)        (x)
# define htonl(x)        (x)
# define htons(x)        (x)
# endif
#endif

typedef union { char uc[2]; unsigned short us; } TwoByteInt;
typedef union { char uc[4]; unsigned int ul; } FourByteInt;

/* Macro to read a 2-byte integer */
#define	READI2(z) \
	{ \
            TwoByteInt u; \
            u.uc[0] = FGETC(calmaInputFile); \
            u.uc[1] = FGETC(calmaInputFile); \
            (z) = (int) ntohs(u.us); \
	}

/* Macro to read a 4-byte integer */
#define	READI4(z) \
	{ \
            FourByteInt u; \
            u.uc[0] = FGETC(calmaInputFile); \
            u.uc[1] = FGETC(calmaInputFile); \
            u.uc[2] = FGETC(calmaInputFile); \
            u.uc[3] = FGETC(calmaInputFile); \
            (z) = (int) ntohl(u.ul); \
	}

/* Macros for reading and unreading record headers */
#define READRH(nb, rt) \
	{ \
	    if (calmaLApresent) { \
		(nb) = calmaLAnbytes; \
		(rt) = calmaLArtype; \
		calmaLApresent = FALSE; \
	    } else { \
		READI2(nb); \
		if (FEOF(calmaInputFile)) nb = -1; \
		else { \
		    (rt) = FGETC(calmaInputFile); \
		    (void) FGETC(calmaInputFile); \
		} \
	    } \
	}

#define UNREADRH(nb, rt) \
	{ \
	    ASSERT(!calmaLApresent, "UNREADRH"); \
	    calmaLApresent = TRUE; \
	    calmaLAnbytes = (nb); \
	    calmaLArtype = (rt); \
	}

#define	PEEKRH(nb, rt) \
	{ \
	    READRH(nb, rt); \
	    UNREADRH(nb, rt); \
	}

/* Structure used for sorting ports by number */

typedef struct portlabel
{
    Label         *pl_label;
    unsigned int   pl_port;
} PortLabel;

/* Other commonly used globals */
extern HashTable calmaLayerHash;
extern const int calmaElementIgnore[];
extern CellDef *calmaFindCell(const char *name, bool *was_called, bool *predefined);

/* (Added by Nishit, 8/18/2004--8/24/2004) */
extern CellDef *calmaLookCell(char *name);
extern void calmaWriteContacts(FILE *f);
extern CellDef *calmaGetContactCell(TileType type, bool lookOnly);
extern bool calmaIsContactCell;

extern const char *calmaRecordName(int rtype);
extern void calmaSkipSet(const int *skipwhat);
extern bool calmaParseUnits(void);

extern int compport(const void *one, const void *two);


#define LB_EXTERNAL	0	/* Polygon external edge	*/
#define LB_INTERNAL	1	/* Polygon internal edge	*/
#define LB_INIT		2	/* Data not yet valid		*/

typedef struct LB1 {
    char lb_type;		/* Boundary Type (external or internal) */
    Point lb_start;		/* Start point */
    struct LB1 *lb_next;	/* Next point record */
} LinkedBoundary;

typedef struct BT1 {
    LinkedBoundary *bt_first;   /* Polygon list */
    int bt_points;		/* Number of points in this list */
    struct BT1 *bt_next;	/* Next polygon record */
} BoundaryTop;

extern int calmaAddSegment(LinkedBoundary **lbptr, bool poly_edge, int p1x, int p1y, int p2x, int p2y);
extern void calmaMergeSegments(LinkedBoundary *edge, BoundaryTop **blist, int num_points);
extern void calmaRemoveDegenerate(BoundaryTop *blist);
extern void calmaRemoveColinear(BoundaryTop *blist);

/* ------------------- Imports from CIF reading ----------------------- */

extern CellDef *cifReadCellDef;
extern Plane *cifSubcellPlanes[];
extern Plane **cifCurReadPlanes;
extern HashTable CifCellTable;
extern Plane *cifEditCellPlanes[];

#endif /* _MAGIC__CALMA__CALMAINT_H */
