#include <iostream>
#include <cstring>
#include <string.h>
#include <magicOA.h>

#ifdef __cplusplus
#define __restrict /*restrict*/
#define EXTERN_C extern "C"
#else
#define __restrict restrict
#define EXTERN_C extern
#endif
#ifndef rsize_t
#define rsize_t size_t
#endif
EXTERN_C /*errno_t*/ int strcpy_s(char *__restrict dest, rsize_t destsz, const char *__restrict src); // glibc does not provide C11 Annex K

#ifndef __STDC_LIB_EXT1__
/*
 * Inline fallback for platforms whose C library does not implement the C11
 * Annex K bounds-checked interfaces (notably glibc, which never defines
 * __STDC_LIB_EXT1__).  Without this the declaration above would be an
 * unresolved symbol at link time.  Copies src into dest (capacity destsz,
 * including the terminating NUL); on a bad argument or on truncation it empties
 * dest and returns non-zero, matching strcpy_s's error contract closely enough
 * for our use.
 */
EXTERN_C int strcpy_s(char *__restrict dest, rsize_t destsz, const char *__restrict src) {
    rsize_t i;
    if (dest == NULL || destsz == 0) {
	return 22;			/* ~EINVAL */
    }
    if (src == NULL) {
	dest[0] = '\0';
	return 22;			/* ~EINVAL */
    }
    for (i = 0; i + 1 < destsz && src[i] != '\0'; i++) {
	dest[i] = src[i];
    }
    if (src[i] != '\0') {		/* src did not fit */
	dest[0] = '\0';
	return 34;			/* ~ERANGE */
    }
    dest[i] = '\0';
    return 0;
}
#endif /* __STDC_LIB_EXT1__ */

using namespace std;

// current open cell view
oaCellView *curCellView;


int
getTechInfo (const char *techName)
{
  char argvbuf[] = "tclsh";	/* mutable: oaDBInit takes char** */
  char *argArray[] = {argvbuf};
  int argCount = 1;
  // initialize OA DB
  oaDBInit( &argCount, argArray );

  // open OA tech DB
  //oaDefNS ns;
  oaNativeNS ns;
  oaScalarName chipTechName(ns, (oaChar *) techName);
  oaTech *chipTech = oaTech::open(chipTechName);

  // get route specs from DB
  cout << "Get route spec info " << endl;
  oaIter<oaRouteSpec> routeSpecIter (chipTech->getRouteSpecs());
  while (oaRouteSpec *routeSpec = routeSpecIter.getNext()) {
    oaString routeSpecName;
    routeSpec->getName(routeSpecName);
    cout << "Name: "<< routeSpecName
	 << " width " << routeSpec->isWidthFixed()
	 << " spacing " << routeSpec->isSpacingFixed() << endl;

    oaRouteLayerSpecArray routeLayerSpecArray;
    routeSpec->getLayerSpecs(routeLayerSpecArray);
    oaString routeLayerName;

    cout <<"Route layer specs" << endl;
    for (unsigned int i = 0; i < routeLayerSpecArray.getNumValues(); i++) {
      oaRouteLayerSpec routeLayerSpec = routeLayerSpecArray[i];
      oaPhysicalLayer *routeLayer = routeLayerSpec.layer();
      routeLayer->getName(routeLayerName);
      cout << "Route layer name: " << routeLayerName << endl;
      cout << "  width - "
	   << routeLayerSpec.width() << "\n"
	   << "  spacing - "
	   << routeLayerSpec.spacing() << "\n"
	   << "  diag width - "
	   << routeLayerSpec.diagWidth() << "\n"
	   << "  diag spacing - "
	   << routeLayerSpec.diagSpacing() << "\n"
	   << "  wire ext - "
	   << routeLayerSpec.wireExt() << endl;
    }
  }

  // get layer info from DB
  cout << "Get Layer info " << endl;
  oaIter<oaLayer> layerIter (chipTech->getLayers());
  cout << "Number of layers " << (chipTech->getLayers()).getCount() << endl;

  while (oaLayer *layer = layerIter.getNext()) {
    oaString layerName;
    oaString purposeName;
    layer->getName(layerName);
    cout << "Layer name: " << layerName
	 << ", layer number: " << layer->getNumber() << endl;
    if (oaPhysicalLayer *phyLayer =
	oaPhysicalLayer::find(chipTech, layer->getNumber())) {
      cout << "Physical layer name:\n"
	   << "  routing grid pitch - "
	   << phyLayer->getRouteGridPitch() << "\n"
	   << "  routing grid offset - "
	   << phyLayer->getRouteGridOffset() << "\n"
	   << "  preferred routing dir - "
	   << (phyLayer->getPrefRoutingDir()).getName() << "\n"
	   << "  manufacturing grid - "
	   << phyLayer->getManufacturingGrid() << "\n"
	   << endl;
    }
  }

  // get cell height - use site def info
  cout << "Get site def info" << endl;
  oaIter<oaSiteDef> siteDefIter = chipTech->getSiteDefs();
  cout << "Number of sites " << (chipTech->getSiteDefs()).getCount() << endl;
  while (oaSiteDef *siteDef = siteDefIter.getNext()) {
    oaSiteDefType siteDefType = siteDef->getSiteDefType();
    cout << "  Type - " << siteDefType.getName() << "\n"
	 << "  Cell height - " << siteDef->getHeight() << endl;
  }
  chipTech->close();
  return 0;
}

