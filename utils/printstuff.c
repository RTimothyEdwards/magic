#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header$";
#endif /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"

void
PrintTrans(t)
    Transform *t;
{
    printf("Translate: (%d, %d)\n", t->t_c, t->t_f);
    printf("%d\t%d\n", t->t_a, t->t_d);
    printf("%d\t%d\n", t->t_b, t->t_e);
}

void
PrintRect(r)
    Rect *r;
{
    printf("(%d,%d) :: (%d,%d)\n", r->r_xbot, r->r_ybot, r->r_xtop, r->r_ytop);
}
