/*************************************************************************
 *
 *  lispGC.c -- 
 *
 *   This module contains the garbage collector.
 *   (N.B. This is really inefficient!)
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
 *  $Id: lispGC.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "lispargs.h"

/*
  garbage collection trickery. :)
*/
#define GC_TO_PTR(type,x) ((type)(((unsigned long) x) & ~3))

#define GC_SWMARK(type,x,v)   x=(type)((((unsigned long)x)&~3) | ((v)&3))
#define GC_SWMARKED(x,v)      ((((unsigned long) x)&3) == (v))
#define GC_MARKVAL(x)         (((unsigned long)x)&3)

#define GC_MARK(type,x) GC_SWMARK(type,x,1)
#define GC_UNMARK(type,x) GC_SWMARK(type,x,0)
#define GC_MARKED(x)     GC_SWMARKED(x,1)

static Sexp *SexpMainAllocQ = NULL;
static Sexp *SexpMainAllocQTail = NULL;

static Sexp *SexpAllocQ = NULL;
static Sexp *SexpAllocQTail = NULL;

static Sexp *SexpFreeQ = NULL;
static Sexp *SexpFreeQTail = NULL;

static LispObj *LispObjMainAllocQ = NULL;
static LispObj *LispObjMainAllocQTail = NULL;

static LispObj *LispObjAllocQ = NULL;
static LispObj *LispObjAllocQTail = NULL;

static LispObj *LispObjFreeQ = NULL;
static LispObj *LispObjFreeQTail = NULL;

static Sexp *MarkedSexpQ;
static LispObj *MarkedObjQ;

int LispGCHasWork;
int LispCollectAllocQ;

/*-----------------------------------------------------------------------------
 *
 *  LispNewObj --
 *
 *      Get a new object from the free list.
 *
 *  Results:
 *      Returns the new object.
 *
 *  Side effects:
 *      Modifies free list.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispNewObj ()
{
  LispObj *s;
  if (LispObjFreeQ) {
    s = LispObjFreeQ;
    LispObjFreeQ = LispObjFreeQ->n;
    if (LTYPE(s) == S_STRING)
      freeMagic(LSTR(s));
  }
  else {
    s = (LispObj *) mallocMagic((unsigned) (sizeof(LispObj)));
  }
  s->t = S_INT;
  s->u.l = NULL;
  if (!LispObjAllocQ)
    LispObjAllocQTail = s;
  s->n = LispObjAllocQ;
  LispObjAllocQ = s;
  return s;
}


/*-----------------------------------------------------------------------------
 *
 *  LispCopyObj --
 *
 *      Create a copy of an object.
 *
 *  Results:
 *      Returns the new object.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispCopyObj (l)
     LispObj *l;
{
  LispObj *s;
  s = LispNewObj ();
  LTYPE(s) = LTYPE(l);
  switch (LTYPE(s)) {
  case S_LIST:
    LLIST(s) = LLIST(l);
    break;
  case S_SYM:
    LSYM(s) = LSYM(l);
    break;
  case S_MAGIC_BUILTIN:
    LSYM(s) = LSYM(l);
    break;
  case S_LAMBDA_BUILTIN:
    LBUILTIN(s) = LBUILTIN(l);
    break;
  case S_LAMBDA:
    LUSERDEF(s) = LUSERDEF(l);
    break;
  case S_INT:
    LINTEGER(s) = LINTEGER(l);
    break;
  case S_FLOAT:
    LFLOAT(s) = LFLOAT(l);
    break;
  case S_STRING:
    LSTR(s) = (char *) mallocMagic((unsigned) (strlen(LSTR(l))+1));
    strcpy (LSTR(s),LSTR(l));
    break;
  case S_BOOL:
    LBOOL(s) = LBOOL(l);
    break;
  default:
    TxError ("Fatal error in copy-object!\n");
    break;
  }
  return s;
}


/*-----------------------------------------------------------------------------
 *
 *  LispNewSexp --
 *
 *      Get a Sexp from the free list.
 *
 *  Results:
 *      Returns the new Sexp.
 *
 *  Side effects:
 *      Modifies the free list.
 *
 *-----------------------------------------------------------------------------
 */

Sexp *
LispNewSexp ()
{
  Sexp *s;
  if (SexpFreeQ) {
    s = SexpFreeQ;
    SexpFreeQ = SexpFreeQ->n;
  }
  else {
    s = (Sexp *) mallocMagic((unsigned) (sizeof(Sexp)));
  }
  CAR(s) = NULL;
  CDR(s) = NULL;
  if (!SexpAllocQ)
    SexpAllocQTail = s;
  s->n = SexpAllocQ;
  SexpAllocQ = s;
  return s;
}

/*-----------------------------------------------------------------------------
 *
 *  LispCopySexp --
 *
 *      Return a copy of an Sexp.
 *
 *  Results:
 *      Returns the new Sexp.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Sexp *
LispCopySexp (s)
     Sexp *s;
{
  Sexp *t;
  t = LispNewSexp ();
  CAR(t) = CAR(s);
  CDR(t) = CDR(s);
  return t;
}


/*========================================================================*/

