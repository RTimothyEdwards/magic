/*************************************************************************
 *
 *  lispArith.c --
 *
 *   This module contains the builtin mini-scheme arithmetic functions.
 *
 *  (c) 1996 California Institute of Technology
 *  Department of Computer Science
 *  Pasadena, CA 91125.
 *
 *  Permission to use, copy, modify, and distribute this software
 *  and its documentation for any purpose and without fee is hereby
 *  granted, provided that the above copyright notice appear in all
 *  copies. The California Institute of Technology makes no representations
 *  about the suitability of this software for any purpose. It is
 *  provided "as is" without express or implied warranty. Export of this
 *  software outside of the United States of America may require an
 *  export license.
 *
 *  $Id: lispArith.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "lispargs.h"
#include "textio/textio.h"
#include "utils/malloc.h"


/*-----------------------------------------------------------------------------
 *
 *  LispAdd --
 *
 *      "+"
 *
 *  Results:
 *      Returns the sum of two arguments.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispAdd (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !ARG2P(s) || !NUMBER(LTYPE(ARG1(s))) ||
      !NUMBER(LTYPE(ARG2(s))) || ARG3P(s)) {
    TxPrintf ("Usage: (%s num1 num2)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  if (LTYPE(ARG1(s)) == S_FLOAT || LTYPE(ARG2(s)) == S_FLOAT) {
    LTYPE(l) = S_FLOAT;
    d = LTYPE(ARG1(s)) == S_FLOAT ? LFLOAT(ARG1(s)) : LINTEGER(ARG1(s));
    d+= LTYPE(ARG2(s)) == S_FLOAT ? LFLOAT(ARG2(s)) : LINTEGER(ARG2(s));
    LFLOAT(l) = d;
  }
  else {
    LTYPE(l) = S_INT;
    LINTEGER(l) = LINTEGER(ARG1(s))+LINTEGER(ARG2(s));
  }
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispSub --
 *
 *      "-"
 *
 *  Results:
 *      Returns the difference of two arguments.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispSub (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !ARG2P(s) || !NUMBER(LTYPE(ARG1(s))) ||
      !NUMBER(LTYPE(ARG2(s))) || ARG3P(s)) {
    TxPrintf ("Usage: (%s num1 num2)\n", name);
    RETURN;
  }
  if (!NUMBER(LTYPE(ARG1(s))) || !NUMBER(LTYPE(ARG2(s)))) {
    TxPrintf ("%s: requires two numbers\n", name);
    RETURN;
  }
  l = LispNewObj ();
  if (LTYPE(ARG1(s)) == S_FLOAT || LTYPE(ARG2(s)) == S_FLOAT) {
    LTYPE(l) = S_FLOAT;
    d = LTYPE(ARG1(s)) == S_FLOAT ? LFLOAT(ARG1(s)) : LINTEGER(ARG1(s));
    d-= LTYPE(ARG2(s)) == S_FLOAT ? LFLOAT(ARG2(s)) : LINTEGER(ARG2(s));
    LFLOAT(l) = d;
  }
  else {
    LTYPE(l) = S_INT;
    LINTEGER(l) = LINTEGER(ARG1(s))-LINTEGER(ARG2(s));
  }
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispMult --
 *
 *      "*"
 *
 *  Results:
 *      Returns the product of two arguments.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispMult (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !ARG2P(s) || !NUMBER(LTYPE(ARG1(s))) ||
      !NUMBER(LTYPE(ARG2(s))) || ARG3P(s)) {
    TxPrintf ("Usage: (%s num1 num2)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  if (LTYPE(ARG1(s)) == S_FLOAT || LTYPE(ARG2(s)) == S_FLOAT) {
    d = LTYPE(ARG1(s)) == S_FLOAT ? LFLOAT(ARG1(s)) : LINTEGER(ARG1(s));
    d *= (LTYPE(ARG2(s)) == S_FLOAT ? LFLOAT(ARG2(s)) : LINTEGER(ARG2(s)));
    LTYPE(l) = S_FLOAT;
    LFLOAT(l) = d;
  }
  else {
    LTYPE(l) = S_INT;
    LINTEGER(l) = LINTEGER(ARG1(s))*LINTEGER(ARG2(s));
  }
  return l;
}

/*-----------------------------------------------------------------------------
 *
 *  LispDiv --
 *
 *      "/"
 *
 *  Results:
 *      Returns the quotient of two arguments.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispDiv (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !ARG2P(s) || !NUMBER(LTYPE(ARG1(s))) ||
      !NUMBER(LTYPE(ARG2(s))) || ARG3P(s)) {
    TxPrintf ("Usage: (%s num1 num2)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  if (LTYPE(ARG1(s)) == S_FLOAT || LTYPE(ARG2(s)) == S_FLOAT) {
    LTYPE(l) = S_FLOAT;
    d = LTYPE(ARG1(s)) == S_FLOAT ? LFLOAT(ARG1(s)) : LINTEGER(ARG1(s));
    if ((LTYPE(ARG2(s)) == S_FLOAT && LFLOAT(ARG2(s)) == 0) ||
	(LTYPE(ARG2(s)) == S_INT && LINTEGER(ARG2(s)) == 0)) {
      TxPrintf ("Division by zero\n");
      RETURN;
    }
    d /= LTYPE(ARG2(s)) == S_FLOAT ? LFLOAT(ARG2(s)) : LINTEGER(ARG2(s));
    LFLOAT(l) = d;
  }
  else {
    if (LINTEGER(ARG2(s)) == 0) {
      TxPrintf ("Division by zero\n");
      RETURN;
    }
    d = (double)LINTEGER(ARG1(s))/(double)LINTEGER(ARG2(s));
    if (d == ((int)d)) {
      LTYPE(l) = S_INT;
      LINTEGER(l) = (int)d;
    }
    else {
      LTYPE(l) = S_FLOAT;
      LFLOAT(l) = d;
    }
  }
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispTruncate --
 *
 *      Truncate a number.
 *
 *  Results:
 *      Returns an integer.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispTruncate (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !NUMBER(LTYPE(ARG1(s))) || ARG2P(s)) {
    TxPrintf ("Usage: (%s num)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  if (LTYPE(ARG1(s)) == S_FLOAT) 
    LINTEGER(l) = (int)LFLOAT(ARG1(s));
  else
    LBOOL(l) = LINTEGER(ARG1(s));
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispZeroQ --
 *
 *      Checks if argument is zero.
 *
 *  Results:
 *      Returns a boolean.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispZeroQ (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !NUMBER(LTYPE(ARG1(s))) || ARG2P(s)) {
    TxPrintf ("Usage: (%s num)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  if (LTYPE(ARG1(s)) == S_FLOAT) 
    LBOOL(l) = LFLOAT(ARG1(s)) == 0 ? 1 : 0;
  else
    LBOOL(l) = LINTEGER(ARG1(s)) == 0 ? 1 : 0;
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispPositiveQ --
 *
 *      Checks if argument is positive.
 *
 *  Results:
 *      Returns a boolean.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispPositiveQ (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !NUMBER(LTYPE(ARG1(s))) || ARG2P(s)) {
    TxPrintf ("Usage: (%s num)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  if (LTYPE(ARG1(s)) == S_FLOAT) 
    LBOOL(l) = LFLOAT(ARG1(s)) > 0 ? 1 : 0;
  else
    LBOOL(l) = LINTEGER(ARG1(s)) > 0 ? 1 : 0;
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispNegativeQ --
 *
 *      Checks if argument is negative.
 *
 *  Results:
 *      Returns a boolean.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispNegativeQ (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  double d;

  if (!ARG1P(s) || !NUMBER(LTYPE(ARG1(s))) || ARG2P(s)) {
    TxPrintf ("Usage: (%s num)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  if (LTYPE(ARG1(s)) == S_FLOAT) 
    LBOOL(l) = LFLOAT(ARG1(s)) < 0 ? 1 : 0;
  else
    LBOOL(l) = LINTEGER(ARG1(s)) < 0 ? 1 : 0;
  return l;
}
