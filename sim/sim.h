#ifndef _MAGIC__SIM__SIM_H
#define _MAGIC__SIM__SIM_H

#include "utils/magic.h"
#include "textio/txcommands.h"	/* TxCommand */
#include "windows/windows.h"	/* MagWindow */

extern	const char *SimGetNodeCommand(const char *cmd);
extern	char	*SimGetNodeName(SearchContext *sx, Tile *tp, const char *path);
extern	char	*SimSelectNode(SearchContext *scx, TileType type, int xMask, char *buffer);
extern	bool	SimGetReplyLine(char **replyLine);
extern	void	SimRsimIt(const char *cmd, const char *nodeName);
extern	void	SimEraseLabels(void);
extern	bool	efPreferredName(const char *name1, const char *name2);
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
extern int SimPutLabel(CellDef *cellDef, const Rect *rect, int align, const char *text, TileType type);
extern int SimSrConnect(CellDef *def, const Rect *startArea, const TileTypeBitMask *mask, const TileTypeBitMask *connect,
                        const Rect *bounds, int (*func)(), ClientData clientData);
extern void SimTreeCopyConnect(SearchContext *scx, const TileTypeBitMask *mask, int xMask, const TileTypeBitMask *connect,
                               const Rect *area, CellUse *destUse, char *Node_Name);
extern int SimTreeSrNMTiles(SearchContext *scx, TileType dinfo, const TileTypeBitMask *mask, int xMask, TerminalPath *tpath,
                            int (*func)(), ClientData cdarg);
extern int SimTreeSrTiles(SearchContext *scx, const TileTypeBitMask *mask, int xMask, TerminalPath *tpath,
                          int (*func)(), ClientData cdarg);
extern bool SimStartRsim(char *argv[]);
extern void SimConnectRsim(bool escRsim);
extern bool SimSelection(const char *cmd);
extern void SimRsimMouse(MagWindow *w);
extern int SimFillBuffer(char *buffHead, char **pLastChar, int *charCount);

#endif /* _MAGIC__SIM__SIM_H */
