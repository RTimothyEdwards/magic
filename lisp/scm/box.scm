;-------------------------------------------------------------------------
;
;  Save and restore box position and cursor movement.
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
;  $Id: box.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
; Requires: default.scm
;
;-------------------------------------------------------------------------

(define box.list ())

(define box.=?
  (lambda (b1 b2)
    (and (and (=? (car b1) (car b2)) (=? (cadr b1) (cadr b2)))
	 (and (=? (caddr b1) (caddr b2)) (=? (caddr b1) (caddr b2)))
	 )
    )
  )

(define box.push
  (lambda (pos)
    (set! box.list (cons pos box.list))
    )
  )

(define box.pop
  (lambda ()
    (if (null? box.list)
	(echo "Box list is empty")
	(let ((x (car box.list)))
	  (begin 
	    (set! box.list (cdr box.list))
	    (if (box.=? x (getbox)) #t (eval (cons 'box x)))
	    )
	  )
	)
    )
  )

;
; Magic's move command is buggy . . . -sigh-
;
(define box.move
  (lambda (dx dy)
    (let* ((x (getbox))
	   (nllx (+ dx (car x)))
	   (nlly (+ dy (cadr x)))
	   (nurx (+ dx (caddr x)))
	   (nury (+ dy (cadddr x))))
      (box nllx nlly nurx nury)
      )
    )
  )


;------------------------------------------------------------------------
; Convex "box"-hull of two boxes
;------------------------------------------------------------------------
(define box.hull 
  (lambda (b1 b2) 
    (list (min (car b1) (car b2))
	  (min (cadr b1) (cadr b2))
	  (max (caddr b1) (caddr b2))
	  (max (cadddr b1) (cadddr b2))
	  )
    )
  )
