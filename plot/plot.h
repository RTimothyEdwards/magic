/*
 * plot.h --
 *
 * Contains definitions for things that are exported by the
 * plot module.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/plot/plot.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _PLOT_H
#define _PLOT_H

#include "utils/magic.h"

/* Technology-file reading procedures: */

extern int PlotTechInit();
extern bool PlotTechLine();
extern void PlotTechFinal();

/* Top-level plot procedures: */

#ifdef GREMLIN
extern void PlotGremlin();
#endif
#ifdef VERSATEC
extern void PlotVersatec();

extern void PlotHPRTLHeader();
extern void PlotHPGL2Header();
extern void PlotHPRTLTrailer();
extern void PlotHPGL2Trailer();
extern int  PlotDumpHPRTL();

#endif
extern void PlotPS();
extern void PlotPNM();
extern void PlotSetParam();
extern void PlotPrintParams();

#endif /* _PLOT_H */
