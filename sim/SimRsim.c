/*
 * SimRsim.c -
 *
 *	This file provides routines for Magic to communicate with Rsim/Irsim.
 *	Communications takes place using two pipes, one for Magic to
 *	send a command to the simulator, the other to get back the reply.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1985, 1990 Regents of the University of California. * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 * of California.  All rights reserved.
 *
 */

#ifdef RSIM_MODULE

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>

#include "utils/magic.h"
#include "utils/stack.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/signals.h"
#include "utils/styles.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "sim/sim.h"
#include <errno.h>

static bool InitRsim();

#define BUF_SIZE	1024
#define	LINEBUF_SIZE	256
#define	READBUF_SIZE	4096 + LINEBUF_SIZE

#define E_RUNNING	"Simulator already running.\n"
#define E_PIPE1OP	"Could not create Magic to Rsim pipe.\n"
#define E_PIPE2OP	"Could not create Rsim to Magic pipe.\n"
#define E_NOFORK	"Could not fork process.\n"
#define E_NOSTART	"Rsim not started.\n"
#define E_PIPERD	"Error reading pipe from Rsim.\n"
#define E_PIPEWR	"Could not write on pipe to Rsim.\n"

#define P_READ		0
#define P_WRITE		1

static int 	status;
static int	pipeIn;					/* Rsim --> Magic */
static int	pipeOut;	 			/* Magic --> Rsim */
static char 	keyBoardBuf[BUF_SIZE];
static bool	RsimJustStarted = TRUE;
static char	rsim_prompt[20];
static int	prompt_len;
static int      rsim_pid;

bool	SimRsimRunning = FALSE;
bool	SimHasCoords = FALSE;
bool	SimGetReplyLine();

/* Forward declaration */
void SimStopRsim();

/*
 *-----------------------------------------------------------------------
 * SimGetNodeCommand
 *
 *	This function returns the "full" name of the rsim command if 'cmd'
 *	should be applied to the selected node(s) or NULL if the command 
 *	should be shipped without any node names.
 *
 * Results:
 *	A ptr to the command name or NULL.
 *
 * Side effects:
 *	None.
 *-----------------------------------------------------------------------
 */

char *
SimGetNodeCommand(cmd)
    char *cmd;
{
    /* This table is used to define which Rsim commands are applied to
     * each node in the selection.  Depending on the command, you
     * woudn't want to send a command to rsim for each node.  For example
     * given the "s" command (to step the clock), you wouldn't want to
     * step the clock once for every node in the selection.
     */

    static char *RsimNodeCommands[] =
      {
	"?",
	"!",
	"analyzer",
	"d",
	"h",
	"l",
	"path",
	"t",
	"u",
	"w",
	"x",
	NULL
      };
    int  cmdNum;

    cmdNum = Lookup( cmd, RsimNodeCommands );
    cmd = (cmdNum >= 0) ? RsimNodeCommands[cmdNum] : (char *) NULL;

    return( cmd );
}


/*
 *-----------------------------------------------------------------------
 * SimStartRsim
 *
 *	This procedure is used to fork the rsim process.  It takes a list of
 *	arguements to pass to rsim when initiating the fork.  The
 * 	environment variable RSIM is first checked to find the pathname for
 *	rsim/irsim.  If this variable does not exist, then BIN_DIR/irsim
 *	is then used.
 *
 * Results:
 *	TRUE if the fork was successful.
 *
 * Side effects:
 *	None.
 *-----------------------------------------------------------------------
 */

