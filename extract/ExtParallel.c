/*
 * ExtParallel.c --
 *
 * Parallel hierarchical extraction ("extract jobs N"), used by "extract all",
 * incremental "extract", and "extract do extresist".
 *
 * Each cell in the hierarchy is extracted to its own .ext file, and
 * extracting a cell only reads its children's layout, not their .ext
 * output.  That makes cells natural units of parallel work.  Magic's
 * database is not thread-safe, so instead of threads this code forks
 * worker processes after the whole tree has been read in.  Each worker
 * has a private copy-on-write image of the database, so no locking of
 * the database is needed at all.
 *
 * The one cross-cell dependency is that extracting a parent reads some
 * state that extracting its children left on the child CellDefs (labels
 * made unique, labels marked by extBasic() as unusable for merges, and
 * the prepared substrate plane).  Before a worker extracts a cell, it
 * therefore replays that state (ExtReplayCell(), which skips the costly
 * hierarchical part) for every descendant it has not already processed.
 *
 * Workers take cells from a shared counter, largest estimated cost
 * first, so one large top cell does not end up queued behind many small
 * ones.  Each worker's messages for each cell go to a per-cell log file,
 * and the parent prints the logs in the same order as a serial run, so
 * the output is deterministic.  The .ext files written are identical to
 * those of a serial run.
 *
 * Feedback entries that a worker creates while extracting a cell are
 * written to a per-cell file and re-added by the parent in serial order,
 * so "feedback" commands see the same list as after a serial run.
 *
 * With "extract do extresist", resistance extraction of a cell reads the
 * .ext files of its parents, so it can only start once every cell has
 * been extracted.  Workers therefore wait at a barrier after the first
 * round and then take resistance jobs in a second round, reusing the
 * database state they have already built up.
 *
 * Both "extract all" and incremental "extract" use this code.  It needs
 * fork(), so builds without it (WASM, native Windows, or any build with
 * -DMAGIC_NO_FORK) always extract serially.
 */

#if defined(__EMSCRIPTEN__) || defined(_WIN32) || defined(MAGIC_NO_FORK)
#define EXT_NO_FORK
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef EXT_NO_FORK
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#endif

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "utils/stack.h"
#include "utils/utils.h"
#include "utils/undo.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "resis/resis.h"

/* Number of worker processes used by "extract all" (1 = serial) */
int ExtNumJobs = 1;

/* TRUE only inside a forked worker process */
bool extParallelWorker = FALSE;

/* One unit of work:  a CellDef to extract */
typedef struct
{
    CellDef	*ej_def;
    dlong	 ej_cost;	/* Estimated extraction cost */
    int		 ej_state;	/* Worker-local:  see EJ_* below */
    Plane	*ej_savePlane;	/* Worker-local:  substrate plane to revert */
} ExtJob;

#define EJ_NONE		0	/* Not yet touched in this worker */
#define EJ_REPLAYED	1	/* Side effects replayed, no output written */
#define EJ_EXTRACTED	2	/* Fully extracted by this worker */

/* State shared between the parent and all workers (anonymous mmap) */
typedef struct
{
    int		es_next;	/* Next claim (extraction round) */
    int		es_nextRes;	/* Next claim (resistance round) */
    int		es_ndone;	/* Number of cells extracted so far */
    int		es_abort;	/* Set by the parent if a worker died */
    int		es_data[1];	/* Really 4 * nJobs:  see EXT_* macros */
} ExtShared;

/* Per-job arrays inside es_data[] */
#define EXT_DONE(sh, n)		((sh)->es_data)			/* extracted */
#define EXT_ERRORS(sh, n)	((sh)->es_data + (n))
#define EXT_WARNINGS(sh, n)	((sh)->es_data + 2 * (n))
#define EXT_RESDONE(sh, n)	((sh)->es_data + 3 * (n))	/* extresist */
#define EXT_SHSIZE(n)		(sizeof(ExtShared) + 4 * (n) * sizeof(int))

static ExtJob *extJobs;
static int extNumJobList;
static FILE *extNullFile;

/* Maps CellDef pointer -> job index + 1.  A hash table is used rather
 * than cd_client so that each def looks exactly as it does in a serial
 * run (which sets cd_client to zero before extracting the cell).
 */
