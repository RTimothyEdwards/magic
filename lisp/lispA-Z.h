/*************************************************************************
 *
 *  lispA-Z.h -- 
 *
 *   Declarations for builtins.
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
 *  $Id: lispA-Z.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp.h"
#include "lispInt.h"


/*
 * predicates
 */
extern LispObj *LispIsBool ();
extern LispObj *LispIsSym ();
extern LispObj *LispIsList ();
extern LispObj *LispIsPair ();
extern LispObj *LispIsNumber ();
extern LispObj *LispIsString ();
extern LispObj *LispIsProc ();
extern LispObj *LispEqv ();

/*
 * lists
 */
extern LispObj *LispCar ();
extern LispObj *LispCdr ();
extern LispObj *LispCons ();
extern LispObj *LispNull ();
extern LispObj *LispList ();
extern LispObj *LispLength ();

/*
 * controlling evaluation
 */
extern LispObj *LispQuote ();
extern LispObj *Lispeval ();
extern LispObj *LispLambda ();
extern LispObj *Lispapply ();

/*
 * definitions and side-effects
 */
extern LispObj *LispDefine ();
extern LispObj *LispSetBang ();
extern LispObj *LispLet ();
extern LispObj *LispLetRec ();
extern LispObj *LispLetStar ();
extern LispObj *LispSetCarBang ();
extern LispObj *LispSetCdrBang ();

/*
 * arithmetic
 */
extern LispObj *LispAdd ();
extern LispObj *LispSub ();
extern LispObj *LispMult ();
extern LispObj *LispDiv ();
extern LispObj *LispTruncate ();
extern LispObj *LispZeroQ ();
extern LispObj *LispPositiveQ ();
extern LispObj *LispNegativeQ ();

/*
 * control flow
 */
extern LispObj *LispBegin ();
extern LispObj *LispIf ();
extern LispObj *LispCond ();

/*
 * debugging
 */
extern LispObj *LispShowFrame ();
extern LispObj *LispDisplayObj ();
extern LispObj *LispPrintObj ();
extern LispObj *LispError ();

/*
 * I/O
 */
extern LispObj *LispLoad ();
extern LispObj *LispWrite ();
extern LispObj *LispSpawn ();
extern LispObj *LispWait ();

/*
 * utilities
 */
extern LispObj *LispCollectGarbage ();

/*
 *  String functions
 */
extern LispObj *LispStrCat ();
extern LispObj *LispSymbolToString ();
extern LispObj *LispStringToSymbol ();
extern LispObj *LispNumberToString ();
extern LispObj *LispStringToNumber ();
extern LispObj *LispStringLength ();
extern LispObj *LispStringCompare ();
extern LispObj *LispStringRef ();
extern LispObj *LispStringSet ();
extern LispObj *LispSubString ();

/*
 *  magic interaction
 */
extern LispObj *LispGetPaint ();
extern LispObj *LispGetSelPaint ();
extern LispObj *LispGetbox ();
extern LispObj *LispGetPoint ();
extern LispObj *LispGetLabel ();
extern LispObj *LispGetSelLabel ();
extern LispObj *LispGetCellNames ();
extern LispObj *LispEvalMagic ();
