/*
 * geofast.h --
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
 * This file contains macros for fast geometrical operations:
 * clipping and transforms.  For transforms, it works when they
 * all have unit scale factors, so we can avoid multiplication.
 * This wins a factor of 5 on machines like Suns.  In addition,
 * of course, each is a macro rather than a procedure call;
 * the net effect is about a 5x speedup on a VAX and a 7-8x
 * speedup on a Sun.
 */

/* rcsid "$Header: /usr/cvsroot/magic-8.0/utils/geofast.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $" */

/* ---------------------------- Clipping ------------------------------ */

/*
 * GEOCLIP(r, area) Rect *r, area;
 * clips the rectangle 'r' against the area 'area'.
 */

#define	GEOCLIP(r, area) \
    if (1) { \
	if ((r)->r_xbot < (area)->r_xbot) (r)->r_xbot = (area)->r_xbot; \
	if ((r)->r_ybot < (area)->r_ybot) (r)->r_ybot = (area)->r_ybot; \
	if ((r)->r_xtop > (area)->r_xtop) (r)->r_xtop = (area)->r_xtop; \
	if ((r)->r_ytop > (area)->r_ytop) (r)->r_ytop = (area)->r_ytop; \
    } else

/* --------------------- Transforming rectangles ---------------------- */

/*
 * The GEOTRANSRECT(t, r1, r2) macro has the same effect as the
 * following code.  It assumes that t_a, t_b, t_d, t_e are all
 * chosen from -1, 0, 1, so it can't handle scaling.
 *
 *	int x1, y1, x2, y2;
 *	x1 = r1->r_xbot*t->t_a + r1->r_ybot*t->t_b + t->t_c;
 *	y1 = r1->r_xbot*t->t_d + r1->r_ybot*t->t_e + t->t_f;
 *	x2 = r1->r_xtop*t->t_a + r1->r_ytop*t->t_b + t->t_c;
 *	y2 = r1->r_xtop*t->t_d + r1->r_ytop*t->t_e + t->t_f;
 *
 *	if (x1 < x2) r2->r_xbot = x1, r2->r_xtop = x2;
 *	else r2->r_xbot = x2, r2->r_xtop = x1;
 *
 *	if (y1 < y2) r2->r_ybot = y1, r2->r_ytop = y2;
 *	else r2->r_ybot = y2, r2->r_ytop = y1;
 *
 * We make use of the fact that if t_a != 0 for one of our transforms,
 * then t_e != 0 also, and t_b = t_d = 0.
 */

#define	transRectX(r1, r2, RBOT, RTOP, ta, tc) \
	if (ta > 0) \
	    r2->r_xbot = r1->RBOT + tc, r2->r_xtop = r1->RTOP + tc; \
	else \
	    r2->r_xtop = tc - r1->RBOT, r2->r_xbot = tc - r1->RTOP;

#define	transRectY(r1, r2, RBOT, RTOP, ta, tc) \
	if (ta > 0) \
	    r2->r_ybot = r1->RBOT + tc, r2->r_ytop = r1->RTOP + tc; \
	else \
	    r2->r_ytop = tc - r1->RBOT, r2->r_ybot = tc - r1->RTOP;

#define	GEOTRANSRECT(at, ar1, ar2) \
    if (1) { \
	Transform *xt = (at); \
	Rect *xr1 = (ar1), *xr2 = (ar2); \
	if (xt->t_a) \
	{ \
	    transRectX(xr1, xr2, r_xbot, r_xtop, xt->t_a, xt->t_c); \
	    transRectY(xr1, xr2, r_ybot, r_ytop, xt->t_e, xt->t_f); \
	} \
	else \
	{ \
	    transRectX(xr1, xr2, r_ybot, r_ytop, xt->t_b, xt->t_c); \
	    transRectY(xr1, xr2, r_xbot, r_xtop, xt->t_d, xt->t_f); \
	} \
    } else

/* --------------------- Transforming transforms ---------------------- */

