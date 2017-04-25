 /*************************************************************************
 *
 *  lispPrint.c -- 
 *
 *   Stuff that prints out the internals of lists.
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
 *  $Id: lispPrint.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "lispargs.h"
#include "utils/signals.h"
#include "utils/hash.h"

#define IsSpace(c)        ((c) == ' ')

/*
  Print t into s, expanding control characters.
  flag = 1: put quotes around if spaces present
  flag = 0: don't
*/
static
void
LispBufPrintName (s,t,flag)
     char *s, *t;
     int flag;
{
  int i,j, spc;
  i=0;
  spc=0;
  for (j=0; t[j]; j++)
    if (IsSpace (t[j])) {
      spc=1;
      break;
    }
  if (spc && flag) s[i++] = '\"';
  for (j=0; t[j]; j++)
    if (t[j] >= 32)
      s[i++] = t[j];
    else {
      s[i++] = '^';
      s[i++] = t[j]+64;
    }
  if (spc && flag)
    s[i++] = '\"';
  s[i]='\0';
}

/*------------------------------------------------------------------------
 *
 *  LispPrint --
 *
 *      Print the object out to the text stream.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      Output appears in text window.
 *
 *------------------------------------------------------------------------
 */

static char obuf[128];
static HashTable PrintTable, GenTable;
static int num_refs;

static void
_LispPrint (fp,l)
     FILE *fp;
     LispObj *l;
{
  HashEntry *h;
  int i;
  if (SigInterruptPending) return;
  h = HashLookOnly (&PrintTable, l);
  if (h) {
    i = (int) HashGetValue (h);
    if (i) {
      /* i > 0 */
      if (fp == stdout) TxPrintf ("#%d", i);
      else fprintf (fp, "#%d", i);
      return;
    }
    else {
      /* not printed; set the print tag */
      HashSetValue (h, ++num_refs);
      if (fp == stdout) TxPrintf ("#%d:", num_refs);
      else fprintf (fp, "#%d:", num_refs);
    }
  }
  switch (LTYPE(l)) {
  case S_INT:
    if (fp == stdout) TxPrintf ("%d", LINTEGER(l));
    else fprintf (fp,"%d", LINTEGER(l));
    break;
  case S_FLOAT:
    if (fp == stdout) TxPrintf ("%lf", LFLOAT(l));
    else fprintf (fp, "%lf", LFLOAT(l));
    break;
  case S_STRING:
    LispBufPrintName (obuf, LSTR(l),0);
    if (fp == stdout) TxPrintf ("\"%s\"", obuf);
    else fprintf (fp, "\"%s\"", obuf);
    break;
  case S_BOOL:
    if (fp == stdout) TxPrintf ("#%c", LINTEGER(l) ? 't' : 'f');
    else fprintf (fp, "#%c", LINTEGER(l) ? 't' : 'f');
    break;
  case S_SYM:
    LispBufPrintName (obuf, LSYM(l),1);
    if (fp == stdout) TxPrintf ("%s", obuf);
    else fprintf (fp, "%s", obuf);
    break;
  case S_LIST:
    if (fp == stdout) TxPrintf ("(");
    else fprintf (fp, "(");
    if (LLIST(l)) {
      Sexp *s;
      s = LLIST(l);
      _LispPrint (fp,CAR(s));
      while ((LTYPE(CDR(s)) == S_LIST) && LLIST(CDR(s))) {
	if (fp == stdout) TxPrintf (" ");
	else fprintf (fp, " ");
	h = HashLookOnly (&PrintTable, CDR(s));
	if (h) {
	  i = (int) HashGetValue (h);
	  if (i) {
	    /* i > 0 */
	    if (fp == stdout) TxPrintf ("#%d", i);
	    else fprintf (fp, "#%d", i);
	    goto done;
	  }
	  else {
	    /* not printed; set the print tag */
	    HashSetValue (h, ++num_refs);
	    if (fp == stdout) TxPrintf ("#%d:", num_refs);
	    else fprintf (fp, "#%d:", num_refs);
	  }
	}
	s = LLIST(CDR(s));
	_LispPrint (fp,CAR(s));
      }
      if (LTYPE(CDR(s)) != S_LIST) {
	if (fp == stdout) TxPrintf (" . ");
	else fprintf (fp, " . ");
	_LispPrint (fp,CDR(s));
      }
    }
done:
    if (fp == stdout) TxPrintf (")");
    else fprintf (fp, ")");
    break;
  case S_LAMBDA:
    if (fp == stdout) TxPrintf ("(lambda ");
    else fprintf (fp, "(lambda ");
    _LispPrint (fp,ARG2(LUSERDEF(l)));
    if (fp == stdout) TxPrintf (" ");
    else fprintf (fp, " ");
    _LispPrint (fp,ARG4(LUSERDEF(l)));
    if (fp == stdout) TxPrintf (")");
    else fprintf (fp, ")");
    break;
  case S_LAMBDA_BUILTIN:
  case S_MAGIC_BUILTIN:
    if (fp == stdout) TxPrintf ("#proc");
    else fprintf (fp, "#proc");
    break;
  default:
    break;
  }
}


