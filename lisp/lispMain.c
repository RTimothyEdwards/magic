/*************************************************************************
 *
 *  lispMain.c -- 
 *
 *   This module contains the mini-scheme interpreter interface.
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
 *  $Id: lispMain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "utils/malloc.h"


int lispInFile;			/* global variable used within the lisp
				   module used to figure out whether we're
				   in a file */


/*------------------------------------------------------------------------
 *
 *  LispEvaluate --
 *
 *      Evaluate the command-line as a lisp expression, and generate
 *      a list of commands in a local Cmd queue.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */
void
LispEvaluate (argc, argv, inFile)
     int argc;
     char **argv;
     int inFile;
{
  extern Sexp *LispMainFrame;
  extern LispObj *LispMainFrameObj;
  LispObj *l, *res;
  char output_buf[LISP_MAX_LEN];
  int i,j,k;
  static int my_depth = 0;
  int old_infile;

  old_infile = lispInFile;
  lispInFile = inFile;
  my_depth ++;
  /*
   * convert input string into a lisp line.
   */
  k=0;
  output_buf[k++] = '(';
  for (i=0; i < argc; i++) {
    for (j=0; argv[i][j]; j++) {
      if (argv[i][j] < 32)
	output_buf[k++] = '\\';
      output_buf[k++] = argv[i][j];
    }
    output_buf[k++] = ' ';
  }
  output_buf[k++] = ')';
  output_buf[k] = '\0';

  if (my_depth == 1)
    LispCollectAllocQ = 1;

  l = LispFrameLookup (LispNewString ("scm-echo-parser-input"),
		       LispMainFrame);
  if (l && LTYPE(l) == S_BOOL && LBOOL(l)) 
    TxPrintf (" [ %s ]\n", output_buf);
  l = LispParseString (output_buf);
  res = LispFrameLookup (LispNewString ("scm-echo-parser-output"),
			 LispMainFrame);
  if (l) {
    if (res && LTYPE(res) == S_BOOL && LBOOL(res)) {
      TxPrintf (" >> ");
      LispPrint (stdout, l);
      TxPrintf ("\n\n");
    }
    if (SigInterruptOnSigIO >= 0) SigInterruptOnSigIO = 0;
    SigInterruptPending = FALSE;
    res = LispEval(l,LispMainFrame);
  }
  if (l && res) {
    l = LispFrameLookup (LispNewString ("scm-echo-result"),LispMainFrame);
    if (l && LTYPE(l) == S_BOOL && LBOOL(l)) {
      LispPrint (stdout, res);
      TxPrintf ("\n");
    }
  }
  else {
    if (SigInterruptPending)
      TxPrintf ("[Evaluation Interrupted]\n");
  }
  /* collect garbage */
  if (my_depth == 1)
    LispGC (LispMainFrameObj);
  my_depth--;
  lispInFile = old_infile;
}


/*------------------------------------------------------------------------
 *
 *  LispInit --
 *
 *      Initialize lisp builtins.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LispInit ()
{
  extern LispObj *LispMainFrameObj;

  LispMainFrameObj = LispNewObj ();
  LTYPE(LispMainFrameObj) = S_LIST;
  LLIST(LispMainFrameObj) = NULL;

  LispFnInit ();
  LispFrameInit ();
  LispGCHasWork = 0;
  LispCollectAllocQ = 0;
  LispGC (LispMainFrameObj);

  LispSetEdit ("*unknown*");
}


/*------------------------------------------------------------------------
 *
 *  LispSetTech --
 *
 *      Sets the scheme variable "technology" to the technology name.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LispSetTech (s)
     char *s;
{
  extern Sexp *LispMainFrame;
  extern LispObj *LispMainFrameObj;
  LispObj *l, *m;

  m = LispNewObj ();
  LTYPE(m) = S_SYM;
  LSYM(m) = LispNewString ("technology");
  l = LispNewObj ();
  LTYPE(l) = S_STRING;
  LSTR(l) = (char *) mallocMagic((unsigned) (strlen(s)+1));
  strcpy (LSTR(l),s);
  LispAddBinding (m, l, LispMainFrame);
  LispCollectAllocQ = 0;
  LispGC (LispMainFrameObj);
}



/*------------------------------------------------------------------------
 *
 *  LispSetEdit --
 *
 *      Sets the edit cell name
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LispSetEdit (s)
     char *s;
{
  extern Sexp *LispMainFrame;
  extern LispObj *LispMainFrameObj;
  LispObj *l, *m;

  m = LispNewObj ();
  LTYPE(m) = S_SYM;
  LSYM(m) = LispNewString ("edit-cell");
  l = LispNewObj ();
  LTYPE(l) = S_STRING;
  LSTR(l) = (char *) mallocMagic((unsigned) (strlen(s)+1));
  strcpy (LSTR(l),s);
  LispAddBinding (m, l, LispMainFrame);
  LispCollectAllocQ = 0;
  LispGC (LispMainFrameObj);
}