bool
SimStartRsim(argv)
    char *argv[];		/* list of rsim args for the fork */
{

    int child;
    int magToRsimPipe[2];
    int rsimToMagPipe[2];
    char *getenv();
    char rsimfile[256];
    char *cad = BIN_DIR;
    char *src, *dst;

    /* don't start another rsim if one is already running */

    if (SimRsimRunning) {
	TxPrintf(E_RUNNING);
	return(FALSE);
    }

    /* Create the pipes.  One is for Magic sending to rsim, the other
     * is for rsim sending to Magic.
     */

    if (pipe(magToRsimPipe) < 0) {
	TxPrintf(E_PIPE1OP);
	return(FALSE);
    }

    if (pipe(rsimToMagPipe) < 0) {
	TxPrintf(E_PIPE2OP);
	return(FALSE);
    }

    /* Look for rsim; check for environ var first; if none, then
     * try to open the one located in BIN_DIR.
     */

    src = getenv("RSIM");
    if( src != NULL )
	strcpy(rsimfile, src);
    else {
	src = cad;
	dst = rsimfile;
	if (PaExpand(&src, &dst, 100) == -1) {
	    TxError ("Could not find " BIN_DIR "\n");
	    return(FALSE);
	}
	strcat(rsimfile, "/irsim");
    }
#ifndef NO_ACCESS_CALL
    if( access( rsimfile, 1 ) != 0 )
    {
	TxPrintf("can not execute '%s'\n", rsimfile );
	return(FALSE);
    }
#endif


    FORK(child);
/*
#ifdef SYSV
    child = fork();
#else
    child = vfork();
#endif
*/
    if (child == -1) {
	close(magToRsimPipe[P_READ]);
	close(magToRsimPipe[P_WRITE]);
	close(rsimToMagPipe[P_READ]);
	close(rsimToMagPipe[P_WRITE]);

	TxPrintf(E_NOFORK);
	return(FALSE);
    }

    if (child > 0) {

	/* This is the parent */

	SimRsimRunning = TRUE;
	close(magToRsimPipe[P_READ]);
	close(rsimToMagPipe[P_WRITE]);
	pipeIn = rsimToMagPipe[P_READ];
	pipeOut = magToRsimPipe[P_WRITE];
	rsim_pid = child;
    }
    else {

	int  i;

	/* This is the child */

	close(magToRsimPipe[P_WRITE]);
	close(rsimToMagPipe[P_READ]);

	dup2(magToRsimPipe[P_READ], fileno(stdin));
	dup2(rsimToMagPipe[P_WRITE], fileno(stderr));
	dup2(rsimToMagPipe[P_WRITE], fileno(stdout));

	for( i = 3; i < 15; i++ )
	    close( i );

	/* try our best, folks..... */

	execvp(rsimfile, argv);
	_exit(5);			/* pick a number, any number */

    }
    return(FALSE);			/* to keep lint happy */
}    


/*
 *-----------------------------------------------------------------------
 * SimConnectRsim
 *
 *	This procedure is called when Magic is sending commands to Rsim and
 *	awaiting a reply.  The reply is retrieved by successive calls
 *	to SimGetReplyLine() which returns one line of the reply at a time.
 *
 *	Magic sends a command to Rsim through one pipe, and
 *	Rsim's reply comes back from the other pipe.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The reply generated by Rsim is printed.
 *	The reply buffer from SimGetReplyLine is statically allocated.  
 *	Subsequent calls will change the contents of this buffer.
 *
 *-----------------------------------------------------------------------
 */

void
SimConnectRsim(escRsim)
    bool escRsim;			/* TRUE if we should escape back to Magic */
{
    static char HELLO_MSG[] = 
	"Type \"q\" to quit simulator or \".\" to escape back to Magic.\n";

    char *replyLine;		/* used to hold one line of the Rsim reply */

    if (!SimRsimRunning) {
	TxPrintf(E_NOSTART);
	return;
    }

    /* read the header of the rsim reply and determine the prompt */

    if( RsimJustStarted ) {
	if( ! InitRsim( escRsim ? NULL : HELLO_MSG ) )
	    return;
    }

    if (escRsim) {
	RsimJustStarted = FALSE;
	return;
    }

    if (!RsimJustStarted) {
	TxPrintf("%s", HELLO_MSG);
    }


    while (TRUE) {

	/* exceptions can toggle this flag. */

	if (!SimRsimRunning) {
	    return;
	}
	TxPrintf("%s", rsim_prompt);

	/* Read the user's command for Rsim */

	if (TxGetLine(keyBoardBuf, BUF_SIZE) == 0) keyBoardBuf[0] = 0;

	/* prepare the Rsim command string */

	strcat(keyBoardBuf, "\n");

	/* check to see if we quit Rsim or escape back to Magic */

	if ((keyBoardBuf[0] == '.') && (keyBoardBuf[1] == '\n')) {
	    RsimJustStarted = FALSE;
	    return;
	}
	if ((keyBoardBuf[0] == 'q') && (keyBoardBuf[1] == '\n')) {
	    SimStopRsim();
	    return;
	}

	/* Send the command to Rsim and get the reply. */

	if (write(pipeOut, keyBoardBuf, strlen(keyBoardBuf)) < 0) {
	    TxPrintf(E_PIPEWR);
	    SimStopRsim();
	    return;
	}
	if (!SimGetReplyLine(&replyLine)) {
	    return;
	}
	while (replyLine != NULL) {
	    TxPrintf("%s\n",replyLine);
	    if (!SimGetReplyLine(&replyLine)) {
		return;
	    }
	}
    }
}


