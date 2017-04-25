#include <stdio.h>
#include <X/Xlib.h>


#ifndef lint
static char rcsid[]="$Header: /usr/cvsroot/magic-8.0/graphics/prcolors.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

main()
{
    Color color;
    int i;

    XOpenDisplay(NULL);
    for (i = 0; i < 256; i++)
    {
	color.pixel = i;
	if (XQueryColor(&color))
	    printf("#%x\t%5d%5d%5d\n", i,
		color.red / 256, color.green / 256, color.blue / 256);
    }
}
