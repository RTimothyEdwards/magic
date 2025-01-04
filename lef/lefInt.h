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

/* Various modes for writing nets. */
#define DO_REGULAR  0
#define DO_SPECIAL  1
#define ALL_SPECIAL 2	/* treat all nets as SPECIALNETS */

/* Used with defMakeInverseLayerMap() */
#define LAYER_MAP_NO_VIAS	FALSE
#define LAYER_MAP_VIAS		TRUE

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

/* Structure used for non-default rules. */

typedef struct _lefRule {
    lefLayer	*lefInfo;	/* Layer or via referenced by the rule */
    int 	width;		/* Non-default width value for layer */
    int		spacing;	/* Non-default spacing value for layer */
    int		extend;		/* Non-default extension value for layer */
    struct _lefRule	*next;
} lefRule;

/* Structure to hold nondefault rules required by nets */

typedef struct {
    char *name;
    lefRule *rule;
} LefRules;

/* Structure holding the counts of regular and special nets */

typedef struct {
    int regular;
    int special;
    int blockages;
    bool has_nets;
} NetCount;

/* Linked string structure used to maintain list of nets to ignore */
typedef struct _linkedNetName {
    char *lnn_name;
    struct _linkedNetName *lnn_next;
} linkedNetName;

/* External declaration of global variables */
extern int lefCurrentLine;
extern HashTable LefInfo;
extern HashTable LefNonDefaultRules;
extern linkedNetName *lefIgnoreNets;

/* Forward declarations */

extern int lefDefInitFunc(CellDef *def);
extern int lefDefPushFunc(CellUse *use, bool *recurse);
extern FILE *lefFileOpen(CellDef *def, const char *file, const char *suffix, const char *mode, char **prealfile);

extern int LefParseEndStatement(FILE *f, const char *match);
extern void LefSkipSection(FILE *f, const char *section);
extern void LefEndStatement(FILE *f);
extern CellDef *lefFindCell(const char *name);
extern char *LefNextToken(FILE *f, bool ignore_eol);
extern char *LefLower(char *token);
extern LinkedRect *LefReadGeometry(CellDef *lefMacro, FILE *f, float oscale, bool do_list, bool is_imported);
extern void LefEstimate(int processed, int total, const char *item_name);
extern lefLayer *LefRedefined(lefLayer *lefl, const char *redefname);
extern void LefAddViaGeometry(FILE *f, lefLayer *lefl, TileType curlayer, float oscale);
extern void LefGenViaGeometry(FILE *f, lefLayer *lefl, int sizex, int sizey, int spacex, int spacey, int encbx,
                              int encby, int enctx, int encty, int rows, int cols, TileType tlayer, TileType clayer,
                              TileType blayer, float oscale);
extern Rect *LefReadRect(FILE *f, TileType curlayer, float oscale);
extern TileType LefReadLayer(FILE *f, bool obstruct);
extern void LefReadLayerSection(FILE *f, const char *lname, int mode, lefLayer *lefl);

extern LefMapping *defMakeInverseLayerMap(bool do_vias);

/* Variable argument procedure requires parameter list. */
extern void LefError(int type, const char *fmt, ...) ATTR_FORMAT_PRINTF_2;

/* C99 compat */
extern void LefRead(const char *inName, bool importForeign, bool doAnnotate, int lefTimestamp);
extern void DefRead(const char *inName, bool dolabels, bool annotate, bool noblockage);

extern void LefWriteAll(CellUse *rootUse, bool writeTopCell, bool lefTech, int lefHide, int lefPinOnly, bool lefTopLayer,
                        bool lefDoMaster, bool recurse);
extern void DefWriteCell(CellDef *def, char *outName, bool allSpecial, int units, bool analRetentive);
extern void LefWriteCell(CellDef *def, char *outName, bool isRoot, bool lefTech, int lefHide, int lefPinOnly,
                         bool lefTopLayer, bool lefDoMaster);
extern int DRCGetDefaultLayerWidth(TileType ttype);
extern void LefTechInit(void);
extern void lefRemoveGeneratedVias(void);

/* Definitions for type passed to LefError() */

#define LEF_ERROR	0
#define LEF_WARNING	1
#define LEF_INFO	2
#define LEF_SUMMARY	3
#define DEF_ERROR	4
#define DEF_WARNING	5
#define DEF_INFO	6
#define DEF_SUMMARY	7

#endif /* _LEFINT_H */