/*
 *-----------------------------------------------------------------------
 * InitRsim
 *	Read the initial header from rsim and determine the prompt.
 *	The prompt is found by searching for the string enclosed
 *	the last "\n" and "> ".
 *
 * Results:
 *	Returns TRUE if rsim started correctly, else FALSE.
 *
 * Side effects:
 *	Sets the rsim prompt and length.
 *-----------------------------------------------------------------------
 */

bool
InitRsim(hello_msg)
    char  *hello_msg;
{
    char	buff[READBUF_SIZE];
    char	*last;
    int		nchars;
    bool	first_time = TRUE;

    prompt_len = 0;
    do
    {
	nchars = 0;
	last = buff;
	if( SimFillBuffer( buff, &last, &nchars ) <= 0 )
	{
	    SimStopRsim();			/* rsim must have died */
	    TxPrintf( "<Simulator is dead>\n" );
	    return( FALSE );
	}
	buff[nchars] = '\0';

	if( last[-1] == '>' && *last == ' ' )
	{
	    for( last--; last > buff && last[-1] != '\n'; last-- );
	    strcpy( rsim_prompt, last );
	    prompt_len = strlen( rsim_prompt );
	    *last = '\0';
	}

	if( first_time )
	{
	    TxPrintf("Be sure your sim file matches the root cell of this window.\n");
	    if( hello_msg )
		TxPrintf( "%s", hello_msg );
	    first_time = FALSE;
	}
	if( *buff )
	    TxPrintf( "%s", buff );

    } while( prompt_len == 0 );

    if( write( pipeOut, "has_coords\n", 11 ) < 0 )
    {
	TxPrintf(E_PIPEWR);
	SimStopRsim();
	return(FALSE);
    }

    SimHasCoords = FALSE;
    do
    {
	if( ! SimGetReplyLine( &last ) )
	    return( FALSE );

	if( last != NULL && strncmp( last, "YES", 3 ) == 0 )
	    SimHasCoords = TRUE;
    }
    while( last );

    return( TRUE );
}


/*
 *-----------------------------------------------------------------------
 * SimStopRsim
 *
 *	This procedure is called to kill off the Rsim process.  The
 *	exit condition is checked and a message is printed if necessary.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The child Rsim process is killed.
 *
 *-----------------------------------------------------------------------
 */

void
SimStopRsim()
{
    int  pid;

    if (SimRsimRunning) {

	/* closing the pipes to Rsim have the effect of killing it */

	close(pipeOut);
	close(pipeIn);

	/* set the Rsim state flags */

	RsimJustStarted = TRUE;
	SimRsimRunning = FALSE;

	kill(rsim_pid, SIGHUP);		/* just in case rsim hangs */

	if (WaitPid (rsim_pid, &status) == -1)
	  return;
	pid = rsim_pid;

	switch (status & 0xFF) {
	    case 0 :
		break;
	    case 2 :
		TxPrintf("Simulator interrupted.\n");
		break;
	    default :
		TxPrintf("Simulator terminated abnormally.\n");
		break;
	}
    }
}


/*
 *-----------------------------------------------------------------------
 * RsimErrorMsg
 *
 *	This procedure prints out an error message.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */

