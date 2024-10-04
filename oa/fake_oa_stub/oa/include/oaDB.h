/*
 *
 *
 *
 */

#ifndef _OA_STUB__OADB_HH
#define _OA_STUB__OADB_HH

#include <exception>
#include <string>

#ifndef TRUE
 #define TRUE 1
#endif

typedef char oaChar;

typedef std::string oaString;

extern int oaDBInit(int* argc, char **argv);

//oaCellViewType viewType=oacMaskLayout
enum oaCellViewType {
  oacMaskLayout
};

class oaException : public std::exception {
public:
   char const *getMsg(void) { 
       return "exception";
   }
};

class oaDBException : public std::exception {
public:
#if 0
   char const *getMsg(void) { 
       return "exception";
   }
#endif
   char const* getFormatString() {
       return "exception";
   }
};

class oaNativeNS {

};

class oaScalarName {
public:
    oaScalarName(oaNativeNS const& ns, oaChar const* name) {
    }
    oaScalarName(oaNativeNS const& ns, oaString const& name) {
    }
};


class oaPoint {
public:
};

class oaBox {
public:
    oaChar const* left(void) {
       return nullptr;
    }
    oaChar const* right(void) {
       return nullptr;
    }
    oaChar const* top(void) {
       return nullptr;
    }
    oaChar const* bottom(void) {
       return nullptr;
    }
};

class oaSiteDefType {
public:
    oaString const getName(void) {
        return oaString("oaSiteDefType");
    }
};

class oaSiteDef {
public:
    int getHeight(void) {
        return 0;
    }
    oaSiteDefType getSiteDefType(void) {
       return oaSiteDefType();
    }
};

template <typename T>
class oaIter {
public:
    T* getNext(void) {
        return nullptr;
    }
    unsigned int getCount() {
        return 0;
    }
};

class oaPrefRoutingDir {
public:
    oaString const getName(void) {
        return oaString("");
    }
};

class oaTech;

class oaPhysicalLayer {
public:
    void getName(oaString const& s) {
    }
    unsigned int getRouteGridPitch() {
        return 0;
    }
    unsigned int getRouteGridOffset() {
        return 0;
    }
    oaPrefRoutingDir getPrefRoutingDir() {
        return oaPrefRoutingDir(); // .getName()
    }
    unsigned int getManufacturingGrid() {
        return 0;
    }

    static oaPhysicalLayer* find(oaTech* tech, unsigned int layerNumber) {
        return new oaPhysicalLayer();
    }
};

class oaRouteLayerSpec {
public:
    oaPhysicalLayer* layer(void) {
        return nullptr;
    }
    unsigned int width() {
        return 0;
    }
    unsigned int spacing() {
        return 0;
    }
    unsigned int diagWidth() {
        return 0;
    }
    unsigned int diagSpacing() {
        return 0;
    }
    unsigned int wireExt() {
        return 0;
    }
};

class oaRouteLayerSpecArray {
private:
public:
    unsigned int getNumValues() {
        return 0;
    }
    oaRouteLayerSpec operator [](int index) {
        return oaRouteLayerSpec();
    }
};

class oaRouteSpec {
public:
    bool isWidthFixed() {
        return false;
    }
    bool isSpacingFixed() {
        return false;
    }
    void getName(oaString const& s) {
    }
    void getLayerSpecs(oaRouteLayerSpecArray const& array) {
    
    }
};

class oaLayer {
public:
    void getName(oaString const& s) {
    }
    unsigned int getNumber(void) {
        return 0;
    }
};


enum oacUnits {
    oacMicron,
    oacMillimeter,
    oacCentimeter,
    oacMeter,
    oacMil,
    oacInch,
    oacNanometer
};

class oaTech {
public:
    oacUnits getUserUnits(oaCellViewType const& cellViewType) {
        return oacMicron;
    }
    void close(void) {
    }
    int getDBUPerUU(oaCellViewType const& viewType) {
        return 0;
    }
    oaIter<oaSiteDef> getSiteDefs(void) {
        return oaIter<oaSiteDef>();
    }
    oaIter<oaRouteSpec> getRouteSpecs(void) {
        return oaIter<oaRouteSpec>();
    }
    oaIter<oaLayer> getLayers(void) {
        return oaIter<oaLayer>();
    }
    static oaTech* open(oaScalarName const& chipTechName) {
        return new oaTech();
    }
};

class oaInst;

class oaBlock {
public:
    oaIter<oaInst> getInsts(void) {
        return oaIter<oaInst>();
    }
    void getBBox(oaBox& bb) {
    }
};

class oaCellView {
public:
    oaBlock* getTopBlock(void) {
        return nullptr;
    }
    void close(void) { }
    
    void getCellName(oaNativeNS& ns, oaString& s) {
    }
    
    static oaCellView* find(oaChar const* lib, oaChar const* cell, oaChar const* view) {
        return nullptr;
    }
    static oaCellView* find(oaString& lib, oaString& cell, oaString& view) {
        return nullptr;
    }
    static oaCellView* find(oaScalarName& lib, oaScalarName& cell, oaScalarName& view) {
        return nullptr;
    }
    
    static oaCellView* open(oaScalarName const& libName, oaScalarName const& cellName, oaScalarName const& viewName,
                            oaCellViewType cellViewType, char mode) {
        return new oaCellView();
    }

    static oaIter<oaCellView> getOpenCellViews() {
        return oaIter<oaCellView>();
    }
};

class oaInst {
public:
    void getOrigin(oaPoint const& origin) {
    }
    void getName(oaNativeNS const& ns, oaString const& s) {
    }
    oaCellView* getMaster(void) {
        return new oaCellView();
    }
};


#endif /* _OA_STUB__OADB_HH */