static void
_LispGenTable (l)
     LispObj *l;
{
  HashEntry *h;
  int i;
  if (SigInterruptPending) return;
  h = HashLookOnly (&GenTable, l);
  if (h) {
    i = (int) HashGetValue (h);
    i++;
    HashSetValue (h, i);
    return;
  }
  h = HashFind (&GenTable, l);
  HashSetValue (h, 0);
  switch (LTYPE(l)) {
  case S_INT:
  case S_FLOAT:
  case S_STRING:
  case S_BOOL:
  case S_SYM:
  case S_LAMBDA_BUILTIN:
  case S_MAGIC_BUILTIN:
    break;
  case S_LIST:
    if (LLIST(l)) {
      Sexp *s;
      s = LLIST(l);
      _LispGenTable (CAR(s));
      while ((LTYPE(CDR(s)) == S_LIST) && LLIST(CDR(s))) {
	h = HashLookOnly (&GenTable, CDR(s));
	if (h) {
	  i = (int) HashGetValue (h);
	  i++;
	  HashSetValue (h, i);
	  return;
	}
	h = HashFind (&GenTable, CDR(s));
	HashSetValue (h, 0);
	s = LLIST(CDR(s));
	_LispGenTable (CAR(s));
      }
      if (LTYPE(CDR(s)) != S_LIST) {
	_LispGenTable (CDR(s));
      }
    }
    break;
  case S_LAMBDA:
    _LispGenTable (ARG2(LUSERDEF(l)));
    _LispGenTable (ARG4(LUSERDEF(l)));
    break;
  default:
    break;
  }
}


void
LispPrint (fp, l)
     FILE *fp;
     LispObj *l;
{
  HashEntry *h;
  HashSearch hs;
  int i;

  num_refs = 0;
  HashInit (&PrintTable, 128, HT_WORDKEYS);
  HashInit (&GenTable, 128, HT_WORDKEYS);
  _LispGenTable (l);
  HashStartSearch (&hs);
  while (h = HashNext (&GenTable, &hs)) {
    i = (int) HashGetValue (h);
    if (i) {
      h = HashFind (&PrintTable, h->h_key.h_ptr);
      HashSetValue (h, 0);
    }
  }
  _LispPrint (fp,l);
  HashKill (&PrintTable);
  HashKill (&GenTable);
}


/*------------------------------------------------------------------------
 *
 *  LispPrintType --
 *
 *      Print the type of the object out to the text stream.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      Output appears in text window.
 *
 *------------------------------------------------------------------------
 */

void
LispPrintType (fp,l)
     FILE *fp;
     LispObj *l;
{
  switch (LTYPE(l)) {
  case S_INT:
    if (fp == stdout) TxPrintf ("#integer");
    else fprintf (fp, "#integer");
    break;
  case S_FLOAT:
    if (fp == stdout) TxPrintf ("#float");
    else fprintf (fp, "#float");
    break;
  case S_STRING:
    if (fp == stdout) TxPrintf ("#string");
    else fprintf (fp, "#string");
    break;
  case S_BOOL:
    if (fp == stdout) TxPrintf ("#boolean");
    else fprintf (fp, "#boolean");
    break;
  case S_SYM:
    if (fp == stdout) TxPrintf ("#symbol");
    else fprintf (fp, "#symbol");
    break;
  case S_LIST:
    if (fp == stdout) TxPrintf ("#list");
    else fprintf (fp, "#list");
    break;
  case S_LAMBDA:
    if (fp == stdout) TxPrintf ("#proc-userdef");
    else fprintf (fp, "#proc-userdef");
    break;
  case S_LAMBDA_BUILTIN:
    if (fp == stdout) TxPrintf ("#proc-builtin");
    else fprintf (fp, "#proc-builtin");
    break;
  case S_MAGIC_BUILTIN:
    if (fp == stdout) TxPrintf ("#proc-magic");
    else fprintf (fp, "#proc-magic");
    break;
  default:
    break;
  }
}