static HashTable extJobTable;

int
extJobIndex(
    CellDef *def)
{
    HashEntry *he = HashLookOnly(&extJobTable, (char *) def);

    if (he == NULL) return -1;
    return (int) CD2INT(HashGetValue(he)) - 1;
}
#define JOBINDEX(def)	extJobIndex(def)

/*
 * ----------------------------------------------------------------------------
 *
 * extCountTileFunc --
 *
 *	Tile search callback that counts tiles.
 *
 * ----------------------------------------------------------------------------
 */

int
extCountTileFunc(
    Tile *tile,
    TileType dinfo,
    ClientData cdata)
{
    (*(dlong *)cdata)++;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCellTiles --
 *
 *	Return the number of non-space paint tiles in a cell.
 *
 * ----------------------------------------------------------------------------
 */

dlong
extCellTiles(
    CellDef *def)
{
    dlong count = 0;
    int pNum;

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &TiPlaneRect,
		&DBAllButSpaceBits, extCountTileFunc, (ClientData) &count);
    return count;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extChildCostFunc --
 *
 *	Cell enumeration callback that adds a share of each child's own
 *	tile count to the parent's estimated cost, as a rough measure of
 *	the work done finding hierarchical interactions.
 *
 * ----------------------------------------------------------------------------
 */

int
extChildCostFunc(
    CellUse *use,
    ClientData cdata)
{
    dlong *cost = (dlong *) cdata;
    int idx = JOBINDEX(use->cu_def);
    int nuses;

    if (idx < 0) return 0;
    nuses = (use->cu_xhi - use->cu_xlo + 1) * (use->cu_yhi - use->cu_ylo + 1);
    *cost += (extJobs[idx].ej_cost / 4) * nuses;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extReplayChildFunc --
 *
 *	Cell enumeration callback that makes sure a child (and,
 *	recursively, all of its descendants) have had the side effects
 *	of extraction applied in this worker.
 *
 * ----------------------------------------------------------------------------
 */

int extReplayChildFunc(CellUse *use, ClientData cdata);

void
extReplayDescendants(
    CellDef *def,
    CellDef *rootDef)
{
    (void) DBCellEnum(def, extReplayChildFunc, (ClientData) rootDef);
}

int
extReplayChildFunc(
    CellUse *use,
    ClientData cdata)
{
    CellDef *rootDef = (CellDef *) cdata;
    CellDef *def = use->cu_def;
    int idx = JOBINDEX(def);

    if (idx < 0) return 0;
    if (extJobs[idx].ej_state != EJ_NONE) return 0;

    /* Children first, matching the bottom-up order of a serial run */
    extReplayDescendants(def, rootDef);

    extJobs[idx].ej_savePlane = ExtReplayCell(def, (def == rootDef));
    extJobs[idx].ej_state = EJ_REPLAYED;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extRevertReplay --
 *
 *	Undo a replay on a cell, so that it can be extracted from a clean
 *	state, as it would be in a serial run.
 *
 * ----------------------------------------------------------------------------
 */

void
extRevertReplay(
    ExtJob *job)
{
    if (ExtOptions & EXT_DOUNIQUE) ExtRevertUniqueCell(job->ej_def);
    if (job->ej_savePlane != NULL)
	ExtRevertSubstrate(job->ej_def, job->ej_savePlane);
    job->ej_savePlane = NULL;
    job->ej_state = EJ_NONE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCostCompare --
 *
 *	qsort() comparator:  highest cost first, then bottom-up order.
 *
 * ----------------------------------------------------------------------------
 */

int
extCostCompare(
    const void *a,
    const void *b)
{
    int ia = *(const int *) a, ib = *(const int *) b;

    if (extJobs[ia].ej_cost > extJobs[ib].ej_cost) return -1;
    if (extJobs[ia].ej_cost < extJobs[ib].ej_cost) return 1;
    return ia - ib;
}

#ifndef EXT_NO_FORK

/*
 * extCloseFile --
 *	Close a file that was written, and return FALSE if any write to it
 *	failed (for example because the disk is full).
 */
bool
extCloseFile(
    FILE *f)
{
    bool ok = !ferror(f);

    if (fclose(f) != 0) ok = FALSE;
    return ok;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSaveFeedback --
 *
 *	Write feedback entries 'first' .. DBWFeedbackCount-1 to 'fileName'
 *	so that the parent can re-create them with extLoadFeedback().
 *	Returns FALSE if the file could not be written completely.
 *
 *	Record format (native byte order, same machine):
 *	    int xbot, ybot, xtop, ytop, scale, style, nameLen, textLen;
 *	    char rootName[nameLen]; char text[textLen];
 *
 * ----------------------------------------------------------------------------
 */

bool
extSaveFeedback(
    int first,
    char *fileName)
{
    FILE *f;
    int nth, hdr[8];
    Rect area;
    CellDef *rootDef;
    char *text;

    if (first >= DBWFeedbackCount) return TRUE;
    f = fopen(fileName, "w");
    if (f == NULL) return FALSE;
    for (nth = first; nth < DBWFeedbackCount; nth++)
    {
	text = DBWFeedbackGetRaw(nth, &area, &rootDef, &hdr[4], &hdr[5]);
	if (text == NULL || rootDef == NULL) continue;
	hdr[0] = area.r_xbot;
	hdr[1] = area.r_ybot;
	hdr[2] = area.r_xtop;
	hdr[3] = area.r_ytop;
	hdr[6] = strlen(rootDef->cd_name);
	hdr[7] = strlen(text);
	fwrite(hdr, sizeof(int), 8, f);
	fwrite(rootDef->cd_name, 1, hdr[6], f);
	fwrite(text, 1, hdr[7], f);
    }
    return extCloseFile(f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extLoadFeedback --
 *
 *	Parent side of extSaveFeedback():  re-add the entries in a
 *	feedback file, in order, then delete the file.
 *
 * ----------------------------------------------------------------------------
 */

void
extLoadFeedback(
    char *fileName)
{
    FILE *f;
    int hdr[8];
    Rect area;
    CellDef *rootDef;
    char *name, *text;
    size_t got;
    bool bad = FALSE;

    f = fopen(fileName, "r");
    if (f == NULL) return;
    while ((got = fread(hdr, sizeof(int), 8, f)) == 8)
    {
	if (hdr[6] < 0 || hdr[7] < 0) { bad = TRUE; break; }
	name = (char *) mallocMagic(hdr[6] + 1);
	text = (char *) mallocMagic(hdr[7] + 1);
	if (fread(name, 1, hdr[6], f) != hdr[6] ||
		fread(text, 1, hdr[7], f) != hdr[7])
	{
	    freeMagic(name);
	    freeMagic(text);
	    bad = TRUE;
	    break;
	}
	name[hdr[6]] = '\0';
	text[hdr[7]] = '\0';
	rootDef = DBCellLookDef(name);
	if (rootDef != NULL)
	{
	    area.r_xbot = hdr[0];
	    area.r_ybot = hdr[1];
	    area.r_xtop = hdr[2];
	    area.r_ytop = hdr[3];
	    DBWFeedbackAddRoot(&area, text, rootDef, hdr[4], hdr[5]);
	}
	freeMagic(name);
	freeMagic(text);
    }
    if (bad || (got != 0) || ferror(f))
	TxError("Parallel extraction:  feedback file %s is damaged;  "
		"some feedback entries are missing.\n", fileName);
    fclose(f);
    unlink(fileName);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPrintLog --
 *
 *	Copy a worker's per-cell log to the parent's output, then delete it.
 *
 * ----------------------------------------------------------------------------
 */

void
extPrintLog(
    char *fileName)
{
    FILE *f;
    char buf[4096];
    size_t len;

    f = fopen(fileName, "r");
    if (f == NULL) return;
    while ((len = fread(buf, 1, sizeof(buf) - 1, f)) > 0)
    {
	buf[len] = '\0';
	TxPrintf("%s", buf);
    }
    fclose(f);
    unlink(fileName);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extDiscardOutput --
 *
 *	Delete every per-cell log and feedback file, and the directory
 *	holding them, without using their contents.
 *
 * ----------------------------------------------------------------------------
 */

void
extDiscardOutput(
    char *logDir,
    int n)
{
    static const char * const formats[] =
	{ "%s/%d.log", "%s/%d.fb", "%s/r%d.log", "%s/r%d.fb" };
    char fileName[1100];
    int i, k;

    for (i = 0; i < n; i++)
	for (k = 0; k < 4; k++)
	{
	    snprintf(fileName, sizeof(fileName), formats[k], logDir, i);
	    unlink(fileName);
	}
    rmdir(logDir);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extWorker --
 *
 *	Body of a forked worker process.  Never returns.
 *
 * ----------------------------------------------------------------------------
 */

void
extWorker(
    ExtShared *shared,
    int *order,
    CellDef *rootDef,
    char *logDir,
    bool doResist)
{
    int n = extNumJobList;
    int *done = EXT_DONE(shared, n);
    int *errors = EXT_ERRORS(shared, n);
    int *warnings = EXT_WARNINGS(shared, n);
    int *resDone = EXT_RESDONE(shared, n);
    char logName[1024];
    int k, idx, fbStart;
    FILE *logf;
    CellDef *def;

    extParallelWorker = TRUE;

    /* Magic's SIGTERM handler writes crash backups of modified cells
     * (possibly into the parent's own backup file) and runs exit
     * handlers.  A worker holds only a stale copy of the database and
     * must just die;  the parent notices and extracts serially.
     */
    signal(SIGTERM, SIG_DFL);

    extNullFile = fopen("/dev/null", "w");
    if (extNullFile == NULL) _exit(2);
    TxRedirectFile = extNullFile;

    /* Round 1:  extraction */
    while (!SigInterruptPending && !shared->es_abort)
    {
	k = __atomic_fetch_add(&shared->es_next, 1, __ATOMIC_SEQ_CST);
	if (k >= n) break;
	idx = order[k];
	def = extJobs[idx].ej_def;

	extReplayDescendants(def, rootDef);
	if (extJobs[idx].ej_state == EJ_REPLAYED)
	    extRevertReplay(&extJobs[idx]);

	/* If a message or feedback file cannot be written, stop all the
	 * workers:  the cell is not marked done, so the parent discards
	 * everything and extracts serially.
	 */
	snprintf(logName, sizeof(logName), "%s/%d.log", logDir, idx);
	logf = fopen(logName, "w");
	if (logf == NULL) goto failed;
	TxRedirectFile = logf;

	fbStart = DBWFeedbackCount;
	extNumErrors = extNumWarnings = 0;
	extJobs[idx].ej_savePlane = ExtCell(def, (char *) NULL, (def == rootDef));
	extJobs[idx].ej_state = EJ_EXTRACTED;

	TxRedirectFile = extNullFile;
	if (!extCloseFile(logf)) goto failed;
	if (SigInterruptPending) break;

	snprintf(logName, sizeof(logName), "%s/%d.fb", logDir, idx);
	if (!extSaveFeedback(fbStart, logName)) goto failed;

	errors[idx] = extNumErrors;
	warnings[idx] = extNumWarnings;
	__atomic_store_n(&done[idx], 1, __ATOMIC_SEQ_CST);
	__atomic_fetch_add(&shared->es_ndone, 1, __ATOMIC_SEQ_CST);
    }

    /* Round 2:  resistance extraction, once every .ext file exists */
    if (doResist)
    {
	ResisData *resisdata;

	while (__atomic_load_n(&shared->es_ndone, __ATOMIC_SEQ_CST) < n)
	{
	    if (SigInterruptPending || shared->es_abort) goto finish;
	    usleep(2000);
	}

	resisdata = ResInit();
	UndoDisable();
	while (!SigInterruptPending && !shared->es_abort)
	{
	    k = __atomic_fetch_add(&shared->es_nextRes, 1, __ATOMIC_SEQ_CST);
	    if (k >= n) break;
	    idx = order[k];
	    def = extJobs[idx].ej_def;

	    /* In a serial run every cell has been extracted by now; make
	     * sure this cell and its descendants are in that state here.
	     */
	    extReplayDescendants(def, rootDef);
	    if (extJobs[idx].ej_state == EJ_NONE)
	    {
		extJobs[idx].ej_savePlane = ExtReplayCell(def, (def == rootDef));
		extJobs[idx].ej_state = EJ_REPLAYED;
	    }

	    snprintf(logName, sizeof(logName), "%s/r%d.log", logDir, idx);
	    logf = fopen(logName, "w");
	    if (logf == NULL) goto failed;
	    TxRedirectFile = logf;
	    fbStart = DBWFeedbackCount;

	    ResOptionsFlags |= ResOpt_Signal;
	    resisdata->mainDef = def;
	    resisdata->savePlanes = (struct saveList *) NULL;
	    TxPrintf("Processing cell %s for resistance extraction.\n",
			def->cd_name);
	    ExtResisForDef(def, resisdata);

	    TxRedirectFile = extNullFile;
	    if (!extCloseFile(logf)) goto failed;
	    if (SigInterruptPending) break;

	    snprintf(logName, sizeof(logName), "%s/r%d.fb", logDir, idx);
	    if (!extSaveFeedback(fbStart, logName)) goto failed;
	    __atomic_store_n(&resDone[idx], 1, __ATOMIC_SEQ_CST);
	}
	UndoEnable();
    }
    goto finish;

failed:
    TxRedirectFile = extNullFile;
    shared->es_abort = 1;

finish:
    fflush(NULL);
    _exit(0);
}

#endif /* !EXT_NO_FORK */

/*
 * ----------------------------------------------------------------------------
 *
 * extFreeJobs --
 *
 *	Free the job list built by extExtractStackParallel().
 *
 * ----------------------------------------------------------------------------
 */

void
extFreeJobs(void)
{
    freeMagic(extJobs);
    extJobs = NULL;
    HashKill(&extJobTable);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtParallelAvailable --
 *
 *	Report whether this build can extract in parallel.
 *
 * ----------------------------------------------------------------------------
 */

bool
ExtParallelAvailable(void)
{
#ifdef EXT_NO_FORK
    return FALSE;
#else
    return TRUE;
#endif
}

/*
 * ----------------------------------------------------------------------------
 *
 * extExtractStackParallel --
 *
 *	Extract every CellDef on 'stack' (bottommost first) using up to
 *	ExtNumJobs worker processes, followed by resistance extraction of
 *	every cell if "extract do extresist" is set.
 *
 * Results:
 *	TRUE if the stack was handled.  FALSE if parallel extraction
 *	could not be done, in which case 'stack' is left unchanged and
 *	the caller should fall back to serial extraction.
 *
 * Side effects:
 *	Writes .ext files (and .res.ext files for extresist).  Leaves
 *	'stack' empty on success.  Resets cd_client and clears CDNOEXTRACT
 *	on every def, as the serial code does.  Adds the workers' feedback
 *	entries.  The parent's own database is never modified by the
 *	extraction itself.
 *
 * ----------------------------------------------------------------------------
 */

bool
extExtractStackParallel(
    struct stack *stack,
    CellDef *rootDef)
{
#ifdef EXT_NO_FORK
    /* Not reached:  "extract jobs" never sets more than one job in a
     * build without fork(), and callers check ExtParallelAvailable().
     */
    return FALSE;
#else
    CellDef *def;
    int n, cap, i, nwork, nworkers, started, running, status;
    int errorcnt = 0, warnings = 0;
    int *order;
    size_t shsize;
    ExtShared *shared;
    pid_t pid, *pids;
    bool ok = TRUE, preReplayed;
    bool doResist = (ExtOptions & EXT_DOEXTRESIST) ? TRUE : FALSE;
    char logDir[1024], fileName[1100];
    const char *tmp;

    /* Pop the stack into an array, bottommost cell first */
    n = 0;
    cap = 64;
    extJobs = (ExtJob *) mallocMagic(cap * sizeof(ExtJob));
    while ((def = (CellDef *) StackPop(stack)))
    {
	if (n == cap)
	{
	    ExtJob *newJobs = (ExtJob *) mallocMagic(2 * cap * sizeof(ExtJob));
	    memcpy(newJobs, extJobs, cap * sizeof(ExtJob));
	    freeMagic(extJobs);
	    extJobs = newJobs;
	    cap *= 2;
	}
	extJobs[n].ej_def = def;
	extJobs[n].ej_state = EJ_NONE;
	extJobs[n].ej_savePlane = NULL;
	n++;
    }
    extNumJobList = n;
    if (n == 0)
    {
	freeMagic(extJobs);
	extJobs = NULL;
	return TRUE;
    }

    HashInit(&extJobTable, 2 * n, HT_WORDKEYS);
    for (i = 0; i < n; i++)
    {
	def = extJobs[i].ej_def;
	HashSetValue(HashFind(&extJobTable, (char *) def), INT2CD(i + 1));
	def->cd_client = (ClientData) 0;	/* As the serial code does */
    }

    /* Estimate costs, bottom-up so children are done before parents */
    for (i = 0; i < n; i++)
    {
	bool noextract;

	def = extJobs[i].ej_def;
	DBPropGet(def, "noextract", &noextract);
	if (noextract || (def->cd_flags & CDNOEXTRACT))
	{
	    extJobs[i].ej_cost = 0;
	    continue;
	}
	extJobs[i].ej_cost = extCellTiles(def);
	(void) DBCellEnum(def, extChildCostFunc, (ClientData) &extJobs[i].ej_cost);
	extJobs[i].ej_cost++;
    }

    order = (int *) mallocMagic(n * sizeof(int));
    for (i = 0; i < n; i++) order[i] = i;
    qsort(order, n, sizeof(int), extCostCompare);

    /* Cells with zero cost are up to date (incremental extraction) or
     * marked "noextract";  they are only replayed, not extracted.  With
     * at most one cell to extract and no resistance extraction, forking
     * gains nothing, so let the serial code handle it.
     */
    nwork = 0;
    for (i = 0; i < n; i++)
	if (extJobs[i].ej_cost > 0) nwork++;
    if (doResist) nwork = n;
    if (nwork <= 1)
	goto restore;

    nworkers = (ExtNumJobs < nwork) ? ExtNumJobs : nwork;

    shsize = EXT_SHSIZE(n);
    shared = (ExtShared *) mmap(NULL, shsize, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANON, -1, 0);
    if (shared == (ExtShared *) MAP_FAILED)
    {
	TxError("Parallel extraction:  cannot allocate shared memory.\n");
	goto restore;
    }
    memset(shared, 0, shsize);

    tmp = getenv("TMPDIR");
    snprintf(logDir, sizeof(logDir), "%s/magic-ext-XXXXXX",
		(tmp && *tmp) ? tmp : "/tmp");
    if (mkdtemp(logDir) == NULL)
    {
	TxError("Parallel extraction:  cannot create temporary directory.\n");
	munmap(shared, shsize);
	goto restore;
    }

    TxPrintf("Extracting %d cell%s with %d parallel jobs.\n", nwork,
		(nwork == 1) ? "" : "s", nworkers);
    TxFlush();

    /* If replaying a cell is cheap (see ExtReplayIsCheap()), replay every
     * cell here once, bottom-up as a serial run would extract them.  The
     * workers inherit that state (shared copy-on-write) instead of each
     * replaying every cell it needs;  a worker only reverts the cell it
     * extracts.  The parent reverts everything once the workers are done.
     */
    preReplayed = FALSE;
    if (ExtReplayIsCheap())
    {
	for (i = 0; i < n && !SigInterruptPending; i++)
	{
	    def = extJobs[i].ej_def;
	    extJobs[i].ej_savePlane = ExtReplayCell(def, (def == rootDef));
	    extJobs[i].ej_state = EJ_REPLAYED;
	}
	preReplayed = TRUE;
    }
    fflush(NULL);

    pids = (pid_t *) mallocMagic(nworkers * sizeof(pid_t));
    running = 0;
    for (started = 0; started < nworkers; started++)
    {
	pid = fork();
	if (pid == 0)
	    extWorker(shared, order, rootDef, logDir, doResist); /* No return */
	if (pid < 0)
	{
	    TxError("Parallel extraction:  fork failed: %s\n", strerror(errno));
	    break;
	}
	pids[running++] = pid;
    }
    /* If no worker could be started, nothing can make progress */
    if (running == 0) shared->es_abort = 1;

    /* Wait for all workers.  Only the workers' own process IDs are
     * waited for, so other children of this process (for example
     * started with Tcl "exec ... &") are left alone.  If a worker dies,
     * tell the others to stop so none of them waits forever at the
     * extresist barrier.
     */
    while (running > 0)
    {
	int k, reaped = 0;

	for (k = 0; k < running; k++)
	{
	    pid = waitpid(pids[k], &status, WNOHANG);
	    if (pid == 0) continue;
	    if ((pid < 0) && (errno == EINTR)) continue;
	    if ((pid < 0) || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	    {
		TxError("Parallel extraction:  a worker did not finish normally.\n");
		shared->es_abort = 1;
	    }
	    pids[k--] = pids[--running];
	    reaped++;
	}
	if (running == 0 || reaped > 0) continue;
#ifdef MAGIC_WRAPPER
	/* Keep the layout window and console responsive while waiting,
	 * as extSubtree() does during a long serial extraction.
	 */
	while (Tcl_DoOneEvent(TCL_WINDOW_EVENTS | TCL_DONT_WAIT) != 0);
#endif
	usleep(10000);
    }
    freeMagic(pids);

    /* Undo the replay done before forking */
    if (preReplayed)
	for (i = n - 1; i >= 0; i--)
	{
	    if (ExtOptions & EXT_DOUNIQUE)
		ExtRevertUniqueCell(extJobs[i].ej_def);
	    if (extJobs[i].ej_savePlane != NULL)
		ExtRevertSubstrate(extJobs[i].ej_def, extJobs[i].ej_savePlane);
	    extJobs[i].ej_savePlane = NULL;
	    extJobs[i].ej_state = EJ_NONE;
	}

    for (i = 0; i < n; i++)
    {
	if (!EXT_DONE(shared, n)[i]) ok = FALSE;
	if (doResist && !EXT_RESDONE(shared, n)[i]) ok = FALSE;
    }

    /* If the run is incomplete for any reason other than an interrupt,
     * discard everything the workers produced and extract serially, so
     * that no message or feedback entry appears twice.  Otherwise replay
     * messages and feedback in serial (bottom-up) order:  first the
     * extraction of every cell, then resistance extraction.
     */
    if (!ok && !SigInterruptPending)
    {
	extDiscardOutput(logDir, n);
	munmap(shared, shsize);
	TxError("Parallel extraction incomplete;  extracting serially.\n");
	goto restore;
    }

    for (i = 0; i < n; i++)
    {
	snprintf(fileName, sizeof(fileName), "%s/%d.log", logDir, i);
	extPrintLog(fileName);
	snprintf(fileName, sizeof(fileName), "%s/%d.fb", logDir, i);
	extLoadFeedback(fileName);
	if (EXT_DONE(shared, n)[i])
	{
	    errorcnt += EXT_ERRORS(shared, n)[i];
	    warnings += EXT_WARNINGS(shared, n)[i];
	}
    }
    if (doResist)
	for (i = 0; i < n; i++)
	{
	    snprintf(fileName, sizeof(fileName), "%s/r%d.log", logDir, i);
	    extPrintLog(fileName);
	    snprintf(fileName, sizeof(fileName), "%s/r%d.fb", logDir, i);
	    extLoadFeedback(fileName);
	}
    rmdir(logDir);
    munmap(shared, shsize);

    for (i = 0; i < n; i++)
	extJobs[i].ej_def->cd_flags &= ~CDNOEXTRACT;
    freeMagic(order);
    extFreeJobs();

    if (errorcnt > 0)
	TxError("Total of %d error%s (check feedback entries).\n",
		errorcnt, errorcnt != 1 ? "s" : "");
    if (warnings > 0)
	TxError("Total of %d warning%s.\n",
		warnings, warnings != 1 ? "s" : "");
    return TRUE;

restore:
    /* Put the stack back as it was (bottommost cell on top) */
    for (i = n - 1; i >= 0; i--)
	StackPush((ClientData) extJobs[i].ej_def, stack);
    freeMagic(order);
    extFreeJobs();
    return FALSE;
#endif /* EXT_NO_FORK */
}
