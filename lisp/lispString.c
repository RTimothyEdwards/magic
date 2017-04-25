/*************************************************************************
 *
 *  lispString.c --
 *
 *   This module contains the builtin mini-scheme string functions.
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
 *  $Id: lispString.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
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
 *  LispStrCat --
 *
 *      Concatenate two strings.
 *
 *  Results:
 *      Returns the concatenated string.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
LispObj *
LispStrCat (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;

  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_STRING ||
      LTYPE(ARG2(s)) != S_STRING || ARG3P(s)) {
    TxPrintf ("Usage: (%s str1 str2)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_STRING;
  LSTR(l) = (char *) mallocMagic((unsigned) (strlen(LSTR(ARG1(s)))+strlen(LSTR(ARG2(s)))+1));
  strcpy (LSTR(l),LSTR(ARG1(s)));
  strcat (LSTR(l),LSTR(ARG2(s)));
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispSymbolToString --
 *
 *      Returns the string name for a symbol.
 *
 *  Results:
 *      New string.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispSymbolToString (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_SYM || ARG2P(s)) {
    TxPrintf ("Usage: (%s symbol)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_STRING;
  LSTR(l) = (char *) mallocMagic((unsigned) (strlen(LSYM(ARG1(s)))+1));
  strcpy (LSTR(l), LSYM(ARG1(s)));
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispStringToSymbol --
 *
 *      Symbol named "string"
 *
 *  Results:
 *      The symbol.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispStringToSymbol (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s string)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_SYM;
  LSYM(l) = LispNewString (LSTR(ARG1(s)));
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispNumberToString --
 *
 *      Convert number to string.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispNumberToString (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  char buf[128];

  if (!ARG1P(s) || !NUMBER(LTYPE(ARG1(s))) || ARG2P(s)) {
    TxPrintf ("Usage: (%s num)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_STRING;
  if (LTYPE(ARG1(s)) == S_FLOAT)
    sprintf (buf, "%lf", LFLOAT(ARG1(s)));
  else
    sprintf (buf, "%d", LINTEGER(ARG1(s)));
  LSTR(l) = (char *) mallocMagic((unsigned) (strlen(buf)+1));
  strcpy (LSTR(l),buf);
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispStringToNumber --
 *
 *      Number named "string"
 *
 *  Results:
 *      The number.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispStringToNumber (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  char *str;
  int r;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s string)\n", name);
    RETURN;
  }
  str = LSTR(ARG1(s));
  l = LispNewObj ();
  if (isdigit(*str) || *str == '.' || *str == '-' || *str == '+') {
    r = 0;
    if (*str == '-' || *str == '+')
      str++;
    if (!*str) {
      TxPrintf ("String is not a number.\n");
      RETURN;
    }
    while (*str && isdigit (*str))
      str++;
    if (*str && *str == '.') {
      r = 1;
      str++;
    }
    while (*str && isdigit(*str))
      str++;
    *str = '\0';
    if (r) {
      LTYPE(l) = S_FLOAT;
      sscanf (LSTR(ARG1(s)), "%lf", &LFLOAT(l));
    }
    else {
      LTYPE(l) = S_INT;
      sscanf (LSTR(ARG1(s)), "%d", &LINTEGER(l));
    }
  }
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispStringLength --
 *
 *      Compute length of string.
 *
 *  Results:
 *      Returns length.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispStringLength (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s string)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  LINTEGER(l) = strlen (LSTR(ARG1(s)));
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispStringCompare --
 *
 *      Compare two strings.
 *
 *  Results:
 *      An integer.
 *      0    => str1 == str2
 *      (>0) => str1 > str2
 *      (<0) => str1 < str2
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispStringCompare (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_STRING ||
      LTYPE(ARG2(s)) != S_STRING || ARG3P(s)) {
    TxPrintf ("Usage: (%s str1 str2)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  LINTEGER(l) = strcmp (LSTR(ARG1(s)),LSTR(ARG2(s)));
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispStringRef --
 *
 *      Return character k from a string.
 *
 *  Results:
 *      An integer.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispStringRef (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_STRING ||
      LTYPE(ARG2(s)) != S_INT || ARG3P(s)) {
    TxPrintf ("Usage: (%s str int)\n", name);
    RETURN;
  }
  if (strlen (LSTR(ARG1(s))) <= LINTEGER(ARG2(s)) || LINTEGER(ARG2(s)) < 0) {
    TxPrintf ("%s: integer argument out of range.\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  LINTEGER(l) = LSTR(ARG1(s))[LINTEGER(ARG2(s))];
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispStringSet --
 *
 *      Set kth string character to the appropriate integer.
 *
 *  Results:
 *      boolean.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispStringSet (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;

  if (!ARG1P(s) || !ARG2P(s) || !ARG3P(s) ||
      LTYPE(ARG1(s)) != S_STRING || LTYPE(ARG2(s)) != S_INT ||
      LTYPE(ARG3(s)) != S_INT || ARG4P(s)) {
    TxPrintf ("Usage: (%s str int int)\n", name);
    RETURN;
  }
  if (strlen (LSTR(ARG1(s))) <= LINTEGER(ARG2(s)) || LINTEGER(ARG2(s)) < 0) {
    TxPrintf ("%s: integer argument out of range.\n", name);
    RETURN;
  }
  l = LispNewObj();
  LSTR(ARG1(s))[LINTEGER(ARG2(s))] = LINTEGER(ARG3(s));
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispSubString --
 *
 *      Return a substring from a string.
 *
 *  Results:
 *      String.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispSubString (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;

  if (!ARG1P(s) || !ARG2P(s) || !ARG3P(s) ||
      LTYPE(ARG1(s)) != S_STRING || LTYPE(ARG2(s)) != S_INT ||
      LTYPE(ARG3(s)) != S_INT || ARG4P(s)) {
    TxPrintf ("Usage: (%s str int int)\n", name);
    RETURN;
  }
  if (!(0 <= LINTEGER(ARG2(s)) && LINTEGER(ARG2(s)) <= LINTEGER(ARG3(s)) &&
	LINTEGER(ARG3(s)) <= strlen(LSTR(ARG1(s))))) {
    TxPrintf ("%s: integer argument out of range.\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_STRING;
  LSTR(l) = (char*) mallocMagic((unsigned) (LINTEGER(ARG3(s))-LINTEGER(ARG2(s))+1));
  strncpy (LSTR(l), LSTR(ARG1(s))+LINTEGER(ARG2(s)), 
	   LINTEGER(ARG3(s))-LINTEGER(ARG2(s)));
  LSTR(l)[LINTEGER(ARG3(s))-LINTEGER(ARG2(s))] = '\0';
  return l;
}
