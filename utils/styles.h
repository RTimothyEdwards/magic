/*
 * styles.h --
 *
 * Definitions of styles used for system purposes.
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
 * rcsid:  $Header: /usr/cvsroot/magic-8.0/utils/styles.h,v 1.2 2008/12/11 04:20:14 tim Exp $
 */

#ifndef _STYLES_H
#define _STYLES_H 1

/* Styles are divided into three parts: 1) system styles, used for
 * purposes such as drawing and erasing tools, window borders, etc.,
 * 2) Styles used to draw the layout, and 3) "Pale" styles duplicating
 * all entries in (2) for drawing areas of the layout not currently
 * begin edited.
 *
 * The system styles are predefined, so they can be accessed from the
 * source code by preprocessor macros.
 *
 * STYLE_SOLIDHIGHLIGHTS:    used to draw solid highlight areas.
 * STYLE_MEDIUMHIGHLIGHTS:   used to draw highlights in a medium-weight stipple.
 * STYLE_PALEHIGHLIGHTS:     used to draw highlights in a pale stipple.
 * STYLE_HORIZHIGLIGHTS:     used to draw highlights with horizontal lines.
 * STYLE_VERTHIGLIGHTS:      used to draw highlights with vertical lines.
 * STYLE_OUTLINEHIGHLIGHTS:  used to draw highlights as solid box outlines.
 * STYLE_DOTTEDHIGLIGHTS:    used to draw highlights with dotted box outlines.
 * STYLE_ERASEBOX:	     used to erase the box from the screen.
 * STYLE_ERASEHIGHLIGHTS:    used to erase all highlights from an area.
 * STYLE_ERASEALL:	     used to erase all information from the screen.
 * STYLE_ERASEALLBUTTOOLS:   used to erase everything but the tools.
 * STYLE_LABEL:		     used to draw labels.
 * STYLE_PORT:		     used to draw ports.
 * STYLE_PORT_CONNECT:	     used to draw the connecting boundary on ports.
 * STYLE_BBOX:		     used to draw cell bounding boxes and names.
 * STYLE_GRID:		     used to draw grid on the screen (dotted lines).
 * STYLE_SOLIDGRID:	     an alternate grid, drawn with solid lines.
 * STYLE_ORIGIN:	     used to display a box at origin of edit cell.
 * STYLE_DRAWTILE:	     used to draw tiles for *watch command.
 * STYLE_BORDER:	     used to draw borders around windows.
 * STYLE_ELEVATOR	     used by the window package to draw the elevator
 *			     (slug) in the scroll bars.
 * STYLE_CAPTION:	     used to draw window captions.
 * STYLE_BACKGROUND:	     used to draw the areas outside of windows.
 * STYLE_CMEDIT		     used to display the color being edited by the
 *			     colormap editor.
 * STYLE_WHITE, etc.	     used to generate common colors, for use in
 *			     menus.  Most colors have several styles,
 *			     corresponding to different saturation levels.
 *			     These colors are numbered where 1 corresponds
 *			     to a very pale color and a large number is used
 *			     for a highly saturated (rich) color.  Unnumbered
 *			     colors correspond to the most saturated ones.
 *
 * If any of the style numbers below are changed, all of the display styles
 * files must be modified so that they agree.
 */

/* Transparency */

#define STYLE_TRANSPARENT	0

/* Styles for drawing and erasing highlights: */

#define STYLE_SOLIDHIGHLIGHTS	1
#define STYLE_MEDIUMHIGHLIGHTS	2
#define STYLE_PALEHIGHLIGHTS	3
#define STYLE_HORIZHIGHLIGHTS	4
#define STYLE_VERTHIGHLIGHTS	5
#define STYLE_OUTLINEHIGHLIGHTS	6
#define STYLE_DOTTEDHIGHLIGHTS	7
#define	STYLE_ERASEBOX		8
#define STYLE_ERASEHIGHLIGHTS	9
#define	STYLE_ERASEALL		10
#define	STYLE_ERASEALLBUTTOOLS	11

/* Other miscellaneous styles */

#define	STYLE_LABEL		12
#define	STYLE_PORT		13
#define	STYLE_PORT_CONNECT	14
#define	STYLE_BBOX		15
#define	STYLE_GRID		16
#define	STYLE_SOLIDGRID		17
#define STYLE_ORIGIN		18
#define	STYLE_DRAWTILE		19
#define	STYLE_BORDER		20
#define STYLE_ELEVATOR		21
#define	STYLE_CAPTION		22
#define	STYLE_BACKGROUND	23
#define STYLE_CMEDIT		24

/* Colors used by Tcl/Tk */

#define TK_MEDIUM_GRAY		25
#define TK_LIGHT_GRAY		26

#define	STYLE_LABEL_PALE	27
#define	STYLE_PORT_PALE		28

/* Assortment of solid colors */

#define	STYLE_WHITE		29
#define STYLE_GRAY1		30
#define STYLE_GRAY2		31
#define STYLE_GRAY		31
#define STYLE_BLACK		32
#define STYLE_RED1		33
#define STYLE_PINK		33
#define STYLE_RED2		34
#define STYLE_RED3		35
#define	STYLE_RED		35
#define STYLE_GREEN1		36
#define STYLE_GREEN2		37
#define STYLE_GREEN3		38
#define	STYLE_GREEN		38
#define STYLE_BLUE1		39
#define STYLE_BLUE2		40
#define STYLE_BLUE3		41
#define	STYLE_BLUE		41
#define STYLE_PURPLE1		42
#define STYLE_PURPLE2		43
#define STYLE_PURPLE		43
#define STYLE_YELLOW1		44
#define STYLE_YELLOW2		45
#define STYLE_YELLOW		45
#define STYLE_ORANGE1		46
#define STYLE_ORANGE2		47
#define STYLE_ORANGE		47
#define STYLE_BROWN1		48
#define STYLE_BROWN2		49
#define STYLE_BROWN		49
#define STYLE_MAGENTA		50
#define STYLE_CYAN		51

/* Mark the number after all the built-in styles as the	*/
/* beginning of the styles used by the technology files	*/

#define TECHBEGINSTYLES		52


/*
 * Here are the cursors defined in the standard styles file.
 *
 */

/* misc patterns */
#define	STYLE_CURS_NORMAL	0
#define	STYLE_CURS_STAR		1

/* corners */
#define	STYLE_CURS_LLCORNER	2
#define	STYLE_CURS_LRCORNER	3
#define	STYLE_CURS_ULCORNER	4
#define	STYLE_CURS_URCORNER	5

/* boxes */
#define	STYLE_CURS_LLBOX	6
#define	STYLE_CURS_LRBOX	7
#define	STYLE_CURS_ULBOX	8
#define	STYLE_CURS_URBOX	9

/* entire windows */
#define	STYLE_CURS_LLWIND	10
#define	STYLE_CURS_LRWIND	11
#define	STYLE_CURS_ULWIND	12
#define	STYLE_CURS_URWIND	13

/* corners of windows */
#define	STYLE_CURS_LLWINDCORN	14
#define	STYLE_CURS_LRWINDCORN	15
#define	STYLE_CURS_ULWINDCORN	16
#define	STYLE_CURS_URWINDCORN	17

/* netlist editing icon */
#define STYLE_CURS_NET		18

/* wiring icon */
#define STYLE_CURS_ARROW	19

/* irouting icon */
#define STYLE_CURS_IROUTE	20

/* rsim icon */
#define STYLE_CURS_RSIM		21

/* pick icon */
#define STYLE_CURS_PICK		22

#endif /* _STYLES_H */
