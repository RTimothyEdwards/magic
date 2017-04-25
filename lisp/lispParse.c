/*************************************************************************
 *
 *  lispParse.c -- 
 *
 *   This module contains the mini-scheme command-line parser (ugh).
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
 *  $Id: lispParse.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>
#include <ctype.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "textio/textio.h"
#include "utils/hash.h"
#include "utils/malloc.h"

#define IsSpace(c)        ((c) == ' ')

#define BeginIdChar(c)    (isalpha(c) || (c) == '+' || (c) == '-' || \
			   (c) == '.' || (c) == '*' || (c) == '/' || \
			   (c) == '<' || (c) == '=' || (c) == '>' || \
			   (c) == '!' || (c) == '?' || (c) == ':' || \
			   (c) == '$' || (c) == '%' || (c) == '_' || \
			   (c) == '&' || (c) == '~' || (c) == '^' || \
			   (c) == '#' || (c) == '@' || (c) == ',')

#define IsIdChar(c)   (BeginIdChar(c) || isdigit(c) || ((c) == '[') || \
		       ((c) == ']'))

#define ISEND(c)   ((c) == '\0' || isspace(c) || (c) == ')' || (c) == '(')

/*-----------------------------------------------------------------------------
 *
 * Various string munging functions
 *
 *-----------------------------------------------------------------------------
 */

/* 
   strip whitespace from left: returns new string pointer
*/
static char *
stripleft (s)
     char *s;
{
  while (*s && IsSpace (*s))
    s++;
  return s;
}

#define STRINGTAB 1000

static int nstrings = 0;
HashTable Strings;


/*-----------------------------------------------------------------------------
 * 
 *  LispNewString --
 *
 *    Returns a unique string pointer corresponding to string "s"
 *
 *-----------------------------------------------------------------------------
 */
char *LispNewString (s)
     char *s;
{
  int i;
  HashEntry *h;

  if (nstrings == 0)
    HashInit (&Strings, STRINGTAB, HT_STRINGKEYS);
  
  h = HashLookOnly (&Strings, s);
  if (h)
    i = (int) HashGetValue(h);
  else {
    i = nstrings++;
    h = HashFind (&Strings, s);
    HashSetValue (h, i);
  }
  return h->h_key.h_name;
}

/*-----------------------------------------------------------------------------
 *
 *  LispStringId --
 *
 *      Returns an integer identifier associated with string, used for
 *      faster function table lookup.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int
LispStringId (s)
     char *s;
{
  int i;
  HashEntry *h;

  h = HashLookOnly (&Strings, s);
  if (!h)
    i = -1;
  else
    i = (int)HashGetValue (h);
  return i;
}

/*-----------------------------------------------------------------------------
 *
 *  LispAtomParse --
 *
 *      Parse an atom.
 *      If within a quote, 'quoted' is 1.
 *
 *  Results:
 *      Returns pointer to an object.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispAtomParse (pstr,quoted)
     int quoted;
     char **pstr;
{
  char *str = *pstr;
  char *q, c;
  LispObj *l;
  int r;
  char *t;

  l = LispNewObj ();

  if ((*str == '+' || *str == '-' || *str == '.') && ISEND(*(str+1))) {
    str++;
    *str = '\0';
    LTYPE(l) = S_SYM;
    LSYM(l) = LispNewString (str-1);
    *pstr = str+1;
    return l;
  }
  if ((isdigit(*str) || *str == '.' || *str == '-' || *str == '+') &&
      (isdigit(str[1]) || str[1] == '.' || str[1] == '-' || str[1] == '+' 
       || ISEND(str[1]))) {
    /* eat leading sign */
    q = str;
    r = 0;
    if (*str == '-' || *str == '+')
      str++;
    if (!*str) {
      TxPrintf ("Invalid number\n");
      *pstr = str;
      return NULL;
    }
    while (*str && isdigit (*str))
      str++;
    if (*str && *str == '.') {
      r = 1;
      str++;
    }
    while (*str && isdigit(*str))
      str++;
    c = *str;
    *str = '\0';
    if (r) {
      LTYPE(l) = S_FLOAT;
      sscanf (q, "%lf", &LFLOAT(l));
    }
    else {
      LTYPE(l) = S_INT;
      sscanf (q, "%d", &LINTEGER(l));
    }
    *str = c;
  }
  else if (*str == '\"') {
    str++;
    q = str;
    while (*str != '\"') {
      if (!*str) {
	TxPrintf ("Unterminated string\n");
	*pstr = str;
	return NULL;
      }
      if (*str == '\\') {
	if (*(str+1))
	  str++;
	else {
	  TxPrintf ("Trailing character constant\n");
	  *pstr = str;
	  return NULL;
	}
      }
      str++;
    }
    *str = '\0';
    LTYPE(l) = S_STRING;
    LSTR(l) = (char *) mallocMagic((unsigned) (strlen (q)+1));
    strcpy (LSTR(l), q);
    *str = '\"';
    str++;
  }
  else if (!quoted && str[0] == '#' && (str[1] == 't'|| str[1] == 'f') && 
	   ISEND(str[2])) {
    LTYPE(l) = S_BOOL;
    LBOOL(l) = (str[1] == 't') ? 1 : 0;
    str+=2;
  }
  else if (BeginIdChar(*str) || (*str == '\\')) {
    int nest;

    LTYPE(l) = S_SYM;
    t = q = str;
    if (*str == '\\') {
      *t = *++str;
      t++;
      str++;
    }
    nest = 0;
    while (*str && IsIdChar (*str) || *str == '(' || (*str == ')' && nest>0)) {
      *t = *str;
      if (*str == '(') nest++;
      if (*str == ')') nest--;
      str++;
      if (*str == '\\') {
	*t = *++str;
	str++;
      }
      t++;
    }
    c = *t;
    *t = '\0';
    LSYM(l) = LispNewString (q);
    *t = c;
  }
  else {
    TxPrintf ("Unparsable input character: %c\n", *str);
    *pstr = str;
    return NULL;
  }
  *pstr = str;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispIParse --
 *
 *      Parse a string to a Sexp.
 *
 *  Results:
 *      Returns pointer to a Sexp.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Sexp *