/*
 * The GEOTRANSTRANS(first, second, net) macro has the same effect
 * as the following code.  It assumes that the t_a, t_b, t_d, and t_f
 * fields of 'second' are chosen from -1, 0, 1, so it can't handle scaling.
 *
 * net->t_a = first->t_a*second->t_a + first->t_d*second->t_b;
 * net->t_b = first->t_b*second->t_a + first->t_e*second->t_b;
 * net->t_c = first->t_c*second->t_a + first->t_f*second->t_b + second->t_c;
 * net->t_d = first->t_a*second->t_d + first->t_d*second->t_e;
 * net->t_e = first->t_b*second->t_d + first->t_e*second->t_e;
 * net->t_f = first->t_c*second->t_d + first->t_f*second->t_e + second->t_f;
 */

#define	transTransAC(t1, net, ta, tc, da, db, dc) \
    ((ta > 0) \
	? (net->t_a = t1->da, net->t_b = t1->db, net->t_c = t1->dc + tc) \
	: (net->t_a = -t1->da, net->t_b = -t1->db, net->t_c = tc - t1->dc))

#define	transTransDF(t1, net, ta, tc, da, db, dc) \
    ((ta > 0) \
	? (net->t_d = t1->da, net->t_e = t1->db, net->t_f = t1->dc + tc) \
	: (net->t_d = -t1->da, net->t_e = -t1->db, net->t_f = tc - t1->dc))

#define	GEOTRANSTRANS(xt1, xt2, xnet) \
	if (1) { \
	    Transform *_t1 = (xt1), *_t2 = (xt2), *_net = (xnet); \
	    if (_t2->t_a) \
		transTransAC(_t1, _net, _t2->t_a, _t2->t_c, t_a, t_b, t_c); \
	    else \
		transTransAC(_t1, _net, _t2->t_b, _t2->t_c, t_d, t_e, t_f); \
	    if (_t2->t_d) \
		transTransDF(_t1, _net, _t2->t_d, _t2->t_f, t_a, t_b, t_c); \
	    else \
		transTransDF(_t1, _net, _t2->t_e, _t2->t_f, t_d, t_e, t_f); \
	} else

/* ----------------------- Inverting transforms ----------------------- */

/*
 * The GEOINVERTTRANS(t, inv) macro has the same effect as the following
 * code.  The code assumes that t_a, t_b, t_c, and t_d are one of
 * -1, 1, or 0, so it can't invert scaled transforms (but neither can
 * the normal GeoInvertTrans, anyway).
 *
 *	Transform t3;
 *
 *	t3.t_a = t->t_a;
 *	t3.t_b = t->t_d;
 *	t3.t_d = t->t_b;
 *	t3.t_e = t->t_e;
 *	t3.t_c = t3.t_f = 0;
 *	GeoTransTranslate(-t->t_c, -t->t_f, &t3, inv);
 *
 * where GeoTranslateTrans(x, y, t, net) is
 *
 *	net->t_a = t->t_a;
 *	net->t_b = t->t_b;
 *	net->t_d = t->t_d;
 *	net->t_e = t->t_e;
 *	net->t_c = x*t->t_a + y*t->t_b + t->t_c;
 *	net->t_f = x*t->t_d + y*t->t_e + t->t_f;
 */

/*
 * GEOINVERTTRANS(t, tinv) inverts Transform t into tinv
 * without multiplication.
 *
 * tMul(c, a) implements (a*c) without multiplication,
 * assuming that each of a, c are chosen from 0, -1, 1.
 */
#define	tMul(c, a) \
	((a)	? ((a) > 0 ? (c) : -(c))	: 0)

#define	GEOINVERTTRANS(t, inv) \
    if (1) { \
	Transform *xt = (t), *xinv = (inv); \
	xinv->t_a = xt->t_a; \
	xinv->t_b = xt->t_d; \
	xinv->t_d = xt->t_b; \
	xinv->t_e = xt->t_e; \
	xinv->t_c = - tMul(xt->t_c, xinv->t_a) - tMul(xt->t_f, xinv->t_b); \
	xinv->t_f = - tMul(xt->t_c, xinv->t_d) - tMul(xt->t_f, xinv->t_e); \
    } else

/*
 * GEOTRANSTRANSLATE(x, y, t, tresult) transforms an (x, y) translation
 * by the transform t, resulting in the transform tresult.
 */
#define	GEOTRANSTRANSLATE(x, y, t, tresult) \
    { \
	Transform *xt = (t); \
	*(tresult) = (*xt); \
	(tresult)->t_c += tMul(x, xt->t_a) + tMul(y, xt->t_b); \
	(tresult)->t_f += tMul(x, xt->t_d) + tMul(y, xt->t_e); \
    }
