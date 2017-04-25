;-------------------------------------------------------------------------
;
;  Design-rule file
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
;  $Id: drc.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
; Requires: default.scm
;
;-------------------------------------------------------------------------


;------------------------------------------------------------------------
; Look for name in a name list
;------------------------------------------------------------------------
(define drc.find-name
  (lambda (name name-list)
    (cond ((null? name-list) (echo 
			      (string-append "Could not find: " name)
			      ))
	  ((string=? name (caar name-list)) (cadar name-list))
	  (#t (drc.find-name name (cdr name-list)))
	  )
    )
  )

;------------------------------------------------------------------------
;
; Spacing rules:
;
; Tight metal rules: m3: 3, m1,m2: 2
;
;------------------------------------------------------------------------

(define drc.min-spacing
    (let ((spacing 
	   (cond
	    ((string=? technology "scmos")
	     '(("poly" 2) 
	       ("m1" 3) 
	       ("m2" 3)
	       ("m3" 4)
	       ("contact-gate" 1)
	       ("contact-contact" 2)
	       ("pdiff-ndiff" 10)
	       ("pdiff-pdiff" 3)
	       ("ndiff-ndiff" 3)
	       ))
	    ((string=? technology "SCN5M_DEEP.12")
	     '(("poly" 4)
	       ("m1" 3) 
	       ("m2" 3)
	       ("m3" 4)
	       ("contact-gate" 1)
	       ("contact-contact" 2)
	       ("pdiff-ndiff" 12)
	       ("pdiff-pdiff" 3)
	       ("ndiff-ndiff" 3)
	       ))
	     (#t
	      '(("poly" 3)
		("m1" 3) 
		("m2" 3)
		("m3" 4)
		("contact-gate" 1)
		("contact-contact" 2)
		("pdiff-ndiff" 12)
		("pdiff-pdiff" 3)
		("ndiff-ndiff" 3)
		))
	     )
	   )
	  )
      (lambda (layer-name) (drc.find-name layer-name spacing))
      )
    )

;
; Width rules
;

(define drc.min-width
    (let ((width '(("poly" 2) 
		   ("m1" 3) 
		   ("m2" 3)
		   ("m3" 5)
		   ("contact" 4)
		   )
		 )
	  )
      (lambda (layer-name) (drc.find-name layer-name width))
      )
    )

;
; Overhang rules
;

(define drc.min-overhang
  (let ((overhang 
	 (cond
	  ((string=? technology "SCN5M_DEEP.12")
	   '(("gate-pdiff" 4)
	     ("pdiff-gate" 4)
	     ("gate-poly" 2)
	     ("poly-gate" 2)
	     ("gate-ndiff" 4) 
	     ("ndiff-gate" 4)
	     ))
	  (#t 
	   '(("gate-pdiff" 3)
	     ("pdiff-gate" 3)
	     ("gate-poly" 2)
	     ("poly-gate" 2)
	     ("gate-ndiff" 3) 
	     ("ndiff-gate" 3)
	     ))
	  )))
    (lambda (layer-name) (drc.find-name layer-name overhang))
    )
  )
