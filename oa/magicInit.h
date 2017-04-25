/*******************/
/*   Defines       */
/*******************/

#ifndef _magicinit_h_
#define _magicinit_h_

#include <tcl.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CATCH \
catch (oaException &excp) {\
Tcl_SetObjResult(interp,Tcl_NewStringObj((const oaChar *)excp.getMsg(),-1));\
return TCL_ERROR;\
}


#define getArgString(index) Tcl_GetStringFromObj(objv[index],NULL)
#define getArgInt(index,Int) Tcl_GetIntFromObj(interp,objv[index],&Int)
#define getArgDouble(index,Double) Tcl_GetDoubleFromObj(interp,objv[index],&Double)

extern Tcl_Interp *REX_interp;
extern FILE *REX_debug_file;

#define USE_TCL_STUBS 1

#define TCL_ARGS _ANSI_ARGS_((ClientData clientData, \
    Tcl_Interp *interp, int objc, struct Tcl_Obj * CONST * objv))

#define TCLENTRY() {REX_interp = interp;}

#define TCLFUNC(name) int name TCL_ARGS

#define TCLCMD(name, func) Tcl_CreateObjCommand(REX_interp, name, func, NULL, NULL);

//#define TCLLINK(name, addr, type) Tcl_LinkVar(REX_interp, name, addr, type)

/* Error Macros */

#define INFO(fmt, msg...) \
do {fprintf(stdout,"%s: ",__FUNCTION__); \
fprintf(stdout,fmt, ## msg); \
fputc('\n',stdout);} while (0)

#define WARNING(fmt, msg...) \
do {fprintf(stderr,"Warning in %s at %s:%d: ",__FUNCTION__,__FILE__,__LINE__); \
fprintf(stderr,fmt, ## msg); \
fputc('\n',stderr);} while (0)

#define ERROR(fmt, msg...) \
do { fprintf(stderr,"Error in %s at %s:%d:\n",__FUNCTION__,__FILE__,__LINE__); \
fprintf(stderr,fmt, ## msg); \
fputc('\n',stderr); \
abort(); } while(0)

#define ASSERT(test) \
do {if(!(test)) ERROR("Assertion: %s", #test );} while (0)


extern int __Tcl_Rtn_Error(const char *function,const char *file,unsigned int line,
			   const char *fmt, ...);

#define TCL_RTN_ERROR(fmt, msg...) \
do {\
  __Tcl_Rtn_Error(__FUNCTION__,__FILE__,__LINE__,fmt, ## msg);\
  return TCL_ERROR;\
} while(0)

extern int REX_Tcl_Eval(char *fmt, ...);

extern int REX_Tcl_Error(const char *fmt, ...);

#define TCL_EVAL(fmt, msg...) \
do {\
    if(REX_Tcl_Eval(fmt, ## msg) == TCL_ERROR)\
	TCL_RTN_ERROR(fmt, ## msg);\
} while(0)

#define CHECK_ERR(func) \
if((func) == TCL_ERROR) \
TCL_RTN_ERROR(#func)

/* Return Codes */
#define rOK TCL_OK
#define rERROR TCL_ERROR

#ifndef offsetof
#define offsetof(t,m) (int)(&((t*)0)->m)
#endif

#if defined(__cplusplus)
}
#endif

#endif
