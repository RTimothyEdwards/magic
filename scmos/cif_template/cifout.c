
#ifdef STANDARD

/* standard CMOS processes, ORBIT specifically... */
/* 3.0 micron technology - not supported anymore
#define lambda_value 150
#include "calc.lambda"
#include "cifout.gen"
#include "cifout.nw"
#include "cifout.pw"
*/

/* ORBIT 2.0 micron technology */
#define lambda_value 100
#include "calc.lambda"
#include "cifout-orbit.gen"
#include "cifout-orbit.nw"
#include "cifout-orbit.pw"
/*  just testing...
#include "cifout.test"
*/

/* ORBIT 1.6 micron technology - not supported, but retained for ORBIT */
#define lambda_value 80
#include "calc.lambda"
#include "cifout-ami16.gen"
/*  just the generic style is enough at the moment...
#include "cifout-ami16.nw"
#include "cifout-ami16.pw"
 */

/* ORBIT 1.2 micron technology - ORBIT 1.2 micron process supported... */
#define lambda_value 60
#include "calc.lambda"
#include "cifout-orbit.gen"
#include "cifout-orbit.nw"
#include "cifout-orbit.pw"

/* 1.2 micron technology - for Mentor Graphics CMOSN technology */
/* not supported yet, for my own test... */
/*
#define lambda_value 20
#include "calc.lambda"
#include "cifout-cmosn.nw"
*/

#endif          /* Standard SCMOS technologies */

#ifdef TIGHTMETAL

/* HP CMOS34 1.2 micron technology */
#define lambda_value 60
#include "calc.lambda"
#include "cifout-cmos34.gen"
#include "cifout-cmos34.nw"
#include "cifout-cmos34.pw"

/* HP CMOS26b 1.0 micron technology */
#undef cif_tech
#define cif_tech sub
#define lambda_value 50
#include "calc.lambda"
#include "cifout-cmos26b.gen"

/* HP CMOS26b 1.0 micron technology */
/* This is used for generation of standard 2.0 micron layout for Cadence tool */
/*
#define lambda_value 100
#include "calc.lambda"
#include "cifout-cmos26b.gen"
 */

/* HP CMOS14b 0.6 micron technology */
#undef cif_tech
#define cif_tech sub
#define lambda_value 35
#include "calc.lambda"
#include "cifout-cmos14b.gen"

#endif           /* Tight Metal SCMOS technologies */

#ifdef SUBMICRON

/* HP CMOS26G 0.8 micron technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifout-cmos26g.gen"

/* HP CMOS14B 0.6 micron technology */
#define cif_tech sub
#define lambda_value 30
#include "calc.lambda"
#include "cifout-cmos14b-sub.gen"

/* This one is used for an internal SOI technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifout-cmos26g.soi"

/* This one is used for an internal SOI technology */
#define cif_tech cmosx
#define lambda_value 40
#include "calc.lambda"
#include "cifout-cmosx.gen"

#endif           /* HP CMOS26G and HP CMOS14B */


#ifdef IBM

/* IBM CMSX2185 0.8 micron technology */
#define lambda_value 40
#include "calc.lambda"
#include "cifout-ibm.gen"

#endif           /* IBM */
