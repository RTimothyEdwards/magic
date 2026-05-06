/*
 * magicWasm.c --
 *
 *  Headless Emscripten entry points for running Magic without a
 *  terminal-driven event loop.
 */

#include <stdio.h>
#include <stdlib.h>

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

    return magicMainInit(5, argv);
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

    return TxDispatchString(command, FALSE);
}

EMSCRIPTEN_KEEPALIVE int
magic_wasm_source_file(const char *path)
{
    FILE *f;
    int status;

    status = magic_wasm_init();
    if (status != 0)
	return status;

    if ((path == NULL) || (*path == '\0'))
	return -1;

    f = PaOpen((char *)path, "r", (char *)NULL, ".", (char *)NULL,
	    (char **)NULL);
    if (f == NULL)
    {
	TxError("Unable to open command file \"%s\".\n", path);
	return -1;
    }

    /* Set the current point to the centre of the screen so that
     * WindSendCommand routes all commands from the file to the layout
     * window client, just as magic_wasm_run_command does for single
     * commands.  Without this, commands arrive with point (0,0) and
     * end up in the border/windClient context where most commands are
     * unknown.
     */
    TxSetPoint(GrScreenRect.r_xtop / 2, GrScreenRect.r_ytop / 2,
	    WIND_UNKNOWN_WINDOW);

    TxDispatch(f);
    fclose(f);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void
magic_wasm_update(void)
{
    if (magic_wasm_init() == 0)
	WindUpdate();
}