void
RsimErrorMsg()
{
    static char msg[] = "The simulator must be running before this command "
		"can be executed.  To do\n"
		"this enter the command \"rsim <options> <filename>\".  "
		"To escape back to\n"
		"Magic enter \".\" in response to the simulator prompt.\n";

    TxPrintf("%s", msg);
}


/*
 *-----------------------------------------------------------------------
 * SimRsimIt
 *
 *	This procedure takes an Rsim command and a node name to apply
 *	the command to, constructs a complete Rsim command and sends it
 *	to Rsim to process.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything is set up so that GetReplyLine() should be called
 *	after this routine to read the Rsim output for each node.
 *	The reply buffer is statically allocated.  Subsequent calls will
 *	change the contents of this buffer.
 *	
 *-----------------------------------------------------------------------
 */

void
SimRsimIt(cmd, nodeName)
    char *cmd;
    char *nodeName;
{

    static char cmdStr[256];
    static char cleanName[256];
    char *strptr;

    cmdStr[0] = 0;

    if (!SimRsimRunning) {
	RsimErrorMsg();
	return;
    }

    /* change the node name to a form Rsim will accept.  That is:
     * if CHANGE_SQBRACKET is defined (it really should not) then
     * "[" and "]" in the path name must be changed to a "." Also, the
     * trailing "#" of Magic generated node names must also be removed.
     */

    strcpy(cleanName, nodeName);
    strptr = cleanName;
    while (*strptr != 0) {
#ifdef CHANGE_SQBRACKET
        if ((*strptr == '[') || (*strptr == ']')) *strptr = '.';
#endif
	strptr++;
    }
    if (*--strptr == '#') {
	*strptr = 0;
    }
    sprintf(cmdStr, "%s %s\n", cmd, cleanName);

    /* send the command to Rsim */

    if (write(pipeOut, cmdStr, strlen(cmdStr)) < 0) {
	TxPrintf(E_PIPEWR);
	SimStopRsim();
    }
}


/*
 *-----------------------------------------------------------------------
 * SimFillBuffer
 *
 *	This procedure reads characters from Rsim via a pipe and
 *	places the characters into a buffer pointed by pLastChar.
 *	It is assumed the buffer is at least READBUF_SIZE charcters
 *	large, and that charCount contains the number of charcters
 *	which remain unprocessed in the buffer.
 *	If an interrupt is received while waiting for input from
 *	rsim, then the signal is propagated to rsim and we try the
 *	read again; this should get the simulator to its top level
 *	command parser (or kill it depending on the simulator).
 *
 * Results:
 *	Number of characters read into the buffer.
 *
 * Side effects:
 *	pLastChar is updated to point to the last valid character
 *	in the buffer.  charCount is also updated to contain the
 *	total number of unprocessed characters in the buffer.
 *	Some i/o may take place.
 *
 *-----------------------------------------------------------------------
 */

int
SimFillBuffer(buffHead, pLastChar, charCount)
    char *buffHead;			/* ptr to start of buffer */
    char **pLastChar;			/* used to return ptr to last char 
					 * in the buffer.
					 */
    int *charCount;			/* number of chars in the buffer */
{
    int 	charsRead = 0;
    char 	*temp;
    int		n, nfd;
#if defined(SYSV) || defined(CYGWIN) || defined(__FreeBSD__) || defined(__APPLE__)
    fd_set readfds, writefds, exceptfds;
#else
    int		nr, nex;
#endif  /* SYSV */

    struct timeval timeout;

    /* Set the timeout to 5 seconds so we don't block indefinitely if	*/
    /* something goes wrong with the pipe.				*/

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    /* read reply from Rsim */

#if defined(SYSV) || defined(CYGWIN) || defined(__FreeBSD__) || defined(__APPLE__)
    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);
#endif  /* SYSV */

    nfd = pipeIn + 1;

try_again:

#if defined(SYSV) || defined(CYGWIN) || defined(__FreeBSD__) || defined(__APPLE__)
    FD_SET(pipeIn, &readfds);
    FD_ZERO(&writefds);
    FD_SET(pipeIn, &exceptfds);
    n = select(nfd, &readfds, &writefds, &exceptfds, &timeout);