/*
 *
 *
 * The garbage collector is rather interesting. It has two modes of
 * operation. When evaluation has not executed any "define", "set!", 
 * "set-car!", or "set-cdr!" commands (the only ones which have side-effects),
 * everything allocated during the last evaluation is collected without
 * running a marking algorithm. As a result, normal magic commands will
 * be executed with O(1) garbage collection overhead. Note that strings
 * are free'd when the object is reused.
 * 
 * Scheme functions without side-effects will also execute with O(1)
 * garbage collection overhead.
 * 
 * The full mark/collect algorithm is executed once every 50 times the
 * real garbage collector is called :P This "constant-factor"
 * speedup actually makes a big difference in practice.
 *
 * The marking algorithm is O(NUSE), where NUSE = # of used nodes. We use
 * the Schorr-Waite-Deutch algorithm for a non-recursive traversal of the
 * nodes in use.
 *
 * The collection phase is O(NALLOC), where NALLOC = # of allocated nodes.
 * It could be reduced to O(NUSE) by changing the allocation list to one
 * that is doubly-linked.
 *
 *
 */


/*
 *
 * Merge the main and current alloc q into the current alloc q.
 *
 */
static
void
mergealloc ()
{
  Sexp *s;
  LispObj *l;

  if (!SexpAllocQ) {
    SexpAllocQ = SexpMainAllocQ;
    SexpAllocQTail = SexpMainAllocQTail;
  }
  else {
    SexpAllocQTail->n = SexpMainAllocQ;
    if (SexpMainAllocQ)
      SexpAllocQTail = SexpMainAllocQTail;
  }
  if (!LispObjAllocQ) {
    LispObjAllocQ = LispObjMainAllocQ;
    LispObjAllocQTail = LispObjMainAllocQTail;
  }
  else {
    LispObjAllocQTail->n = LispObjMainAllocQ;
    if (LispObjMainAllocQ)
      LispObjAllocQTail = LispObjMainAllocQTail;
  }
}


#define NIL(x)  ((LTYPE(x) != S_LIST && LTYPE(x) != S_LAMBDA) || (LLIST(x) == NULL))

static
void
mark_sw (l)
     LispObj *l;
{
  LispObj *m;
  LispObj *t0,*t1,*t2,*t3;
  int mark;

  /* mark all the nodes */
  m = NULL;
  while (l != NULL) {
    GC_SWMARK(LispObj *, l->n, 1);
    GC_SWMARK(Sexp *, LLIST(l)->n, GC_MARKVAL (LLIST(l)->n)+1);
    if (GC_MARKVAL(LLIST(l)->n) == 3 || 
	(!NIL(CAR(LLIST(l))) &&  GC_MARKVAL(LLIST(CAR(LLIST(l)))->n) == 0)) {
      t0=l; t1=CAR(LLIST(l)); t2=CDR(LLIST(l)); t3=m;
      CAR(LLIST(l))=t2; CDR(LLIST(l))=t3; m=t0; l=t1;
    }
    else {
      GC_SWMARK (LispObj *, CAR(LLIST(l))->n, 1);
      t0=CAR(LLIST(l)); t1=CDR(LLIST(l)); t2=m;
      CAR(LLIST(l))=t1; CDR(LLIST(l))=t2; m=t0;
    }
  }
}

static 
void
collect_sw ()
{
  /* all marked objects have non-zero mark */
  /* stoll through the alloc Q and split it into two things:
     freeQ, mainallocQ
  */
  SexpMainAllocQ = NULL;
  while (SexpAllocQ) {
    if (GC_MARKVAL (SexpAllocQ->n) != 0) {
      /* used */
      if (!SexpMainAllocQ) {
	SexpMainAllocQ = SexpAllocQ;
	SexpMainAllocQTail = SexpAllocQ;
      }
      else {
	SexpMainAllocQTail->n = SexpAllocQ;
	SexpMainAllocQTail = SexpAllocQ;
      }
    }
    else {
      if (!SexpFreeQ) {
	SexpFreeQ = SexpAllocQ;
	SexpFreeQTail = SexpAllocQ;
      }
      else {
	SexpFreeQTail->n = SexpAllocQ;
	SexpFreeQTail = SexpAllocQ;
      }
    }
    SexpAllocQ = GC_TO_PTR(Sexp*, SexpAllocQ->n);
  }
  if (SexpFreeQ)
    SexpFreeQTail->n = NULL;
  if (SexpMainAllocQ)
    SexpMainAllocQTail->n = NULL;
  SexpAllocQ = NULL;

  LispObjMainAllocQ = NULL;
  while (LispObjAllocQ) {
    if (GC_MARKVAL (LispObjAllocQ->n) != 0) {
      /* used */
      if (!LispObjMainAllocQ) {
	LispObjMainAllocQ = LispObjAllocQ;
	LispObjMainAllocQTail = LispObjAllocQ;
      }
      else {
	LispObjMainAllocQTail->n = LispObjAllocQ;
	LispObjMainAllocQTail = LispObjAllocQ;
      }
    }
    else {
      if (LTYPE(LispObjAllocQ) == S_STRING) {
	freeMagic(LSTR(LispObjAllocQ));
	LTYPE(LispObjAllocQ) = S_INT;
      }
      if (!LispObjFreeQ) {
	LispObjFreeQ = LispObjAllocQ;
	LispObjFreeQTail = LispObjAllocQ;
      }
      else {
	LispObjFreeQTail->n = LispObjAllocQ;
	LispObjFreeQTail = LispObjAllocQ;
      }
    }
    LispObjAllocQ = GC_TO_PTR(LispObj*, LispObjAllocQ->n);
  }
  if (LispObjFreeQ)
    LispObjFreeQTail->n = NULL;
  if (LispObjMainAllocQ)
    LispObjMainAllocQTail->n = NULL;
  LispObjAllocQ = NULL;
}
  

