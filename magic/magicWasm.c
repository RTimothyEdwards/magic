/*
 * magicWasm.c --
 *
 *  Headless Emscripten entry points for running Magic without a
 *  terminal-driven event loop.
 */

#include <stdio.h>
#include <stdlib.h>

#ifdef MAGIC_WRAPPER
#include "tcltk/tclmagic.h"
#endif

#include "utils/main.h"
#include "utils/magic.h"
#include "utils/paths.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "graphics/graphics.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

static int
magicWasmEnsureCadRoot(void)
{
    if (getenv("CAD_ROOT") == NULL)
    {
	if (setenv("CAD_ROOT", "/", 0) != 0)
	{
	    TxError("Failed to set CAD_ROOT for the WASM runtime.\n");
	    return -1;
	}
    }

    return 0;
}

#ifdef MAGIC_WRAPPER
/* Forward decl — Tclmagic_Init bootstraps the Tcl interpreter (registers
 * the magic::initialize command and calls Tcl_InitStubs(), which sets
 * tclStubsPtr).  Without this, any Tcl_X macro dereferences a NULL stubs
 * pointer at runtime (crashes the wasm).  The actual magic:: commands
 * (magic::load, magic::gds, etc.) are registered separately by
 * TclmagicRegisterCommands() after magicMainInit() populates the clients. */
extern int Tclmagic_Init(Tcl_Interp *interp);
#endif

EMSCRIPTEN_KEEPALIVE int
magic_wasm_init(void)
{
    static char *argv[] = {
	"magic",
	"-d",
	"null",
	"-T",
	"minimum",
	NULL
    };

    if (magicWasmEnsureCadRoot() != 0)
	return -1;

#ifdef MAGIC_WRAPPER
    /* In wrapper mode, magic's code (and our PaExpand path expansion) reaches
     * for `magicinterp` to resolve $env vars via Tcl_GetVar.  In the normal
     * Linux flow Tclmagic_Init() is called by tclsh after dlopen(); here we
     * embed the interp directly, so we have to bootstrap it before
     * magicMainInit() runs anything that might touch Tcl.
     *
     * Note: we deliberately avoid TxError here — in MAGIC_WRAPPER mode
     * TxError flushes via Tcl_EvalEx through tclStubsPtr, which only becomes
     * non-NULL after Tclmagic_Init -> Tcl_InitStubs.  So early errors go
     * straight to stderr. */
    if (magicinterp == NULL)
    {
	Tcl_Interp *interp = Tcl_CreateInterp();
	if (interp == NULL)
	{
	    fprintf(stderr, "magic_wasm_init: Tcl_CreateInterp returned NULL\n");
	    return -1;
	}
	/* Tcl_Init loads /init.tcl from the Tcl library directory; in our
	 * embedded VFS that script isn't shipped, so failure here is expected
	 * and non-fatal — the interpreter itself is still usable for embedded
	 * evaluation, which is all we need. */
	(void)Tcl_Init(interp);
	consoleinterp = interp;
	if (Tclmagic_Init(interp) != TCL_OK)
	{
	    fprintf(stderr, "magic_wasm_init: Tclmagic_Init failed: %s\n",
		Tcl_GetStringResult(interp));
	    return -1;
	}
    }
#endif

    {
	static int commandsRegistered = FALSE;
	int rc = magicMainInit(5, argv);
#ifdef MAGIC_WRAPPER
	if (rc == 0 && !commandsRegistered)
	{
	    TclmagicRegisterCommands(magicinterp);
	    commandsRegistered = TRUE;
	}
#endif
	return rc;
    }
}

EMSCRIPTEN_KEEPALIVE int
magic_wasm_run_command(const char *command)
{
    int status;

    status = magic_wasm_init();
    if (status != 0)
	return status;

    if ((command == NULL) || (*command == '\0'))
	return 0;

    /* Set the current point to the center of the screen so that
     * WindSendCommand routes the command to the layout window client
     * (not the window-management border client which handles point 0,0).
     */
    TxSetPoint(GrScreenRect.r_xtop / 2, GrScreenRect.r_ytop / 2,
	    WIND_UNKNOWN_WINDOW);

#ifdef MAGIC_WRAPPER
    /* In wrapper mode the command is Tcl. Evaluate it via the magic interp;
     * the magic backend is reachable through ::magic:: ensemble commands. */
    if (magicinterp == NULL)
	return -1;
    return Tcl_EvalEx(magicinterp, command, -1, 0);
#else
    return TxDispatchString(command, FALSE);
#endif
}

EMSCRIPTEN_KEEPALIVE int
magic_wasm_source_file(const char *path)
{
    int status;

    status = magic_wasm_init();
    if (status != 0)
	return status;

    if ((path == NULL) || (*path == '\0'))
	return -1;

    TxSetPoint(GrScreenRect.r_xtop / 2, GrScreenRect.r_ytop / 2,
	    WIND_UNKNOWN_WINDOW);

#ifdef MAGIC_WRAPPER
    /* In wrapper mode the file contains Tcl; evaluate it through the
     * Tcl interpreter so that magic:: commands are dispatched via
     * _tcl_dispatch just like magic_wasm_run_command does for strings. */
    if (magicinterp == NULL)
	return -1;
    return Tcl_EvalFile(magicinterp, path);
#else
    {
	FILE *f = PaOpen((char *)path, "r", (char *)NULL, ".", (char *)NULL,
		(char **)NULL);
	if (f == NULL)
	{
	    TxError("Unable to open command file \"%s\".\n", path);
	    return -1;
	}
	TxDispatch(f);
	fclose(f);
	return 0;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE void
magic_wasm_update(void)
{
    if (magic_wasm_init() == 0)
	WindUpdate();
}