#else /* !SYSV */
    nr = nex = 1 << pipeIn;
    n = select(nfd, &nr, (int *) NULL, &nex, &timeout);

#endif

    if (n == 0)
	return 0;	/* select() timed out */

    else if (n < 0)
    {
	if (errno == EINTR)
	{
	    if (SigInterruptPending)
	    {
		kill(rsim_pid, SIGINT);
		SigInterruptPending = FALSE;
	    }
	    goto try_again;
	}
    }

    temp = *pLastChar;
    charsRead = read(pipeIn, temp, (READBUF_SIZE - 1 - *charCount));

    if (charsRead > 0) {
	temp += charsRead;
	if (*charCount == 0) {
	    temp--;
	}
	*pLastChar = temp;
	*charCount += charsRead;
    }
    ASSERT(((buffHead + READBUF_SIZE) > *pLastChar), "SimFillBuffer");
    return(charsRead);
}

/*
 *-----------------------------------------------------------------------
 * SimShiftChars
 *
 *	This procedure shifts unprocessed charcters in a buffer
 *	to the beginning of the buffer and updates the head and
 *	tail pointers to the valid buffer characters.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Valid data in the buffer is shifted to the beginning of
 *	the buffer.
 *
 *-----------------------------------------------------------------------
 */

void
SimShiftChars(buffStart, lineStart, lastChar)
    char *buffStart;		/* beginning of buffer */
    char **lineStart;		/* ptr to first valid char in buffer */
    char **lastChar;		/* ptr to last valid char in buffer */
{
    char *temp;
    char *temp1;

    if (buffStart == *lineStart) {
	return;
    }

    for (temp = buffStart, temp1 = *lineStart; temp1 <= *lastChar;) {
	*temp++ = *temp1++;
    }
    temp--;
    *lineStart = buffStart;
    *lastChar = temp;
}

/*
 *-----------------------------------------------------------------------
 * SimFindNewLine
 *
 *	This procedure searches for a '\n' in the char buffer delimited by
 *	buffStart and buffEnd.
 *
 * Results:
 *	returns a ptr to the '\n' in the buffer.  If no '\n' is found,
 *	NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */

char *
SimFindNewLine(buffStart, buffEnd)
    char 	*buffStart;		/* first char in buffer */
    char	*buffEnd;		/* last char in buffer */
{
    char *sp;

    for (sp = buffStart; sp <= buffEnd; sp++) {
	if (*sp == '\n') {
	    return(sp);
	}
    }
    return(NULL);
}

/*
 *-----------------------------------------------------------------------
 * SimGetReplyLine
 *
 *	This procedure returns the next line of an Rsim reply.  It does this
 *	by maintaining a character buffer to read the Rsim reply.   This
 *	buffer is scanned for '\n' which delimit lines, and lines are
 *	returned from this buffer.  This routine automatically refills
 *	the reply buffer if no '\n' charcters are found.  Head and tail
 *	pointers to the "unprocessed" charcters in the buffer (those
 *	characters beloning to a reply line in the buffer which has not 
 *	yet been returned) are maintained.
 *	
 *	We assume that the longest line Rsim will return is less than
 *	256 characters, and that at most 4K characters can be read
 *	from a pipe in one read call.
 *
 * Results:
 *	SimGetReplyLine returns TRUE if replyLine is a valid value, 
 *	otherwise it is FALSE.
 *	If we find a line containing only the rsim prompt, replyLine
 *	points to a NULL pointer and TRUE is returned.
 *
 * Side effects:
 *	replyLine contains a pointer to the next Rsim reply line.
 *	The buffer returned by replyLine is statically allcoated, so
 *	subsequent calls to this routine will change this buffer.
 *
 *-----------------------------------------------------------------------
 */

