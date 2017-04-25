#ifdef STANDARD

/* 3.0 micron technology - not supported anymore
#define lambda_value 150
#include "calc.lambda"
#include "cifin.gen"
#include "cifin.nw"
#include "cifin.oldnw"
#include "cifin.pw"
*/

/* ORBIT 2.0 micron technology */
#define lambda_value 100
#include "calc.lambda"
#include "cifin.gen"
#include "cifin.nw"
#include "cifin.oldnw"
#include "cifin.pw"
#define NOWELL
#include "cifin.gen"
#undef NOWELL

/* ORBIT 1.6 micron technology - not supported, but retained for ORBIT */
#define lambda_value 80
#include "calc.lambda"
#include "cifin-ami16.gen"
#include "cifin.nw"
#include "cifin.oldnw"
#include "cifin.pw"
#define NOWELL
#include "cifin.gen"
#undef NOWELL

/* 1.2 micron technology */
#define lambda_value 60
#include "calc.lambda"
#include "cifin.gen"
#include "cifin.nw"
#include "cifin.oldnw"
#include "cifin.pw"
#define NOWELL
#include "cifin.gen"
#undef NOWELL

/* 1.2 micron technology - for Mentor Graphics CMOSN technology */

#define lambda_value 100
#include "calc.lambda"
#include "cifin-cmosn.gen"


/* all other technology */
/*
#include "cifin.others"
*/

#endif /* Standard SCMOS technologies */

#ifdef TIGHTMETAL

/* HP CMOS34 1.2 micron technology */
#define lambda_value 60
#include "calc.lambda"
#include "cifin.gen"
#include "cifin.nw"
#include "cifin.oldnw"
#define NOWELL
#include "cifin.gen"
#undef NOWELL

/* HP CMOS26B 1.0 micron technology */
#define lambda_value 50
#include "calc.lambda"
#include "cifin-cmos26b.gen"
#include "cifin.nw"
#include "cifin.oldnw"
#define NOWELL
#include "cifin-cmos26b.gen"
#undef NOWELL

/* HP CMOS14B 0.6 micron technology */
#define lambda_value 35
#include "calc.lambda"
#include "cifin-cmos14b.gen"

/*  read HP's layer assignment from Mentor */
/*
#define lambda_value 20
#include "calc.lambda"
#include "cifin-hp-cif.nw"
*/

/*  This is just testing...
#define lambda_value 20
#include "calc.lambda"
#include "cifin.cascade"
*/

#endif      /* Tight Metal Technologies ends here .... */

#ifdef SUBMICRON

/* HP CMOS26G 0.8 micron technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifin-cmos26g.gen"
#define NOWELL
#include "cifin-cmos26g.gen"
#undef NOWELL
#include "cifin.nw"

/* HP CMOS14B 0.6 micron technology */
#define lambda_value 30
#include "calc.lambda"
#include "cifin-cmos14b.gen"
#define NOWELL
#include "cifin-cmos14b.gen"
#undef NOWELL

/* 0.8 micron CMOSX technology */
/*
#define lambda_value 40
#include "calc.lambda"
#include "cifin-cmosx.gen"
#define NOWELL
#include "cifin-cmosx.gen"
#undef NOWELL
*/

#endif      /* HP CMOS26G and CMOS14B process */


#ifdef IBM  /* IBM */
/* 0.8 micron technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifin-ibm.gen"
#define NOWELL
#include "cifin-ibm.gen"
#include "cifin.nw"
#undef NOWELL
#endif      /* IBM */
