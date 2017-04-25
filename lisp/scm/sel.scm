;-------------------------------------------------------------------------
;
;  (c) 1997 California Institute of Technology
;  Department of Computer Science
;  Pasadena, CA 91125.
;  All Rights Reserved
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
;  $Id: sel.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------


;------------------------------------------------------------------------
; Extract fields from label return value
;------------------------------------------------------------------------
(define label.ret-pos (lambda (l) (cddr l)))
(define label.ret-name (lambda (l) (car l)))
(define label.ret-layer (lambda (l) (cadr l)))


;------------------------------------------------------------------------
; Select all netlists from the labels under the current box which match
; the specified name.
;------------------------------------------------------------------------
(define sel.netlist
  (letrec (
	   (sel-netlist-helper 
	    (lambda (l)
	      (if (null? l) #t
		  (begin
		    (eval (cons 'box (label.ret-pos (car l))))
		    (repeat 6 (lambda () (select more box (label.ret-layer (car l)))))
		    (sel-netlist-helper (cdr l))
		    )
		  )
	      )
	    )
	   )
    (lambda (name)
      (begin
	(if (string? name) #t
	    (error "Usage: sel.netlist \"name\"")
	    )
	(box.push (getbox))
	(define x (getlabel name))
	(if (null? x) 
	    (echo "No netlist selected")
	    (sel-netlist-helper x)
	    )
	(box.pop)
	)
      )
    )
  )


;------------------------------------------------------------------------
;  Compare two selections
;------------------------------------------------------------------------
(define sel.stack ())
(define sel.push 
  (lambda ()
    (set! sel.stack (cons (getsellabel "*") sel.stack))
    )
  )

(define sel.pop
  (lambda ()
    (if (null? sel.stack)
	(echo "Selection stack is empty")
	(set! sel.stack (cdr sel.stack))
	)
    )
  )


(define sel.cmp
  (letrec ((equal-labs 
	    (lambda (a b)
	      (and 
	       (and
		(string=? (car a) (car b))
		(string=? (cadr a) (cadr b))
		)
	       (and 
		(=? (caddr a) (caddr b))
		(=? (cadddr a) (cadddr b))
		)
	       )
	      )
	    )
	   (search
	    (lambda (a b)
	      (cond ((null? b) #f)
		    ((equal-labs a (car b)) #t)
		    (#t (search a (cdr b)))
		    )
	      )
	    )
	   (helper
	    (lambda (a b c)
	      (cond ((null? a) c)
		    ((search (car a) b) (helper (cdr a) b c))
		    (#t (helper (cdr a) b (cons (car a) c)))
		    )
	      )
	    )
	   )
	  (lambda ()
	    (if (null? sel.stack)
		(echo "Selection stack is empty")
	      (begin
	       (define x (getsellabel "*"))
	       (label.set!
		(append (helper x (car sel.stack) (list))
			(helper (car sel.stack) x (list))
			)
		)
	       (set! x ())
	       (collect-garbage)
	       )
	      )
	    )
	  )
  )

(define duplabel
  (lambda ()
    (begin
      (box.push (getbox))
      (select clear)
      (define x (getpoint))
      (set! x (list (car x) (cadr x) (car x) (cadr x)))
      (repeat 6 (lambda () (select more)))
      (define y (getsellabel "*"))
      (apply box x)
      (select clear)
      (if (=? 0 (length y))
	  (begin (error "duplabel: no labels on netlist") (box.pop))
	  (label (caar y))
	  )
      (box.pop)
      )
    )
  )
