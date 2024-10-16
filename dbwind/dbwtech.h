/*
 * dbwtech.h --
 *
 * Style information for display.
 * MAXTILESTYLES is the maximum number of styles usable for display
 * of tiles.
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/dbwind/dbwtech.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _MAGIC__DBWIND__DBWTECH_H
#define	_MAGIC__DBWIND__DBWTECH_H

extern TileTypeBitMask	*DBWStyleToTypesTbl;

#define	DBWStyleToTypes(s)	(DBWStyleToTypesTbl + s)

/* forward declarations */
extern int  DBWTechParseStyle();

/* C99 compat */
extern void DBWElementStyle();
extern void DBWElementText();
extern void DBWSetCrosshair();

#endif /* _MAGIC__DBWIND__DBWTECH_H */
