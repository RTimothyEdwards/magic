;-------------------------------------------------------------------------
;
;  Top-level layout file
;
;  (c) 1996 California Institute of Technology
;  Department of Computer Science
;  Pasadena, CA 91125.
;
;  Permission to use, copy, modify, and distribute this software
;  and its documentation for any purpose and without fee is hereby
;  granted, provided that the above copyright notice appear in all
;  copies. The California Institute of Technology makes no representations
;  about the suitability of this software for any purpose. It is
;  provided "as is" without express or implied warranty. Export of this
;  software outside of the United States of America may require an
;  export license.
;
;  $Id: layout.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------

(echo "Loading modules:")
(echo -n "[help]")
(load-scm "help.scm")
(echo -n "[box]")
(load-scm "box.scm")
(echo -n "[drc]")
(collect-garbage)
(load-scm "drc.scm")
(echo -n "[label]")
(load-scm "label.scm")
(echo -n "[draw]")
(load-scm "draw.scm")
(collect-garbage)
(echo -n "[stack]")
(load-scm "stack.scm")
(echo -n "[prs]")
(load-scm "prs.scm")
(collect-garbage)
(echo -n "[gate]")
(load-scm "gate.scm")
(echo -n "[sel]")
(load-scm "sel.scm")
(echo "")
(collect-garbage)
(echo "Use scm-help ? for help on some scheme functions")
