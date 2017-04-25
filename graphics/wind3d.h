/*
 * wind3d.h --
 *
 *	Interface definitions for the 'glue' between the window
 *	manager and the 3D rendering window
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 */

#ifndef _WIND3D_H
#define _WIND3D_H

#include "windows/windows.h"

/* Data structures */

typedef struct
{
    float	view_x;		/* View angle around x axis */
    float	view_y;		/* View angle around y axis */
    float	view_z;		/* View angle around z axis */

    float	trans_x;	/* Translation in x */
    float	trans_y;	/* Translation in y */
    float	trans_z;	/* Translation in z */

    float	scale_xy;	/* Size scaling */
    float	prescale_z;	/* Depth scaling */
    float	scale_z;	/* Depth scaling */

    int		width;		/* Window width */
    int		height;		/* Window height */

    int		level;		/* rendering level (0 = coarse) */

    bool	cif;		/* If TRUE, draw CIF layers */

    bool	clipped;	/* If TRUE, set clipping */
    Rect	cutbox;		/* Clip to this box, if clipping is set */

    TileTypeBitMask visible;	/* Layers to see */

    MagWindow	*boxWin;	/* Render what's in this window */
} W3DclientRec;

/* Exported procedures */

extern void w3dSetProjection();
extern void W3Dcommand();
extern void W3Dinit();

/* Command procedures */

extern void w3dView();
extern void w3dScroll();
extern void w3dZoom();
extern void w3dRefresh();
extern void w3dToggleCIF();
extern void w3dDefaults();
extern void w3dLevel();
extern void w3dClose();
extern void w3dRenderValues();
extern void w3dSeeLayers();
extern void w3dHelp();
extern void w3dCutBox();

extern WindClient W3DclientID;

#endif /* _WIND3D_H */
