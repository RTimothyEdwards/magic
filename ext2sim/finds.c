#include "utils/magic.h"
#include "utils/hash.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>

char *
token(
    char **pnext)
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
int
endmatch(
    const char *name,
    int len,
    const char *cp)
{
    const char *ep;

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

int
main(
    int argc,
    char *argv[])
{
    char line[1024], *cp, *next;
    const char *name;
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
    return 0;
}

char *
TxGetLine(
    char *line,
    int len)
{
    return (fgets(line, len, stdin));
}

void
TxError(
    const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    vfprintf(stdout, s, ap);
    va_end(ap);
    fflush(stdout);
}

void
MainExit(
    int code)
{
    exit(code);
}
