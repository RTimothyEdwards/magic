/*
 * CMWrgbhsv.c --
 *
 * Procedures to translate between RGB color space and HSV color space.
 * Courtesy of Ken Fishkin.
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
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cmwind/CMWrgbhsv.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>

#include "utils/magic.h"

/* max of three numbers */
#define	MAX3(a,b,c)	(a > b ? (a > c ? a : c) : ( b > c ?b : c))
/*	and min	*/
#define	MIN3(a,b,c)	(a < b ? (a < c ? a : c) : ( b < c ?b : c))

/*
|-----------------------------------------------------------------------|
|	procedure	:  RGBxHSV					|
|	description	:  given r,g,b, each on [0,1], return hue, sat-	|
|			:  uration, and value, each on [0,1].		|
|			:  returns true <==> hue is defined.		|
|	source		:  Smith, Alvy Ray. "Color Tutorial Notes".	|
|			:  Technical Memo 37, LucasFilm.		|
|			:  hue calc changed a tad, according to Foley	|
|			:  and Van Dam.					|
|-----------------------------------------------------------------------|
*/

bool
RGBxHSV( r, g, b, h, s, v)
    double r, g, b;
    double *h, *s, *v;
{
  double	max, delta;
  double	mr,mg,mb;

  *v = max = MAX3(r,g,b);
  if ( (max == 0) || ( (delta = max - MIN3(r,g,b) ) == 0) ) {
	  /* achromatic */
    *s = 0;
    *h = 0;
    return(FALSE);
  };

  *s = delta/ max;
  mr = (max - r)/delta;
  mg = (max - g)/delta;
  mb = (max - b)/delta;

  if (r == max)
      *h = mb - mg;
  else if ( g == max)
      *h = 2.0 + mr - mb;
  else if ( b == max)
      *h = 4.0 + mg - mr;

  *h /= 6;
  if (*h < 0.0)
      *h += 1.0;
  return(TRUE);
}

/*
|-----------------------------------------------------------------------|
|	procedure	:  HSVxRGB					|
|	description	:  given HSV on [0,1], returns equivalent	|
|			:  RGB on [0,1].				|
|	source		:  Smith, Alvy Ray. "Color Tutorial Notes".	|
|			:  Technical Memo 37, LucasFilm.		|
|-----------------------------------------------------------------------|
*/
#define		SETRGB(rr,gg,bb)	*r=rr;*g=gg;*b=bb

void
HSVxRGB( h,s,v,r,g,b)
double	h,s,v;
double	*r,*g,*b;
    {
    double	f,m,n,k;
    int	i;
    double	vs,vsf;

    h *= 6;
    i	= h; 	/* integer part of hue	*/
    f	= h - i;	/* fractional part	*/
    vs	= v*s;
    vsf	= vs*f;
    m	= v - vs;
    n	= v - vsf;
    k	= v - vs + vsf;
    switch(i % 6)  {
      case 0	: SETRGB( v,k,m); break;
      case 1	: SETRGB( n,v,m); break;
      case 2	: SETRGB( m,v,k); break;
      case 3	: SETRGB( m,n,v); break;
      case 4	: SETRGB( k,m,v); break;
      case 5	: SETRGB( v,m,n); break;
    }
}

/*
|-----------------------------------------------------------------------|
|	procedure	:  RGBxHSL					|
|	description	:  converts from RGB to HSL coordinates.	|
|	source		:  '79 graphics core.				|
|-----------------------------------------------------------------------|
*/

bool
RGBxHSL( r, g, b, h, s, l )
    double  r,  g,  b;
    double  *h, *s, *l;
{
  double min, max;
  double delta, mr, mg, mb;

  min = MIN3( r,g,b);
  max = MAX3( r,g,b);

  /* Lightness calculation 	*/
  /* source - 79 graphics core	*/
  *l = 0.5 * (max + min);

  if ( (delta = max - min) == 0) {
    /* achromatic	*/
    *s = 0.0;
    *h = 0.0;
    return FALSE;
  }

  /* Saturation calculation */
  /* HSL is a double cone	*/
  if (*l < 0.5)
    *s = (delta)/(max+min);
  else
    *s = (delta)/(2.0-(max+min));

  /* hue calculation	*/
  mr = (max - r)/delta;
  mg = (max - g)/delta;
  mb = (max - b)/delta;


  if (r == max)
      *h = mb - mg;
  else if ( g == max)
      *h = 2.0 + mr - mb;
  else if ( b == max)
      *h = 4.0 + mg - mr;

  *h /= 6;
  if (*h < 0.0)
      *h += 1.0;
  return(TRUE);
}

