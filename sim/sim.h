#ifndef _SIM_H
#define _SIM_H

#include "utils/magic.h"

extern	char	*SimGetNodeCommand();
extern	char	*SimGetNodeName();
extern	char	*SimSelectNode();
extern	bool	SimGetReplyLine();
extern	void	SimRsimIt();
extern	void	SimEraseLabels();
extern	bool	efPreferredName();
extern  void	SimRsimHandler();
extern  void	SimInit();

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

#endif /* _SIM_H */