/*-----------------------------------------------------------------------------
 *
 *  LispGC --
 *
 *      Run the garbage collector, assuming all reachable nodes are reachable
 *      from the Sexp passed to the garbage collector.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
static int skip_gc = 50;	/* make this bigger if it is too slow :) */

void
LispGC (fl)
     LispObj *fl;
{
  LispObj *l;
  
  if (LispCollectAllocQ) {
    /*
     * The last evaluation did not have any side-effects.
     * Collect everything allocated on the last pass and put it
     * back into the free list.
     *
     */
    if (LispObjFreeQ) {
      LispObjFreeQTail->n = LispObjAllocQ;
      if (LispObjAllocQ)
	LispObjFreeQTail = LispObjAllocQTail;
    }
    else {
      LispObjFreeQ = LispObjAllocQ;
      LispObjFreeQTail = LispObjAllocQTail;
    }
    if (SexpFreeQ) {
      SexpFreeQTail->n = SexpAllocQ;
      if (SexpAllocQ)
	SexpFreeQTail = SexpAllocQTail;
    }
    else {
      SexpFreeQ = SexpAllocQ;
      SexpFreeQTail = SexpAllocQTail;
    }
    LispObjAllocQ = NULL;
    SexpAllocQ = NULL;
    return;
  }
  if (skip_gc-- > 0) {
    LispGCHasWork = 1;
    mergealloc ();
    SexpMainAllocQ = SexpAllocQ;
    SexpMainAllocQTail = SexpAllocQTail;
    LispObjMainAllocQ = LispObjAllocQ;
    LispObjMainAllocQTail = LispObjAllocQTail;
    SexpAllocQ = NULL;
    LispObjAllocQ = NULL;
    return;
  }
  skip_gc = 50;

  if (fl) {
    extern Sexp *LispMainFrame;

    l = LispFrameLookup (LispNewString ("scm-gc-frequency"), LispMainFrame);
    if (l && LTYPE(l) == S_INT && LINTEGER(l)  >= 0)
      skip_gc = LINTEGER(l);
  }

  if (fl) {
    mergealloc ();
    mark_sw(fl);
    collect_sw ();
    LispObjAllocQ = NULL;
  }
  LispGCHasWork = 0;
}


/*-----------------------------------------------------------------------------
 *
 *  LispCollectGarbage --
 *
 *      Force garbage collection after this evaluation.
 *
 *  Results:
 *      Returns #t
 *
 *  Side effects:
 *      none.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispCollectGarbage (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  extern LispObj *LispMainFrameObj;
  
  if (ARG1P(s)) {
    TxPrintf ("Usage: (%s)\n", name);
    RETURN;
  }
  skip_gc = 0;
  LispCollectAllocQ = 0;
  LispGC (LispMainFrameObj);
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}



/*------------------------------------------------------------------------
 *
 *  LispGCAddSexp --
 *
 *      Add an sexp to the list of roots used for garbage collection.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      Modifies LispMainFrameObj list.
 *
 *------------------------------------------------------------------------
 */

void LispGCAddSexp (s)
     Sexp *s;
{
  extern LispObj *LispMainFrameObj;
  Sexp *t;

  t = LispNewSexp ();
  CAR(t) = LispNewObj ();
  LTYPE(CAR(t)) = S_LIST;
  LLIST(CAR(t)) = s;
  CDR(t) = LispMainFrameObj;
  
  LispMainFrameObj = LispNewObj ();
  LTYPE(LispMainFrameObj) = S_LIST;
  LLIST(LispMainFrameObj) = t;
}



/*------------------------------------------------------------------------
 *
 *  LispGCRemoveSexp --
 *
 *      Remove an Sexp from the list of roots for garbage collection.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      Modifies LispMainFrameObj list.
 *
 *------------------------------------------------------------------------
 */

void LispGCRemoveSexp (s)
     Sexp *s;
{
  extern LispObj *LispMainFrameObj;
  LispObj *l;
  
  if (LLIST(CAR(LLIST(LispMainFrameObj))) != s) {
    TxError ("Fatal internal error. Proceed at your own risk!\n");
    return;
  }

  LispMainFrameObj = CDR(LLIST(LispMainFrameObj));
}
