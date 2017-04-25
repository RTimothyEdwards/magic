#ifndef TECHINFO_H
#define TECHINFO_H

#include <tcl.h>
#include <oaDB.h>

// Functions implemented for OA DB access and query
int getTechInfo(const char *techName);
int getUserUnit(const char *techName, char *userUnit, ClientData *cdarg, 
		int (*magicFunc) (const char *techName, char *userUnit,
		ClientData *cdarg) = NULL, 
		oaCellViewType viewType=oacMaskLayout);
int getDBUnitsPerUserUnit(const char *techName, int &dbUPerUU, 
			  ClientData *cdarg, int (*magicFunc) 
			  (const char *techName, int &dbUPerUU, 
			  ClientData *cdarg) = NULL,
			  oaCellViewType viewType=oacMaskLayout);
int openDesign(const char *libName, const char *cellName,
	       const char *viewName);
int closeDesign(const char *libName, const char *cellName,
		const char *viewName);
int closeDesign();
int closeAll();
int getBoundingBox (oaInst *instPtr, const char *instanceName, 
		    const char *defName, int (*magicFunc) 
		    (const char *instName, const char *defName,
		     int llx, int lly, int urx, int ury, 
		     const char *curName, ClientData *cdarg), 
		    ClientData *cdarg, int callBack);

#endif