/*
|-----------------------------------------------------------------------|
|	procedure	:  HSLxRGB					|
|	description	:  converts from HSL, each on [0,1], to RGB,	|
|			:  each on [0,1].				|
|	note		:  the idea is to use s,l to get HSV s,v, and	|
|			:  then use HSVxRGB - this is faster than the	|
|			:  '79 core algorithm.				|
|-----------------------------------------------------------------------|
*/

void
HSLxRGB( h, s, l, r, g, b )
    double h, s, l;
    double *r, *g, *b;
{
  double	min;
  double	v;
  double	f,n,k;
  int	i;
  double	vsf;

  if ( l <= 0.5) 
    v = l * (1.0 + s);
  else
    v = l + s - l*s;

  min = 2*l - v;
  if ( (s == 0.0) || ( l == 0.0) || ( l == 1.0 ) ) {
      SETRGB(l,l,l);
      return;
  }
  /* now just use hsvxrgb	*/
  s = ( v - min) / v;

  h *= 6;
  i	= h; 	/* integer part of hue	*/
  f	= h - i;	/* fractional part	*/
  vsf	= v*s*f;
  n	= v - vsf;
  k	= min + vsf;
  switch(i % 6)  {
    case 0	: SETRGB( v,k,min); break;
    case 1	: SETRGB( n,v,min); break;
    case 2	: SETRGB( min,v,k); break;
    case 3	: SETRGB( min,n,v); break;
    case 4	: SETRGB( k,min,v); break;
    case 5	: SETRGB( v,min,n); break;
  }
}

/*
|-----------------------------------------------------------------------|
|	procedure	:  Correct_chromaticity				|
|	description	:  given the white point at location (wx,wy),	|
|			:  and an existing point (x,y), such that	|
|			:  x + y > 1, intersect a line through the	|
|			:  white point and the exisiting point with the	|
|			:  line x+y = 1. side-affects new point.	|
|-----------------------------------------------------------------------|
*/

void
Correct_chromaticity(x, y, wx, wy)
    double	*x,*y;
    double	wx,wy;
{
    double	oldx,oldy;
    double	slope;
    double	b;

    oldx = *x; oldy = *y;
    slope = ( oldy - wy ) / ( oldx - wx);
    /* find b by eval'ing at white point */
    b = wy - slope*wx;

    *x = ( 1.0 - b ) / ( 1.0 + slope);
    *y = 1.0 - *x;
}

/*
|-----------------------------------------------------------------------|
|	procedure	:  xyz_to_mrgb					|
|	description	:  converts xyz to monitor rgb.			|
|			:  only really correct for mitsubishi mon.	|
|-----------------------------------------------------------------------|
*/

void
xyz_to_mrgb(x, y, z, mr, mg, mb)
    double	x, y, z;
    double	*mr, *mg, *mb;
{
    *mr =  2.4513*x - 1.2249*y - 0.3237*z;
    *mg = -1.4746*x + 2.5052*y + 0.0596*z;
    *mb =  0.0212*x - 0.2550*y + 1.1487*z;
}

/*
|-----------------------------------------------------------------------|
|	procedure	:  Make_mRGB_Nice				|
|	description	:  make mRGB values lie in 0..1, correcting for	|
|			:  luminance overflow, chrominance irreproduc-	|
|			:  ibility.					|
|	assumptions	:  at most one of mR,mG,mB < 0			|
|-----------------------------------------------------------------------|
*/

void
Make_mRGB_Nice(mR,mG,mB)
    double	*mR,*mG,*mB;
{
    double	min,max;
    double	mr, mg, mb;
    double	mY;
    double	white_mr,white_mg,white_mb;

    max = *mR;
    if ( max < *mG) max = *mG;
    if ( max < *mB) max = *mB;
    if ( max > 1.0) { /* luminance overflow, maintain chromaticity */
	*mR /= max;
	*mG /= max;
	*mB /= max;
    }
    min = *mR;
    if ( min > *mG) min = *mG;
    if ( min > *mB) min = *mB;
    if ( min >= 0.0 )
	return;
    /* correct in mrgb space	*/
    mY = *mR + *mG + *mB;
    mr = *mR/mY; mg = *mG / mY; mb = *mB / mY;
    /* correct based on which component is < 0 */
    /* find monitor white point */
    xyz_to_mrgb(1.0/3.0,1.0/3.0,1.0/3.0,&white_mr,&white_mg,&white_mb);
    if ( mr < 0 ) {
	mr = 0.0;
	Correct_chromaticity( &mg, &mb, white_mg,white_mb);
    } else if ( mg < 0 ) {
	mg = 0.0;
	Correct_chromaticity( &mr, &mb, white_mr,white_mb);
    } else if ( mb < 0 ) {
	mb = 0.0;
	Correct_chromaticity( &mr, &mg, white_mr,white_mg);
    }
    *mR = mr*mY;
    *mG = mg*mY;
    *mB = mb*mY;
}

