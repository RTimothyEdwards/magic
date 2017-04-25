#include <iostream>
#include <oaDB.h>
#include <magicOA.h>
#include <magicInit.h>

using namespace std;

Tcl_Interp *REX_interp;
FILE *REX_debug_file;

// get_user_unit callback
int techUserUnit(const char *techName, char *userUnit, ClientData *cdarg)
{
  cout << " callback func - techUserUnit : result - " << userUnit << endl;
  return 0;
}

// get_db_units_per_user_unit callback
int techDBUPerUU(const char *techName, int &dbUPerUU, ClientData *cdarg)
{
  cout << " callback func - techDBUPerUU : result - " << dbUPerUU << endl;
  return 0;
}

// get_boundin_box callback
int boundingBoxInfo(const char *instName, const char *defName,
		    int llx, int lly, int urx, int ury, const char *curName,
		    ClientData *cdarg)
{
  cout << " callback func boundingBoxInfo : result - " 
       << curName << "\n bounding box "
       << llx << " " << lly << " " << urx << " " << ury << endl;
  return 0;
}

int __Tcl_Rtn_Error(const char *function,const char *file,unsigned int line,
		    const char *fmt, ...) {
    char *buf;
    int chars1,chars2;
    va_list msg;
    va_start(msg,fmt);

    chars1 = snprintf(NULL,0,"Error in %s at %s:%d: ",function,file,line);
    chars2 = vsnprintf(NULL,0,fmt,msg);
    
    buf = (char *)malloc(chars1 + chars2 + 1);

    sprintf(buf,"Error in %s at %s:%d: ",function,file,line);
    vsprintf(buf+chars1,fmt,msg);
    
    fputs(buf,stderr);
    fputc('\n',stderr);

    Tcl_SetResult(REX_interp, buf,(Tcl_FreeProc *) free);
    return TCL_OK;
}

int REX_Tcl_Error(const char *fmt, ...) {
    char *buf;
    int chars;
    va_list msg;
    va_start(msg,fmt);
 
    chars = vsnprintf(NULL,0,fmt,msg);
     
    buf = (char *)malloc(chars+1);
 
    vsprintf(buf,fmt,msg);
     
    Tcl_SetResult(REX_interp, buf,(Tcl_FreeProc *) free);
    return TCL_OK;
}

int REX_Tcl_Eval(char *fmt, ...) {
    static char buf2[256];
    char *ptr2=buf2;
    int nchars, rtn;
    va_list msg;
    va_start(msg,fmt);
    
    nchars = vsnprintf(ptr2,256,fmt,msg);
    if(nchars >= 256) {
	ptr2 = (char *)malloc(nchars+1);
	vsprintf(ptr2,fmt,msg);
    }
    
    if(REX_debug_file) {
	fputs(ptr2,REX_debug_file);
	fputc('\n',REX_debug_file);
	fflush(REX_debug_file);
    }
    
    rtn = Tcl_Eval(REX_interp, (char *)ptr2);
    
    if(nchars >= 256)
	free(ptr2);
    return rtn;
}


void helpPrint();
void helpInfo(const char *cmdStr);


TCLFUNC(print_tech_info) {
  TCLENTRY();

  if(objc != 2)
      TCL_RTN_ERROR("Usage: %s tech",getArgString(0));
 
  try {
    getTechInfo(getArgString(1));
  } CATCH
  return TCL_OK;
}

TCLFUNC(get_user_unit) {
  TCLENTRY();
  char uUnit[32];

  if(objc != 2)
    TCL_RTN_ERROR("Usage: %s tech",getArgString(0));
 
  try {
    getUserUnit(getArgString(1), uUnit, NULL, techUserUnit);
    Tcl_Obj *strResult = Tcl_NewStringObj(uUnit, strlen(uUnit));
    //Tcl_SetResult(REX_interp, uUnit, TCL_STATIC);
    Tcl_SetObjResult(REX_interp, strResult);
  } CATCH
  return TCL_OK;
}

TCLFUNC(get_db_units_per_user_unit) {
  TCLENTRY();
  int dbUPerUU;

  if(objc != 2)
      TCL_RTN_ERROR("Usage: %s tech",getArgString(0));
 
  try {
    getDBUnitsPerUserUnit(getArgString(1), dbUPerUU, NULL, techDBUPerUU);
    Tcl_Obj *intResult = Tcl_NewIntObj(dbUPerUU);
    Tcl_SetObjResult(REX_interp, intResult);
  } CATCH
  return TCL_OK;
}

