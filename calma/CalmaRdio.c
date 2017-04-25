/*
 * CalmaReadio.c --
 *
 * Input of Calma GDS-II stream format.
 * Low-level input.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/calma/CalmaRdio.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>

#include <netinet/in.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"
#include "utils/tech.h"
#include "cif/cif.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "calma/calmaInt.h"

/* Forward declarations */
bool calmaReadR8();
bool calmaSkipBytes();


/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadTransform --
 *
 * Read a CALMA_STRANS, CALMA_MAG, CALMA_ANGLE sequence and construct
 * the corresponding geometric transform.
 *
 * Results:
 *	TRUE normally, FALSE on EOF or fatal syntax error.
 *
 * Side effects:
 *	Consumes input.
 *	Modifies the Transform pointed to by 'ptrans'.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaReadTransform(ptrans, name)
    Transform *ptrans;	/* Fill in this transform */
    char *name;		/* Name of subcell (for errors) */
{
    int nbytes, rtype, flags, angle;
    double dangle;
    double dmag;
    Transform t;

    /* Default is the identity transform */
    *ptrans = GeoIdentityTransform;

    /* Is there any transform at all? */
    READRH(nbytes, rtype);
    if (nbytes < 0) return (FALSE);
    if (rtype != CALMA_STRANS)
    {
	UNREADRH(nbytes, rtype);
	return (TRUE);
    }
    if (nbytes != 6)
    {
	(void) calmaSkipBytes(nbytes - CALMAHEADERLENGTH);
	return (FALSE);
    }
    READI2(flags);

    /* Look for magnification and angle */
    READRH(nbytes, rtype);
    if (nbytes < 0) return (FALSE);
    if (rtype == CALMA_MAG)
    {
	if (nbytes != CALMAHEADERLENGTH + 8)
	{
	    (void) calmaSkipBytes(nbytes - CALMAHEADERLENGTH);
	    return (FALSE);
	}
	if (!calmaReadR8(&dmag)) return (FALSE);

        if (dmag != (double)((int)(dmag + 0.5)))
	{
	    calmaReadError("Non-integer magnification (%g) in transform\n", dmag);
	    calmaReadError("Rounding to %d.\n", (int)(dmag + 0.5));
	}
	GeoScaleTrans(ptrans, (int)(dmag + 0.5), &t);
	*ptrans = t;
    }
    else UNREADRH(nbytes, rtype);

    READRH(nbytes, rtype);
    if (nbytes < 0) return (FALSE);
    dangle = 0.0;
    if (rtype == CALMA_ANGLE)
    {
	if (nbytes != CALMAHEADERLENGTH + 8)
	{
	    (void) calmaSkipBytes(nbytes - CALMAHEADERLENGTH);
	    return (FALSE);
	}
	if (!calmaReadR8(&dangle)) return (FALSE);
    }
    else UNREADRH(nbytes, rtype);

    /* Make sure the angle is Manhattan */
    angle = (int) dangle;
    while (angle < 0) angle += 360;
    while (angle > 360) angle -= 360;
    switch (angle)
    {
	case 360:
	    angle = 0;
	    break;
	case 0:	case 90: case 180: case 270:
	    break;
	default:
	    calmaReadError("Non-Manhattan angle (%d) in transform\n", angle);
	    if (angle < 45) angle = 0;
	    else if (angle < 135) angle = 90;
	    else if (angle < 225) angle = 180;
	    else if (angle < 315) angle = 270;
	    else angle = 0;
	    calmaReadError("    Rounding to %d degrees.\n", angle);
    }

    /*
     * Construct the transform.
     * Magic angles are clockwise; Calma angles are counterclockwise.
     */
    if (flags & CALMA_STRANS_UPSIDEDOWN)
    {
	GeoTransTrans(ptrans, &GeoUpsideDownTransform, &t);
	*ptrans = t;
    }
    switch (angle)
    {
	case 90:
	    GeoTransTrans(ptrans, &Geo270Transform, &t);
	    *ptrans = t;
	    break;
	case 180:
	    GeoTransTrans(ptrans, &Geo180Transform, &t);
	    *ptrans = t;
	    break;
	case 270:
	    GeoTransTrans(ptrans, &Geo90Transform, &t);
	    *ptrans = t;
	    break;
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadI2Record --
 *
 * Read a record that should contain a two-byte integer.
 *
 * Results:
 *	TRUE on success, FALSE if the record type we read is not
 *	what we're expecting, or if it is of the wrong size.
 *
 * Side effects:
 *	Consumes input.
 *	Stores the result value in *pvalue (note that this is a normal
 *	int, even though we're reading only 16 bits from the input).
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaReadI2Record(type, pvalue)
    int type;		/* Type of record expected */
    int *pvalue;	/* Store value here */
{
    int nbytes, rtype, n;

    READRH(nbytes, rtype);
    if (nbytes < 0)
	goto eof;
    if (type != rtype)
    {
	calmaUnexpected(type, rtype);
	return (FALSE);
    }

    /* Read the value */
    READI2(n);
    if (feof(calmaInputFile)) goto eof;
    *pvalue = n;
    return (TRUE);

eof:
    calmaReadError("Unexpected EOF.\n");
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadI4Record --
 *
 * Read a record that should contain a four-byte integer.
 *
 * Results:
 *	TRUE on success, FALSE if the record type we read is not
 *	what we're expecting, or if it is of the wrong size.
 *
 * Side effects:
 *	Consumes input.
 *	Stores the result value in *pvalue.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaReadI4Record(type, pvalue)
    int type;		/* Type of record expected */
    int *pvalue;	/* Store value here */
{
    int nbytes, rtype, n;

    READRH(nbytes, rtype);
    if (nbytes < 0)
	goto eof;
    if (type != rtype)
    {
	calmaUnexpected(type, rtype);
	return (FALSE);
    }

    /* Read the value */
    READI4(n);
    if (feof(calmaInputFile)) goto eof;
    *pvalue = n;
    return (TRUE);

eof:
    calmaReadError("Unexpected EOF.\n");
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadStringRecord --
 *
 * Read a record that should contain an ASCII string.
 *
 * Results:
 *	TRUE on success, FALSE if the record type we read is not
 *	what we're expecting.
 *
 * Side effects:
 *	Consumes input.
 *	Allocates memory for string str (must be freed by the caller)
 *	Stores the result in the string pointed to by 'str'.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaReadStringRecord(type, str)
    int type;
    char **str;
{
    int nbytes, rtype;

    READRH(nbytes, rtype);
    if (nbytes < 0)
	goto eof;

    if (type != rtype)
    {
	calmaUnexpected(type, rtype);
	return (FALSE);
    }

    nbytes -= CALMAHEADERLENGTH;
    *str = (char *) mallocMagic(nbytes + 1);
    if (fread(*str, sizeof (char), nbytes, calmaInputFile) != nbytes)
	goto eof;

    *(*str + nbytes) = '\0';
    return (TRUE);

eof:
    calmaReadError("Unexpected EOF.\n");
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadR8 --
 *
 * Read a single 8-byte real number in Calma stream format.
 * Convert to internal double-precision format and store in
 * the double pointed to by 'pd'.
 *
 * Results:
 *	TRUE on success, FALSE if EOF is encountered.
 *
 * Side effects:
 *	Consumes input.
 *	Stores the result in the *pd.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaReadR8(pd)
    double *pd;		/* Store result in *pd */
{
    int i, exponent;
    unsigned char dchars[8];
    double mantissa, d;
    bool isneg;

    if (fread((char *) dchars, sizeof (char), sizeof dchars,
		calmaInputFile) != sizeof dchars)
	return (FALSE);

    /* Extract the sign and exponent */
    exponent = dchars[0];
    if (isneg = (exponent & 0x80))
	exponent &= ~0x80;
    exponent -= 64;

    /* Construct the mantissa */
    mantissa = 0.0;
    for (i = 7; i > 0; i--)
    {
	mantissa += dchars[i];
	mantissa /= 256.0;
    }

    /* Now raise the mantissa to the exponent */
    d = mantissa;
    if (exponent > 0)
    {
	while (exponent-- > 0)
	    d *= 16.0;
    }
    else if (exponent < 0)
    {
	while (exponent++ < 0)
	    d /= 16.0;
    }

    /* Make it negative if necessary */
    if (isneg)
	d = -d;

    *pd = d;
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaSkipSet --
 *
 * Skip all records falling in a specified set of types.
 * Leave the input stream positioned to the start of the first
 * record not in the specified set.
 *
 * The array pointed to by 'skipwhat' contains the record types
 * of all records to be skipped, terminated with -1.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Consumes input.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaSkipSet(skipwhat)
    int *skipwhat;
{
    int *skipp;
    int nbytes, rtype;

    for (;;)
    {
	READRH(nbytes, rtype);
	if (nbytes < 0)
	    return;

	for (skipp = skipwhat; *skipp >= 0; skipp++)
	    if (*skipp == rtype)
		goto skipit;

	UNREADRH(nbytes, rtype);
	break;

skipit:
	(void) calmaSkipBytes(nbytes - CALMAHEADERLENGTH);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaSkipExact --
 *
 * Skip a single stream record, which must be of the type 'type'.
 * Leave the input positioned to the start of the record following
 * this one.  Complain if the record is not the one expected.
 *
 * Results:
 *	TRUE if successful, FALSE if we encountered an error and
 *	the caller should abort.
 *
 * Side effects:
 *	Consumes input.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaSkipExact(type)
    int type;
{
    int nbytes, rtype;

    /* Eat up the record header */
    READRH(nbytes, rtype);

    if (nbytes < 0)
	goto eof;

    /* Skip remainder of record */
    if (!calmaSkipBytes(nbytes - CALMAHEADERLENGTH))
	goto eof;

    if (rtype != type)
    {
	calmaUnexpected(type, rtype);
	return (FALSE);
    }

    return (TRUE);

eof:
    calmaReadError("Unexpected EOF.\n");
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaSkipTo --
 *
 * Skip to a record of a particular type.  Leaves the input stream
 * positioned AFTER the record whose type is given by 'what'.
 *
 * Results:
 *	TRUE if we found this record, FALSE if EOF was encountered.
 *
 * Side effects:
 *	Consumes input.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaSkipTo(what)
    int what;
{
    int nbytes, rtype;

    do
    {
	READRH(nbytes, rtype);
	if (nbytes < 0)
	    return (FALSE);
	calmaSkipBytes(nbytes - CALMAHEADERLENGTH);
    } while (rtype != what);

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaSkipBytes --
 *
 * Skip 'nbytes' bytes from the input.
 * WARNING: this procedure doesn't know about input saved via UNREADRH(),
 * so if the caller wants this input to be discarded, it must call READRH()
 * itself.
 *
 * Results:
 *	TRUE if successful, FALSE if EOF was encountered.
 *
 * Side effects:
 *	Consumes nbytes of input.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaSkipBytes(nbytes)
    int nbytes;	/* Skip this many bytes */
{
    while (nbytes-- > 0)
	if (getc(calmaInputFile) < 0)
	    return (FALSE);

    return (TRUE);
}
