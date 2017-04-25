/* no comments are provided - the code is self explanatory :-) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STRLEN  1024
#define NSIZE  100000



typedef	struct {
	char		typ;
	char 		*g, *s, *d ;
	float		l, w;
	char 		*ga ;
	int		x, y, as, ps, ad, pd;
}	MOS ;

MOS	mosFets[NSIZE];
int	par = 0, ndx = 0;


char *strsave(s)
char *s;
{
	char *p ;

	p = (char *) malloc(strlen(s)+1);
	strcpy(p,s);
	return p;
}

int parseAttr(str, a, p)
char *str;
int *a, *p;
{
	int l;
	char *s;

	if ( (l=strlen(str)) <= 2 ) {
		*a = 0 ; *p = 0;
		return;
	}

	for ( s = str+l*sizeof(char) ; *s != 'A' && *s != 'a' && s != str ; s-- ) ;
	if ( sscanf(s,"A_%d,P_%d", a, p ) != 2)
	        if ( sscanf(s,"a_%d,p_%d", a, p ) != 2 )
	          if ( sscanf(s,"A_%d,p_%d", a, p ) != 2 )
	            if ( sscanf(s,"a_%d,P_%d", a, p ) != 2 )
			fprintf(stderr,"Weird attributes output will be incorect\n");
}

void addMos( typ, g, s, d, l, w, x, y, ga, sa, da)
char *typ, *g, *s, *d, *l, *w, *x, *y, *ga, *sa, *da;
{
	int i ;
	MOS *iptr;
	float ln, wn ;
	int as, ps, ad, pd;

	ln = (float) atof(l); wn = (float) atof(w);
	if ( ga == NULL ) 
		as = ps = ad = pd = 0 ;
	else {
		parseAttr(sa, &as, &ps);
		parseAttr(da, &ad, &pd);
	}
	for ( i = 0 ; i < ndx ; i ++ ) {
		iptr = mosFets + i ;
		if ( iptr->typ == *typ  && iptr->l == ln
			&& ! strcmp(iptr->g, g)
			&&  ( ! strcmp(iptr->s, s) && ! strcmp(iptr->d, d) )
			 ) {
				iptr->w += wn;
				iptr->as += as ; iptr->ps += ps ;
				iptr->ad += ad ; iptr->pd += pd ;
				par ++;
				return;
			}
		if ( iptr->typ == *typ  && iptr->l == ln
			&& ! strcmp(iptr->g, g)
			&& ( ! strcmp(iptr->s, d) && ! strcmp(iptr->d, s) )) {
				iptr->w += wn;
				iptr->as += ad ; iptr->ps += pd ;
				iptr->ad += as ; iptr->pd += ps ;
				par ++;
				return;
			}
	}

	iptr = mosFets + ndx ;
	iptr->typ = *typ;
	iptr->g = strsave(g); iptr->s = strsave(s); iptr->d = strsave(d);
	iptr->l = ln ; iptr->w = wn;
	if ( x != NULL ) {
		iptr->x = (int) atoi(x); 
		iptr->y = (int) atoi(y);
		iptr->as = as ; iptr->ps = ps ;
		iptr->ad = ad ; iptr->pd = pd ;
		if (ga ) iptr->ga = strsave(ga); else iptr->ga = NULL;
	}
	if ( ++ ndx >= NSIZE ) {
			fprintf(stderr, "Mos max cound %d exceeded\n", NSIZE);
			exit(1);
	}
}


main ()
{
	int i;
	char str[STRLEN];
	char *typ, *g, *s, *d, *l, *w, *x, *y, *ga, *sa, *da;


        while (fgets (str, STRLEN - 1, stdin)) {
		if ( *str == 'p' || *str == 'n' || 
		     *str == 'e' || *str == 'd' ) {
				typ = strtok(str, " ");
				g = strtok(NULL, " ");
				s = strtok(NULL, " ");
				d = strtok(NULL, " ");
				l = strtok(NULL, " ");
				w = strtok(NULL, " ");
				x = strtok(NULL, " ");
				y = strtok(NULL, " ");
				if ( y == NULL ) {
					ga = sa = da = NULL ;
				} else  {
					ga = strtok(NULL, " ");
					sa = strtok(NULL, " ");
					da = strtok(NULL, " ");
				}
				addMos(typ, g, s, d, l, w, x, y, ga, sa, da);
          	} 
		else	puts(str);
        }
	fprintf(stderr,"| %d parallel devices\n", par);
	for ( i = 0 ; i < ndx ; i ++ ) 
	  /*if ( mosFets[i].ga )*/
	if (1)
		printf("%c %s %s %s %g %g %d %d %s s=A_%d,P_%d d=A_%d,P_%d\n", 
			mosFets[i].typ, mosFets[i].g, mosFets[i].s,
			mosFets[i].d, mosFets[i].l, mosFets[i].w,
			mosFets[i].x, mosFets[i].y,mosFets[i].ga,
			mosFets[i].as,mosFets[i].ps,
			mosFets[i].ad,mosFets[i].pd);
	   else
		printf("%c %s %s %s %g %g %d %d\n", 
			mosFets[i].typ, mosFets[i].g, mosFets[i].s,
			mosFets[i].d, mosFets[i].l, mosFets[i].w,
			mosFets[i].x, mosFets[i].y);
        exit (0);
}

