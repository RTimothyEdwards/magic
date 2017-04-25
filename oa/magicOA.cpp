#include <iostream>
#include <magicOA.h>

using namespace std;

// current open cell view
oaCellView *curCellView;


int
getTechInfo (const char *techName)
{

  char *argArray[] = {"tclsh"};
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
    for (int i = 0; i < routeLayerSpecArray.getNumValues(); i++) {
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
getUserUnit(const char *techName, char *userUnit, ClientData *cdarg, 
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
    strcpy(userUnit, "micron");
    break;
  case oacMillimeter:
    //userUnit = "millimeter";
    strcpy(userUnit, "millimeter");
    break;
  case oacCentimeter:
    //userUnit = "centimeter";
    strcpy(userUnit, "centimeter");
    break;
  case oacMeter:
    //userUnit = "meter";
    strcpy(userUnit, "meter");
    break;
  case oacMil:
    //userUnit = "mil";
    strcpy(userUnit, "mil");
    break;
  case oacInch:
    //userUnit = "inch";
    strcpy(userUnit, "inch");
    break;
  case oacNanometer:
    //userUnit = "nanometer";
    break;
    strcpy(userUnit, "nanometer");
  default:
    //userUnit = "none";
    strcpy(userUnit, "none");
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
  } catch (oaDBException dbErr) {
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
      } catch (oaDBException dbErr) {
        cout << "error in getMaster" << endl;
        return 1;
      }
      try {
        topBlock = cellView->getTopBlock();
      } catch (oaDBException dbErr) {
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

