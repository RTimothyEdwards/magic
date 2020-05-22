
#include <stdio.h>
#include "textio/textio.h"

#ifndef lint
static char rcsid[] = "$Header$";
#endif  /* not lint */

main()
{
    char str[100];
    char *args[4];
    int argCount;

    TxInitTerm();

    strcpy(str, "");

    while (strcmp(str,"q") != 0)
    {
	int i;

	TxPrintf("-->");
	TxGetLine(str, 99);	
	TxPrintf("Line is '%s'\n", str);
	if (!ParsSplit(str, 3, &argCount, args))
	    TxError("Parser failed\n");

	TxPrintf("ARgc = %d\n", argCount);
        for (i = 0; i < argCount; i++)
	{
	    TxPrintf("  arg %d: '%s'\n", i, args[i]);
	}
    }

    TxCloseTerm();
}