int
getUserUnit(const char *techName, char *userUnit, size_t userUnitSz, ClientData *cdarg,
		int (*magicFunc)(const char *techName, char *userUnit,
		ClientData *cdarg), oaCellViewType viewType)
{

  // open OA tech DB
  oaNativeNS ns;
  //oaString userUnit;
  oaScalarName chipTechName(ns, (oaChar *) techName);
  oaTech *chipTech = oaTech::open(chipTechName);
  switch (chipTech->getUserUnits(viewType)) {
  case oacMicron:
    //userUnit = "micron";
    strcpy_s(userUnit, userUnitSz, "micron");
    break;
  case oacMillimeter:
    //userUnit = "millimeter";
    strcpy_s(userUnit, userUnitSz, "millimeter");
    break;
  case oacCentimeter:
    //userUnit = "centimeter";
    strcpy_s(userUnit, userUnitSz, "centimeter");
    break;
  case oacMeter:
    //userUnit = "meter";
    strcpy_s(userUnit, userUnitSz, "meter");
    break;
  case oacMil:
    //userUnit = "mil";
    strcpy_s(userUnit, userUnitSz, "mil");
    break;
  case oacInch:
    //userUnit = "inch";
    strcpy_s(userUnit, userUnitSz, "inch");
    break;
  case oacNanometer:
    //userUnit = "nanometer";
    strcpy_s(userUnit, userUnitSz, "nanometer");
    break;
  default:
    //userUnit = "none";
    strcpy_s(userUnit, userUnitSz, "none");
  }
  //cout << "getTechUserUnit " << userUnit << endl;
  if (magicFunc)
    magicFunc(techName, userUnit, cdarg);
  return 0;
}

int
getDBUnitsPerUserUnit(const char *techName, int &dbUPerUU,
			  ClientData *cdarg, int (*magicFunc)
			  (const char *techName, int &dbUPerUU,
			  ClientData *cdarg), oaCellViewType viewType)
{

  // open OA tech DB
  oaNativeNS ns;
  oaScalarName chipTechName(ns, (oaChar *) techName);
  oaTech *chipTech = oaTech::open(chipTechName);
  dbUPerUU = chipTech->getDBUPerUU(viewType);
  //cout << "getTechDBUnitsPerUserUnit " << dbUPerUU <<endl;
  if (magicFunc)
    magicFunc(techName, dbUPerUU, cdarg);
  return TCL_OK;
}

int
openDesign(const char *lib, const char *cell, const char *view) {

  // init namespace
  oaNativeNS ns;
  oaCellView *cellView = NULL;

  oaScalarName libName(ns, lib);
  oaScalarName cellName(ns, cell);
  oaScalarName viewName(ns, view);

  cout << "Open cell " << cell << " " << view << endl;
  try {
    cellView = oaCellView::open(libName, cellName, viewName,
					    oacMaskLayout, 'r');
  } catch (oaDBException& dbErr) {
    cout << dbErr.getFormatString() << endl;
    return 1;
  }
  curCellView = cellView;

  return 0;
}