LispIParse (pstr)
     char **pstr;
{
  char *str = *pstr;
  Sexp *s;
  Sexp *ret = NULL;
  Sexp **sptr;

  str = stripleft (str);
  if (!*str) {
    TxPrintf ("Input malformed\n");
    return NULL;
  }
  while (*str != ')') {
    if (!*str) {
      TxPrintf ("Input malformed: missing )\n");
      return NULL;
    }
    s = LispNewSexp ();
    if (*str == '(') {
      CAR(s) = LispNewObj ();
      LTYPE(CAR(s)) = S_LIST;
      str++;
      LLIST(CAR(s)) = LispIParse (&str);
      if (*str != ')') {
	*pstr = str;
	return NULL;
      }
      str++;
    }
    else if (*str == '\'') {
      LispObj *l;
      Sexp *t;
      t = s;
      CAR(s) = LispNewObj ();
      LTYPE(CAR(s)) = S_SYM;
      LSYM(CAR(s)) = LispNewString ("quote");
      CDR(s) = LispNewObj ();
      LTYPE(CDR(s)) = S_LIST;
      LLIST(CDR(s)) = LispNewSexp ();
      s = LLIST(CDR(s));
      CDR(s) = LispNewObj ();
      LTYPE(CDR(s)) = S_LIST;
      LLIST(CDR(s)) = NULL;
      str++;
      str = stripleft (str);
      if (*str == '(') {
	str++;
	CAR(s) = LispNewObj ();
	LTYPE(CAR(s)) = S_LIST;
	LLIST(CAR(s)) = LispIParse (&str);
	if (*str != ')') {
	  *pstr = str;
	  return NULL;
	}
	str++;
      }
      else {
	if (!(CAR(s) = LispAtomParse (&str,1))) {
	  *pstr = str;
	  return NULL;
	}
      }
      *pstr = str;
      l = LispNewObj ();
      LTYPE(l) = S_LIST;
      LLIST(l) = t;
      t = LispNewSexp ();
      CAR(t) = l;
      s = t;
    }
    else {
      if (!(CAR(s) = LispAtomParse (&str,0))) {
	*pstr = str;
	return NULL;
      }
    }
    CDR(s) = LispNewObj ();
    LTYPE(CDR(s)) = S_LIST;
    if (ret == NULL)
      ret = s;
    else
      *sptr = s;
    sptr = &LLIST(CDR(s));
    str = stripleft (str);
  }
  if (ret)
    *sptr = NULL;
  *pstr = str;
  return ret;
}


/*-----------------------------------------------------------------------------
 *
 *  LispParseString --
 *
 *      Parse string to a lisp object.
 *
 *  Results:
 *      Returns pointer to the object.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispParseString (str)
     char *str;
{
  LispObj *l;

  str = stripleft (str);
  if (*str != '(')
    l = LispAtomParse (&str,0);
  else {
    str++;
    l = LispNewObj ();
    LTYPE(l) = S_LIST;
    LLIST(l) = LispIParse (&str);
    if (LLIST(l) && *str != ')')
	l = NULL;
  }
  return l;
}