TCLFUNC(open_cell) {
  TCLENTRY();
  int cvIndex;

  if(objc != 4)
      TCL_RTN_ERROR("Usage: %s lib cell view",getArgString(0));

  try {
    openDesign(getArgString(1), getArgString(2), getArgString(3));
    //cvIndex = openDesign(getArgString(1), getArgString(2), getArgString(3));
    //Tcl_Obj *intResult = Tcl_NewIntObj(cvIndex);
    //Tcl_SetObjResult(REX_interp, intResult);
  } CATCH
  return TCL_OK;
}

TCLFUNC(close_cell) {
  TCLENTRY();
  int cellIndex;

  if((objc != 4))
    TCL_RTN_ERROR("Usage: %s lib cell view", getArgString(0));

  try {
    closeDesign(getArgString(1), getArgString(2), getArgString(3));
  } CATCH
  return TCL_OK;
}

TCLFUNC(close_current_cell) {
  TCLENTRY();

  if((objc != 1))
    TCL_RTN_ERROR("Usage: %s",getArgString(0));

  try {
    closeDesign();
  } CATCH
  return TCL_OK;
}

TCLFUNC(close_all_cells) {
  TCLENTRY();

  if((objc != 1))
    TCL_RTN_ERROR("Usage: %s",getArgString(0));

  try {
    closeAll();
  } CATCH
  return TCL_OK;
}

TCLFUNC(get_bounding_box) {
  char *defstring;
  char *inststring;
  int callback = 0;

  TCLENTRY();

  if (objc < 3)
     defstring = "dummy_def";
  else
     defstring = getArgString(2);

  if (objc < 2) {
     inststring = "dummy_inst";
     callback = 1;
  }
  else
     inststring = getArgString(1);

  try {
    getBoundingBox(NULL, inststring, defstring, 
	       boundingBoxInfo, NULL, callback);
  } CATCH
  return TCL_OK;
}

TCLFUNC(help) {
  TCLENTRY();
  if (objc == 2) {
    helpInfo(getArgString(1));
    return TCL_OK;
  }

  try {
    helpPrint();
  } CATCH
  return TCL_OK;
}


struct tclCmd {
  Tcl_ObjCmdProc * cmd;
  char *name;
  char *help;
};

static const struct tclCmd tclCmds[] = {
        // Help
        {help, "help", NULL},
	// MAGIC API
	{print_tech_info, "print_tech_info", "tech"},
	{get_user_unit, "get_user_unit", "tech"},
	{get_db_units_per_user_unit, "get_db_units_per_user_unit", "tech"},
	{open_cell, "open_cell", "lib cell view"},
	{close_cell, "close_cell", "lib cell view"},
	{close_current_cell, "close_current_cell", ""},
	{close_all_cells, "close_all_cells", ""},
	{get_bounding_box, "get_bounding_box", "inst def"}
};

#define tclCmdsNum (sizeof(tclCmds) / sizeof(struct tclCmd))

void
helpPrint() {
  printf ("help <cmd>\n");    
  printf ("where cmd is one of \n");
  for(unsigned int i = 1; i < tclCmdsNum ; i++) {
    printf("  %-24s", tclCmds[i].name);
    if (i%3 == 0)
      printf("\n");
  }
  printf("\n");
}

void
helpInfo(const char *cmdStr) {
  for(unsigned int i = 1; i < tclCmdsNum ; i++) {
    if (!strcmp(cmdStr, tclCmds[i].name)) {
      if (tclCmds[i].help) {
	printf("Usage: %s %s\n", cmdStr, tclCmds[i].help);
      } else {
	printf("Help not implemented\n");
      }
    }
  }
}


extern "C" {

int Magicoa_Init(Tcl_Interp *interp) {

	if (interp == 0) return TCL_ERROR;
	TCLENTRY();

	Tcl_PkgProvide(interp, "magicOA", "0.1");
   
#ifdef USE_TCL_STUBS
    if(Tcl_InitStubs(interp, (char *)"8.1",0) == NULL)
      return TCL_ERROR;
#endif
	try {
	int args=1;
	char *argv[] = {"tclsh"};
	oaDBInit(&args, argv);
 
	} CATCH

	for(unsigned int i = 0; i < tclCmdsNum ; i++) {
	  TCLCMD(tclCmds[i].name,tclCmds[i].cmd);
	}
	//initLink(interp);

	return TCL_OK;
}

} /* extern "C" */
