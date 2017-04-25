;-------------------------------------------------------------------------
;
;  Standard gates
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
;  $Id: gate.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------


;------------------------------------------------------------------------
; Draw c-element
;------------------------------------------------------------------------
(define gate.c
  (eval
   (list
    'lambda
    (cons 'widthp (cons 'widthn (cons 'output 'names)))
    '(begin
       (if (and (and (number? widthn) (number? widthp))
		(and (string? output) (string-list? names)))
	   #t
	   (error "Usage: gate.c <p-width> <n-width> \"out\" \"n1\" \"n2\" ...")
	   )
       (box.push (getbox))
       (define pstk (stack.p widthp (cons '("Vdd!") (append names (list (list output))))))
       (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
       (define nstk (stack.n widthn (cons '("GND!") (append names (list (list output))))))
       (box.pop)
       (box.hull pstk nstk)
       )
    )
   )
  )


;------------------------------------------------------------------------
; Draw folded c-element
;------------------------------------------------------------------------
(define gate.cf
  (eval
   (list
    'lambda
    (cons 'widthp (cons 'widthn (cons 'output 'names)))
    '(let ((plist (cons '("Vdd!") names))
	   (nlist (cons '("GND!") names)))
       (begin
	 (if (and (and (number? widthn) (number? widthp))
		  (and (string? output) (string-list? names)))
	     #t
	     (error "Usage: gate.cf <p-width> <n-width> \"out\" \"n1\" \"n2\" ...")
	     )
	 (box.push (getbox))
	 (define pstk
	   (stack.p 
	    widthp
	    (append (append plist (list (list output))) (reverse plist))
	    )
	   )
	 (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
	 (define nstk
	   (stack.n 
	    widthn
	    (append (append nlist (list (list output))) (reverse nlist))
	    )
	   )
	 (box.pop)
	 (box.hull pstk nstk)
	 )
       )
    )
   )
  )


;------------------------------------------------------------------------
; Draw inverter
;------------------------------------------------------------------------
(define gate.inv
  (lambda (widthp widthn output input)
    (begin
      (if (and (and (number? widthn) (number? widthp))
	       (and (string? output) (string? input)))
	  #t
	  (error "Usage: gate.inv <p-width> <n-width> \"out\" \"inp\"")
	  )
      (box.push (getbox))
      (define pstk (stack.p widthp (list (list "Vdd!") input (list output))))
      (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
      (define nstk (stack.n widthn (list (list "GND!") input (list output))))
      (box.pop)
      (box.hull pstk nstk)
      )
    )
  )

(define gate.nor ())
(define gate.nand ())

(letrec 
    (
     (interleave-power-output
      (lambda (name-list power output)	;even/odd is encoded by l/nl power
	(if (null? name-list) (list 
			       (if (string? power) (list power) (list output))
			       )
	    (cons (if (string? power)
		      (list power)
		      (list output)
		      )
		  (cons (car name-list) 
			(interleave-power-output 
			 (cdr name-list) 
			 (if (string? power) (list power) (car power)) output)
			)
		  )
	    )
	)
      )
     )
  (begin
;------------------------------------------------------------------------
; Draw a nor gate
;------------------------------------------------------------------------
    (set! 
     gate.nor
     (eval
      (list
       'lambda
       (cons 'widthp (cons 'widthn (cons 'output 'names)))
       '(begin
	  (if (and (and (number? widthn) (number? widthp))
		   (and (string? output) (string-list? names)))
	      #t
	      (error "Usage: gate.nor <p-width> <n-width> \"out\" \"n1\" \"n2\" ...")
	      )
	  (box.push (getbox))
	  (define pstk
	    (stack.p widthp (cons '("Vdd!") (append names (list (list output)))))
	    )
	  (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
	  (define nstk
	    (stack.n widthn (interleave-power-output names "GND!" output))
	    )
	  (box.pop)
	  (box.hull pstk nstk)
	  )
       )
      )
     )
;------------------------------------------------------------------------
; Draw a nand gate
;------------------------------------------------------------------------
    (set!
     gate.nand
     (eval
      (list
       'lambda
       (cons 'widthp (cons 'widthn (cons 'output 'names)))
       '(begin
	  (if (and (and (number? widthn) (number? widthp))
		   (and (string? output) (string-list? names)))
	      #t
	      (error "Usage: gate.nand <p-width> <n-width> \"out\" \"n1\" \"n2\" ...")
	      )
	  (box.push (getbox))
	  (define pstk
	    (stack.p widthp (interleave-power-output names "Vdd!" output))
	    )
	  (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
	  (define nstk
	    (stack.n widthn (cons '("GND!") (append names (list (list output)))))
	    )
	  (box.pop)
	  (box.hull pstk nstk)
	  )
       )
      )
     )
    )
  )