oaCellView *
getPrevCV() {
  // init namespace
  oaNativeNS ns;

  oaCellView *cv = NULL;
  oaIter<oaCellView> cellViewIter = oaCellView::getOpenCellViews();
  while (oaCellView *cellView = cellViewIter.getNext()) {
    cv = cellView;
  }
  if (cv) {
    oaString cellName;
    cv->getCellName(ns, cellName);
    cout << "Current open cell set to " << cellName << endl;
  } else {
    cout << "No open cell view" << endl;
  }
  return cv;
}

int
closeDesign(const char *lib, const char *cell, const char *view) {

  // init namespace
  oaNativeNS ns;

  oaScalarName libName(ns, lib);
  oaScalarName cellName(ns, cell);
  oaScalarName viewName(ns, view);

  oaCellView *cellView = oaCellView::find(libName, cellName, viewName);

  // valid open cell view found, so close
  if (cellView) {
    cout << "Close cell " << cell << " " << view << endl;
    cellView->close();
    curCellView = getPrevCV();
  }
  return 0;
}

int
closeDesign() {

  oaNativeNS ns;
  oaString cellName;

  // check for valid cell
  if (curCellView) {
    curCellView->getCellName(ns, cellName);
    cout << "Close cell " << cellName << endl;

    curCellView->close();

    curCellView = getPrevCV();
  } else {
    cout << "No cell view currently open" << endl;
  }

  return 0;
}

int
closeAll() {
  oaIter<oaCellView> cellViewIter = oaCellView::getOpenCellViews();
  while (oaCellView *cellView = cellViewIter.getNext()) {
    cellView->close();
  }

  curCellView = NULL;
  return 0;
}

int
getBoundingBox (oaInst *instPtr, const char *instanceName, const char *defName,
		int (*magicFunc)(const char *instName, const char *defName,
				 int llx, int lly, int urx, int ury,
				 const char *curName, ClientData *cdarg),
		ClientData *cdarg, int callBack) {

  oaNativeNS ns;
  oaCellView *cellView;
  oaString instMagicName(instanceName);
  oaBlock *topBlock;
  oaInst *inst = (oaInst *)instPtr;
  int loccallback = callBack;

  // on first call, use current open cell view
  if (inst == NULL) {
    oaBox bBox;

    //oaIter<oaCellView> cellViewIter = oaCellView::getOpenCellViews();
    if (curCellView) {
      cout << "Using open cell view" << endl;
      cellView = curCellView;
    } else {
      cout << "No cellview open" << endl;
      return 1;
    }

    topBlock = cellView->getTopBlock();
    topBlock->getBBox(bBox);
    cout << "Layout size " << bBox.left() << "," << bBox.bottom()
       << " - " << bBox.right() << "," << bBox.top() << endl;

  } else {

    oaBox bBox;
    oaString instName;
    oaPoint instOrigin;

    inst->getName(ns, instName);
    cout << "instName " << instName << endl;

    if (instName == instMagicName)
       loccallback = TRUE;

    if (loccallback) {
      inst->getOrigin(instOrigin);
      try {
        cellView = inst->getMaster();
      } catch (oaDBException& dbErr) {
        cout << "error in getMaster" << endl;
        return 1;
      }
      try {
        topBlock = cellView->getTopBlock();
      } catch (oaDBException& dbErr) {
	cout << "error in getTopBlock" << endl;
	return 1;
      }
      topBlock->getBBox(bBox);
      //call magic function
//      magicFunc (instanceName, defName,
//                bBox.left() + instOrigin.x(), bBox.bottom() + instOrigin.y(),
//                bBox.right() + instOrigin.x(), bBox.top() + instOrigin.y(),
//                instName, cdarg);
        cout << "instName " << instName << ", bbox: (" << bBox.left() <<
		", " << bBox.bottom() << "), (" << bBox.right() << ", " <<
		bBox.top() << ")" << endl;
    }
  }

  oaIter<oaInst> instIter = topBlock->getInsts();
  while (oaInst *inst = instIter.getNext()) {
    getBoundingBox(inst, instanceName, defName, magicFunc, cdarg, loccallback);
  }

  return 0;
}

