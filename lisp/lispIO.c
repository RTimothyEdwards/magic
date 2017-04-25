/*************************************************************************
 *
 *  lispIO.c --
 *
 *   This module contains the builtin mini-scheme I/O functions.
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
 *  $Id: lispIO.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "lispargs.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "utils/signals.h"

/*-----------------------------------------------------------------------------
 *
 *  LispLoad --
 *
 *      ("load-scm" "filename")
 *      Reads and evaluates file.
 *      
 *
 *  Results:
 *      #t => file was opened successfully.
 *      #f => failure.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispLoad (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  extern int LispEchoResult;
  LispObj *l, *inp, *res;
  FILE *fp;
  int val, pos;
  char *buffer, *tmp;
  int buflen;
  int nest;
  int line;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s string)\n", name);
    RETURN;
  }
  l = LispFrameLookup (LispNewString ("scm-library-path"), f);
  if (!l)
    tmp = NULL;
  else if (LTYPE(l) != S_STRING) {
    TxPrintf ("%s: scm-library-path is not a string\n", name);
    RETURN;
  }
  else
    tmp = LSTR(l);
  if (!(fp = PaOpen (LSTR(ARG1(s)), "r", NULL, ".", tmp, NULL))) {
    TxPrintf ("%s: could not open file %s for reading\n",name,LSTR(ARG1(s)));
    l = LispNewObj ();
    LTYPE(l) = S_BOOL;
    LBOOL(l) = 0;
    RETURN;
  }

  LispGCAddSexp (s);

  buffer = (char *) mallocMagic((unsigned) (buflen = 4096));
  pos = 0;
  nest = 0;
  line = 1;
  while ((val = fgetc (fp)) != EOF) {
    if (pos == buflen) {
      int i;
      /* extend buffer */
      tmp = buffer;
      buflen += 1024;
      buffer = (char *) mallocMagic((unsigned) buflen);
      for (i=0; i < pos; i++)
	buffer[i] = tmp[i];
      freeMagic(tmp);
    }
    if (val == ';') {
      /* skip to eol */
      while ((val = fgetc(fp)) != EOF && val != '\n') 
	;
      if (val == '\n') line++;
      continue;
    }
    if (val == '\n') line++;
    if (val == '\t' || val == '\n') val = ' ';
    /* skip white space at nesting level zero */
    if (nest == 0 && isspace (val)) 
      continue;
    if (nest == 0 && val != '(') {
      TxPrintf ("Error reading file %s, line %d\n", LSTR(ARG1(s)), line);
      l = LispNewObj ();
      LTYPE(l) = S_BOOL;
      LBOOL(l) = 0;
      freeMagic(buffer);
      fclose (fp);
      LispGCRemoveSexp (s);
      RETURN;
    }
    buffer[pos++] = val;
    if (val == '(')
      nest++;
    else if (val == ')') {
      nest--;
      if (nest == 0) {
	buffer[pos] = '\0';
	inp = LispParseString (buffer);
	if (inp) {
	  res = LispEval (inp, f);
	  inp = LispFrameLookup (LispNewString ("scm-echo-result"), f);
	  if (res && inp && LTYPE(inp) == S_BOOL && LBOOL(inp)) {
	    LispPrint (stdout,res);
	    TxPrintf ("\n");
	  }
	  if (!res) {
	    if (!SigInterruptPending)
	      TxPrintf ("Error evaluating file %s, line %d\n",
			LSTR(ARG1(s)), line);
	    freeMagic(buffer);
	    fclose (fp);
	    LispGCRemoveSexp (s);
	    RETURN;
	  }
	}
	else {
	  TxPrintf ("Error parsing file %s, line %d\n", LSTR(ARG1(s)), line);
	  freeMagic(buffer);
	  fclose (fp);
	  LispGCRemoveSexp (s);
	  RETURN;
	}
	pos = 0;
      }
      if (nest < 0) {
	TxPrintf ("Error reading file %s, line %d\n", LSTR(ARG1(s)), line);
	l = LispNewObj ();
	LTYPE(l) = S_BOOL;
	LBOOL(l) = 0;
	freeMagic(buffer);
	fclose (fp);
	LispGCRemoveSexp (s);
	return l;
      }
    }
    else if (val == '\"') {
      while ((val = fgetc (fp)) != EOF && val != '\"') {
	if (val == '\n') line++;
	if (pos > buflen-1) {
	  /* extend buffer */
	  int i;
	  tmp = buffer;
	  buflen += 1024;
	  buffer = (char *) mallocMagic((unsigned) (buflen));
	  for (i=0; i < pos; i++)
	    buffer[i] = tmp[i];
	  freeMagic(tmp);
	}
	buffer[pos++] = val;
	if (val == '\\') {
	  val = fgetc (fp);
	  buffer[pos++] = val;
	  if (val == '\n') line++;
	}
      }
      if (val == EOF) {
	TxPrintf ("Error reading file %s, line %d\n", LSTR(ARG1(s)), line);
	freeMagic(buffer);
	fclose (fp);
	LispGCRemoveSexp (s);
	RETURN;
      }
      buffer[pos++] = val;
    }
  }
  freeMagic(buffer);
  fclose (fp);
  if (pos > 0) {
    TxPrintf ("Error reading file %s, line %d\n", LSTR(ARG1(s)), line);
    LispGCRemoveSexp (s);
    RETURN;
  }
  else  {
    LispGCRemoveSexp (s);
    l = LispNewObj ();
    LTYPE(l) = S_BOOL;
    LBOOL(l) = 1;
  }
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispWrite --
 *
 *      Write an object to a file.
 *
 *  Results:
 *      none.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispWrite (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  FILE *fp;
  LispObj *l;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || !ARG2P(s) || ARG3P(s)) {
    TxPrintf ("Usage: (%s str obj)\n", name);
    RETURN;
  }
  if (!(fp = PaOpen (LSTR(ARG1(s)), "a", NULL, ".", NULL, NULL))) {
    TxPrintf ("%s: could not open file %s for writing\n",name,LSTR(ARG1(s)));
    RETURN;
  }
  fprintf (fp, ";\n");
  LispPrint (fp,ARG2(s));
  fprintf (fp, "\n");
  fclose (fp);
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispSpawn --
 *
 *      (spawn list-of-strings)
 *      Reads and evaluates file.
 *      
 *
 *  Results:
 *      pid => the pid of the spawned process.
 *      -1  => if spawn failed.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispSpawn (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  int pid;
  Sexp *t;
  char **argv;
  int n;

  if (!ARG1P(s)) {
    TxPrintf ("Usage: (%s string-list)\n", name);
    RETURN;
  }

  t = s;
  n = 1;
  while (ARG1P(t)) {
    if (LTYPE(CAR(t)) != S_STRING) {
      TxPrintf ("Usage: (%s string-list)\n", name);
      RETURN;
    }
    n++;
    t = LLIST(CDR(t));
  }
  argv = (char **) mallocMagic((unsigned) (sizeof(char*)*n));
  t = s;
  n = 0;
  while (ARG1P(t)) {
    argv[n] = LSTR(CAR(t));
    n++;
    t = LLIST(CDR(t));
  }
  argv[n] = NULL;
  
  FORK_f(pid);
  if (pid < 0) {
    TxPrintf ("Error: could not fork a process!\n");
    freeMagic(argv);
    RETURN;
  }
  else if (pid == 0) {
    int i;
    /* try closing all files, so that we don't mess up the state of
     the parent */
    for (i=3; i < 256; i++)
	close (i);
    execvp (argv[0], argv);
    _exit (1000);
  }
  freeMagic(argv);
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  LINTEGER(l) = pid;
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispWait --
 *
 *      (wait pid)
 *      Wait for pid to terminate.
 *
 *  Results:
 *      The status, error if the pid is an invalid pid.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispWait (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  int stat;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_INT) {
    TxPrintf ("Usage: (%s pid)\n", name);
    RETURN;
  }
  
  if (WaitPid (LINTEGER(ARG1(s)), &stat) < 0) {
    TxPrintf ("%s: waiting for an invalid pid\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  LINTEGER(l) = stat;
  return l;
}
