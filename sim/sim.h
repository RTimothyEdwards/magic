#ifndef _SIM_H
#define _SIM_H

#include "utils/magic.h"
#include "textio/txcommands.h"	/* TxCommand */
#include "windows/windows.h"	/* MagWindow */

extern	char	*SimGetNodeCommand(char *cmd);
extern	char	*SimGetNodeName(SearchContext *sx, Tile *tp, char *path);
extern	char	*SimSelectNode(SearchContext *scx, TileType type, int xMask, char *buffer);
extern	bool	SimGetReplyLine(char **replyLine);
extern	void	SimRsimIt(char *cmd, char *nodeName);
extern	void	SimEraseLabels(void);
extern	bool	efPreferredName(char *name1, char *name2);
extern  void	SimRsimHandler(MagWindow *w, TxCommand *cmd);
extern  void	SimInit(void);

extern  bool	SimRecomputeSel;
extern 	bool	SimInitGetnode;
extern	bool	SimGetnodeAlias;
extern 	bool	SimSawAbortString;
extern 	bool	SimRsimRunning;
extern	bool	SimIsGetnode;
extern	bool	SimHasCoords;
extern	bool	SimUseCoords;
extern  bool	SimIgnoreGlobals;

extern HashTable SimNodeNameTbl;
extern HashTable SimGNAliasTbl;
extern HashTable SimGetnodeTbl;
extern HashTable SimAbortSeenTbl;

/* C99 compat */
extern void SimGetnode(void);
extern void SimGetsnode(void);
extern void SimGetNodeCleanUp(void);
extern int SimPutLabel(CellDef *cellDef, Rect *rect, int align, char *text, TileType type);
extern int SimSrConnect(CellDef *def, Rect *startArea, TileTypeBitMask *mask, TileTypeBitMask *connect,
                        Rect *bounds, int (*func)(), ClientData clientData);
extern void SimTreeCopyConnect(SearchContext *scx, TileTypeBitMask *mask, int xMask, TileTypeBitMask *connect,
                               Rect *area, CellUse *destUse, char *Node_Name);
extern int SimTreeSrNMTiles(SearchContext *scx, TileType dinfo, TileTypeBitMask *mask, int xMask, TerminalPath *tpath,
                            int (*func)(), ClientData cdarg);
extern int SimTreeSrTiles(SearchContext *scx, TileTypeBitMask *mask, int xMask, TerminalPath *tpath,
                          int (*func)(), ClientData cdarg);
extern bool SimStartRsim(char *argv[]);
extern void SimConnectRsim(bool escRsim);
extern bool SimSelection(char *cmd);
extern void SimRsimMouse(MagWindow *w);
extern int SimFillBuffer(char *buffHead, char **pLastChar, int *charCount);

#endif /* _SIM_H */
