/*************************************************************************
 *
 *  lispTrace.c -- 
 *
 *   This module manipulates the stack trace information used for
 *   error reporting.
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
 *  $Id: lispTrace.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/


#include <stdio.h>
#include "lisp/lisp.h"
#include "lispInt.h"
#include "textio/textio.h"
#include "utils/malloc.h"


typedef struct stack {
  struct stack *n;
  char *s;
  struct stack *next;
} TRACE;

static TRACE *current = NULL;

static TRACE *freeQ = NULL;

static
TRACE *
StackNew ()
{
  TRACE *t;
  if (freeQ) {
    t = freeQ;
    freeQ = freeQ->n;
  }
  else {
    t = (TRACE *) mallocMagic((unsigned) (sizeof(TRACE)));
  }
  t->n = NULL;
  return t;
}

static
void
StackFree (t)
     TRACE *t;
{
  t->n = freeQ;
  freeQ = t;
}
    

/*------------------------------------------------------------------------
 *
 *  LispStackPush --
 *
 *      Push a name onto the call stack.
 *
 *  Results:
 *      none.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LispStackPush (name)
     char *name;
{
  TRACE *t;
  t = StackNew();
  t->s = name;
  t->next = current;
  current = t;
}


/*------------------------------------------------------------------------
 *
 *  LispStackPop --
 *
 *      Pop a frame off the evaluation stack.
 *
 *  Results:
 *      none.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LispStackPop ()
{
  TRACE *t;
  t = current;
  if (!current)
    TxError ("Internal error!\n");
  else {
    current = current->next;
    StackFree (t);
  }
}


/*------------------------------------------------------------------------
 *
 *  LispStackDisplay --
 *
 *      Display call stack.
 *
 *  Results:
 *      none.
 *
 *  Side effects:
 *      text appears in window.
 *
 *------------------------------------------------------------------------
 */

void
LispStackDisplay ()
{
  extern Sexp *LispMainFrame;
  LispObj *l;
  TRACE *t = current;
  int i = 0;
  int depth;
  l = LispFrameLookup (LispNewString ("scm-stack-display-depth"),
		       LispMainFrame);
  if (l && LTYPE(l) == S_INT)
    depth = LINTEGER(l);
  else
    depth = 5;
  if (depth > 0)
    TxPrintf ("Stack trace:\n");
  while (t && i < depth) {
    i++;
    TxPrintf ("\tcalled from: %s\n", t->s);
    t = t->next;
  }
  if (i < depth)
    TxPrintf ("\tcalled from: -top-level-\n");
}


/*------------------------------------------------------------------------
 *
 *  LispStackClear --
 *
 *      Clear the call stack.
 *
 *  Results:
 *      none.
 *
 *  Side effects:
 *      none.
 *
 *------------------------------------------------------------------------
 */

void
LispStackClear ()
{
  while (current)
    LispStackPop ();
}
