/*
 *-------------------------------------------------------------------------
 * flock.c --
 *
 * File opening with file locks (used by utils/path.c and database/DBio.c)
 *
 * Original implementation by Michael Godfrey, Stanford University.
 * Implementation using "lockf" by Stefan Jones, MultiGiG, Inc.,
 * September 2005 (magic 7.3.102_lockf, finally corrected and implemented
 * as magic 7.3.119)
 *-------------------------------------------------------------------------
 */

#ifdef FILE_LOCKS

#include <unistd.h>
#include <fcntl.h>

#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "utils/malloc.h"

/*
 *-------------------------------------------------------------------------
 * Below are the service routines for file locking.
 *
 * The original file locking mechanism (Michael Godfrey) used common files.
 * However, this has the drawback of requiring extraneous directories, and
 * orphaned lock files will remain after a program crash.
 *
 * The lockf solution (Stefan Jones) has the drawback that in order to
 * hold a lock, the file descriptor must be left open.  This leaves the
 * possibility that we may exceed the maximum number of file descriptors
 * per process.  Because this solution is the simplest, it is implemented
 * here.
 *
 * For future reference, the best file locking system (?) would use
 * signaling via fcntl(F_NOTIFY).  The locking mechanism goes like this:
 *
 * 1. process X requests an attributes notification on each directory that
 *    it reads a file from.
 *
 * 2. process X reads file A for editing.
 *
 * 3. process Y requests an attributes notification on each directory that
 *    it reads a file from.
 *
 * 4. process Y reads file A, thus changing its access time attribute.
 *    process Y sets the cell as read/write.
 *
 * 5. process X receives a signal.  It checks the cell that is being
 *    accessed.  If the cell has been modified, it sets a lock on
 *    file A.  Opening file A to set the lock modifies its access time.
 *    If the cell has not been modified, it sets the cell to read-only.
 *
 * 6. process Y receives a signal.  It checks the cell that is being
 *    accessed.  If the cell has not yet been modified, it sets the
 *    cell to read-only.  If the cell has been modified (less likely,
 *    but possible if process X was doing something time-consuming and
 *    uninterruptable and didn't process the signal for a while), then
 *    process Y attempts to set a lock.  If the lock fails, then a
 *    warning is issued.
 *
 * It is possible for either X or Y to win the race if both processes
 * modified the file right after opening.  However, this rare condition
 * is unlikely to be a serious problem, and prevents a process from
 * having to hold open many file descriptors.
 * 
 *-------------------------------------------------------------------------
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

/*
 *-------------------------------------------------------------------------
 * flock_open --
 *
 *	Open a file with "lockf" file locking method.
 *
 * Results --
 *	Pointer to FILE that was opened, or NULL if an error occurred.
 *
 * Side effects --
 *	System I/O
 *-------------------------------------------------------------------------
 */

FILE *flock_open(filename, mode, is_locked)
    char *filename;
    char *mode;
    bool *is_locked;
{
    FILE *f = NULL, *tmp;
    struct flock fl;

    if (is_locked) *is_locked = FALSE;

    /* If is_locked is NULL, then a lock is not requested, so just do	*/
    /* a normal fopen() and return.					*/

    if (is_locked == NULL) return fopen(filename, mode);
	
    /* Diagnostic */
    /* TxPrintf("Opening file <%s>\n", filename); */

    tmp = fopen(filename, "r+");
    if (tmp == NULL)
    {
	if (is_locked) *is_locked = TRUE;
	f = fopen(filename, "r");
	goto done;	
    }

    fl.l_len = 0;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_type = F_WRLCK;
    fl.l_pid = getpid();

    if (fcntl(fileno(tmp), F_GETLK, &fl))
    {
	perror(filename);
	f = fopen(filename, mode);
	goto done;
    }
    fclose(tmp);
	
    if (fl.l_type == F_UNLCK)
    {
	fl.l_len = 0;
	fl.l_start = 0;
	fl.l_whence = SEEK_SET;
	fl.l_type = F_WRLCK;
	fl.l_pid = getpid();

	f = fopen(filename, "r+");
	if (fcntl(fileno(f), F_SETLK, &fl))
	{
 	    perror(filename);
	}
	else
	{
	    /* Diagnostic */
	    /* TxPrintf("Obtained lock on file <%s> (fd=%d)\n", filename, fileno(f)); */
	}
    }
    else
    {
	/* Don't know why PID is not set by F_GETLK as advertised? */
	if (fl.l_pid == 0)
	    TxPrintf("File <%s> is already locked by another process."
			"  Opening read-only.\n", filename);
	else
	    TxPrintf("File <%s> is already locked by pid %d.  Opening read-only.\n",
				filename, (int)fl.l_pid);
	if (is_locked) *is_locked = TRUE;
	f = fopen(filename, "r");
    }

done:
    return f;
}

#endif  /* FILE_LOCKS */