bool
SimGetReplyLine(replyLine)
    char **replyLine;
{
    static char	simReadBuff[READBUF_SIZE];	/* buffer in which to read the 
						 * rsim reply before processing
						 * it.  This is big enough for
						 * one incomplete Rsim line
						 * (256 chars) and for the
						 * additional read from the
						 * pipe to complete the line
						 * (4K chars).
						 */
    static char *lineStart = simReadBuff;	/* points to the first character
						 * of the next reply line.
						 */
    static char *lastChar = simReadBuff;	/* points the the last valid
						 * char in the input buffer.
						 */
    static int	charsInBuff = 0;		/* number of characters left
						 * in input buffer to process.
						 */
    char *strptr;
    char *strptr1;

    /* keep reading characters until we have at least enough for a prompt */

    while (charsInBuff < prompt_len) {
	SimShiftChars(simReadBuff, &lineStart, &lastChar);
	status = SimFillBuffer(simReadBuff, &lastChar, &charsInBuff);
	if (status == 0) {	
	    /* no more characters to read; stop Rsim */
	    SimStopRsim();
	    *replyLine = NULL;
	    return(FALSE);
	}
	if (status < 0) {
	    TxPrintf(E_PIPERD);
	    *replyLine = NULL;
	    return(FALSE);
	}
    }

    /* check for the prompt at the end of the buffer, if found then
     * reset buffer pointers and character count. 
     */

    if (!(strncmp(rsim_prompt, lineStart, prompt_len)))  {
	lineStart = lastChar = simReadBuff;
	charsInBuff = 0;
	*replyLine = NULL;
	return(TRUE);
    }

    /* Now try to extract a line out of the buffer. */

    strptr = SimFindNewLine(lineStart, lastChar);

    if (!strptr) {

	/* haven't found a '\n' in the buffer yet */
	
	SimShiftChars(simReadBuff, &lineStart, &lastChar);
	strptr1 = lastChar;

	/* keep trying to read characters until we find a '\n'; we
	 * are assuming rsim reply lines are no more than LINEBUF_SIZE
	 * characters in length.
	 */

	while (TRUE) {
	    strptr = SimFindNewLine(lineStart, lastChar);
	    if ((charsInBuff > LINEBUF_SIZE) || (strptr)) {
		break;
	    }
	    strptr1 = lastChar;
	    status = SimFillBuffer(simReadBuff, &lastChar, &charsInBuff);
	    if (status == 0) {	
		/* no more characters to read; stop Rsim */
		SimStopRsim();
		*replyLine = NULL;
		return(FALSE);
	    }
	    if (status < 0) {
		TxPrintf(E_PIPERD);
		*replyLine = NULL;
		return(FALSE);
	    }
	}
    }

    if (!strptr) {
	TxPrintf("Error in SimGetReplyLine  -- Rsim line longer than 256 chars\n");
	*replyLine = NULL;
	return(FALSE);
    }

    *strptr = 0;				/* change the '\n' to a NULL */
    strptr1 = lineStart;			/* string to return */
    lineStart = strptr + 1;			/* start of next line */
    charsInBuff -= (strlen(strptr1) + 1); 	/* + 1 because of the '\n' */
    if (charsInBuff == 0) {			/* reset buffer pointers */
	lineStart = lastChar = simReadBuff;
    }
    *replyLine = strptr1;
    return(TRUE);
}

/* ----------------------------------------------------------------------------
 *
 * SimInit --
 *
 *	Initialize this module.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Just initialization.
 *
 * ----------------------------------------------------------------------------
 */

void
SimInit()
{
    static char *rsimdoc =
"You are currently using the \"rsim\" tool.  The button actions are:\n\
   left    - move the box so its lower-left corner is at cursor position\n\
   right   - resize box by moving upper-right corner to cursor position\n\
   middle  - display the Rsim node values of the selected paint\n\
You can move or resize the box by different corners by pressing left\n\
    or right, holding it down, moving the cursor near a different corner\n\
    and clicking the other (left or right) button down then up without\n\
    releasing the initial button.  Rsim must already have been started\n\
    to display the node values on the circuit.\n";

    DBWAddButtonHandler("rsim", SimRsimHandler, STYLE_CURS_RSIM, rsimdoc);
}

#endif	/* RSIM_MODULE */
