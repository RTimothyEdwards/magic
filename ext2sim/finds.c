#include "utils/magic.h"
#include "utils/hash.h"
#include <stdio.h>
#include <ctype.h>

char *
token(pnext)
    char **pnext;
{
    char *cp, *ep;

    cp = *pnext;
    while (*cp && isspace(*cp)) cp++;
    if (*cp == '\0')
	return (NULL);

    for (ep = cp; *ep && !isspace(*ep); ep++) /* Nothing */;
    if (*ep)
	*ep++ = '\0';
    *pnext = ep;
    return (cp);
}

/*
 * Match the last len characters of cp against name.
 * Returns 1 on success, 0 on failure.
 */
endmatch(name, len, cp)
    char *name, *cp;
    int len;
{
    char *ep;

    ep = cp;
    while (*ep++) /* Nothing */;
    ep -= (len + 1);
    if (ep < cp)
	return (0);

    while (*name++ == *ep)
	if (*ep++ == '\0')
	    return (1);

    return (0);
}

main(argc, argv)
    int argc;
    char *argv[];
{
    char line[1024], *name, *cp, *next;
    HashTable ht;
    HashSearch hs;
    HashEntry *he;
    int len;

    if (argc != 2)
    {
	printf("Usage: %s name\n", argv[0]);
	exit (1);
    }

    name = argv[1];
    len = strlen(name);
    HashInit(&ht, 32, 0);
    while (fgets(line, sizeof line, stdin))
    {
	for (next = line, cp = token(&next); cp; cp = token(&next))
	    if (endmatch(name, len, cp))
		(void) HashFind(&ht, cp);
    }

    HashStartSearch(&hs);
    while (he = HashNext(&ht, &hs))
	printf("%s\n", he->h_key.h_name);
}

char *
TxGetLine(line, len)
    char *line;
    int len;
{
    return (fgets(line, len, stdin));
}

TxError(s, a)
    char *s;
{
    vfprintf(stdout, s, &a);
    fflush(stdout);
}

MainExit(code)
    int code;
{
    exit(code);
}
