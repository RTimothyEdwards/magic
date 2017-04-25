;-------------------------------------------------------------------------
;
;  Transistor stacks
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
;  $Id: stack.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------


(define stack.p ())
(define stack.n ())
(define stack.tallp ())
(define stack.talln ())

(letrec
    (
     (flush-left-contacts 0)		; # of contacts that were placed
					; flush left in the middle of a
					; stack


;------------------------------------------------------------------------
; The height of the transistor stack, assuming that the poly wires were
; not jogged and that there were no contacts in the middle of the
; stack.
;------------------------------------------------------------------------
     (height-raw
      (lambda (num-transistors type)
	(+ (+ (* (- num-transistors 1)
		 (+ (drc.min-spacing "poly") (drc.min-width "poly"))
		 )
	      (* 2 (drc.min-overhang (string-append "gate-"
						    (string-append type "diff")
						    )
				     )
		 )
	      )
	   (drc.min-width "poly")
	   )
	)
      )

;------------------------------------------------------------------------
; The height of the transistor stack
;------------------------------------------------------------------------
     (height
      (lambda (num-transistors type)
	(+ (height-raw num-transistors type)
	   (* flush-left-contacts (- (+ (* 2 (drc.min-spacing "contact-gate"))
					(drc.min-width "contact")
					)
				     (drc.min-spacing "poly")
				     )
	      )
	   )
	)
      )

;------------------------------------------------------------------------
; Draw a poly wire. The wire jogs around contacts and is spaced so that
; there all the diffusion between the contact/poly/poly is squished out.
; Returns the new position of "dx"
;------------------------------------------------------------------------
     (draw-poly
      (lambda (name dx y fullwidth a0 a1 a2 a3)
	(let* ((cur (getbox))
	       (nx (+ (car cur) dx))
	       (cury (cadr cur))
	       )
	  (begin
	    (if (>? dx a2)
		(begin 
		  (box (car cur) cury (+ (car cur) fullwidth) (+ cury a3))
		  (paint "poly")
		  )
		(begin
		  (box (car cur) cury (+ nx a0) (+ cury a3))
		  (paint "poly")
		  (box (+ nx a1) (- cury y) (+ nx a0) cury)
		  (paint "poly")
		  (box (+ nx a1) (- cury y) 
		       (+ (car cur) fullwidth) (+ (- cury y) a3)
		       )
		  (paint "poly")
		  )
		)
            (eval (cons 'box cur))
	    (if (positive? (string-length name))
		(label.draw name "poly")
		()
		)
	    (box (car cur) (+ cury a0) (car cur) (+ cury a0))
	    (+ dx a0)
	    )
	  )
	)
      )

;------------------------------------------------------------------------
; Draw a contact. Placed in the "notch" of a poly wire if possible, or
; placed flush left. Returns the new "dx" value.
;------------------------------------------------------------------------
     (draw-contact
      (lambda (name dx y a0 a1 a2 a3 a4 a5  yinc newdx type lastcontact big)
	(let* ((cur (getbox))
	       (nx (+ (car cur) dx))
	       (cury (cadr cur))
	       )
	  (begin
	    (if (>? dx a0)
		(begin
		  (set! flush-left-contacts 
			(+ flush-left-contacts (if lastcontact 0 1))
			)
		  (box (+ (car cur) a4) (+ (- cury a5) a1)
		       (+ (+ (car cur) a4) a2) (+ (- cury a5) a3)
		       )
		  (paint type)
		  (if (positive? (string-length name))
		      (label.draw name type)
		      ()
		      )
		  (box (car cur) (+ cury y) (car cur) (+ cury y))
		  (if lastcontact big newdx)
		  )
		(begin 
		  (box (+ nx a1) (+ (- (- cury y) a5) a1) 
		       (+ nx a3) (+ (- (- cury y) a5) a3))
		  (paint type)
		  (if (positive? (string-length name))
		      (label.draw name type)
		      ()
		      )
		  (box (car cur) cury (car cur) cury)
		  (if lastcontact dx (+ dx y))
		  )
		)
	    )
	  )
	)
      )

;------------------------------------------------------------------------
; Actually draws the stacks. c-name is either "pdc" or "ndc", depending
; on the type of stack.
;------------------------------------------------------------------------
     (drawing-aux
      (let* (
	     (ca1 (drc.min-spacing "contact-gate"))
	     (ca2 (drc.min-width "contact"))
	     (ca3 (+ ca1 ca2))
	     (ca4 (drc.min-overhang "poly-gate"))
	     (ca5 (drc.min-spacing "poly"))
	     (ca6 (drc.min-width "poly"))
	     (dxp (drc.min-overhang "pdiff-gate"))
	     (dxn (drc.min-overhang "ndiff-gate"))
	     (y (max 0 (+ ca2 (- (* 2 ca1) ca5))))
	     )
	(lambda (nl gw dx c-name)
	  (if (null? nl) 
	      (list dx y)
	      (drawing-aux 
	       (cdr nl) gw
	       (if (list? (car nl)) 
		   (draw-contact (caar nl) dx y (- (- gw ca1) ca2)
				 ca1 ca2 ca3 ca4 ca5
				 (+ ca5 ca6)
				 (- (+ ca4 ca3) ca5)
				 c-name (null? (cdr nl)) (+ gw ca4)
				 )
		   (draw-poly (car nl) dx y (+ gw ca4)
			      (+ ca5 ca6) ca5 
			      (- (- gw (+ ca5 ca6)) 
				 (if (=? (string-ref c-name 0)
					 (string-ref "p" 0)
					 )
				     dxp
				     dxn))
			      ca6)
		   )
	       c-name
	       )
	      )
	  )
	)
      )

;------------------------------------------------------------------------
; Returns the number of gates in the stack.
;------------------------------------------------------------------------
     (count-gates
      (lambda (l)
	(if (null? l) 0
	    (+ (count-gates (cdr l)) (if (list? (car l)) 0 1))
	    )
	)
      )


;------------------------------------------------------------------------
; Draws the transistor stacks and paints the diffusion layers.
;------------------------------------------------------------------------
     (pn
      (lambda (name-list gate-width type)
	(let* ((dx-y
		(begin (box.push (getbox))
		       (set! flush-left-contacts 0)
		       (if (list? (car name-list))
			   (begin
			     (box.move 0 
				       (- (drc.min-overhang 
					   (string-append 
					    "gate-"
					    (string-append type "diff"))
					   )
					  (+ (drc.min-width "contact")
					     (drc.min-spacing "contact-gate")
					     )
					  )
				       )
			     (draw.layer (string-append type "dc")
					 (drc.min-width "contact")
					 (drc.min-width "contact")
					 )
			     (if (positive? (string-length (caar name-list)))
				 (label.draw (caar name-list) (string-append type "dc"))
				 ()
				 )
			     (box.move (uminus (drc.min-overhang "gate-poly"))
				       (+ (drc.min-width "contact")
					  (drc.min-spacing "contact-gate")
					  )
				       
				       )
			     (drawing-aux (cdr name-list) 
					  (+ gate-width 
					     (drc.min-overhang "gate-poly")
					     )
					  gate-width
					  (string-append type "dc")
					  )
			     )
			   (begin 
			     (box.move (uminus (drc.min-overhang "gate-poly"))
				       (drc.min-overhang "gate-pdiff")
				       )
			     (drawing-aux name-list
					  (+ gate-width 
					     (drc.min-overhang "gate-poly")
					     )
					  gate-width
					  (string-append type "dc")
					  )
			     )
			   )
		       )
		)
	       (dx (car dx-y))
	       (y  (cadr dx-y))
	       (strip (+ (- dx (drc.min-overhang "poly-gate"))
			 (drc.min-overhang (string-append type "diff-gate"))
			 ))
	       )
	  (begin
	    (box.pop)
	    (define ret-box
	      (draw.layer (string-append type "diff") (min strip gate-width)
			  (height (count-gates name-list) type))
	      )
	    (if (<? strip gate-width)
		(begin
		  (box.move strip 0)
		  (define ret-2
		    (draw.layer (string-append type "diff") 
				(- gate-width strip)
				(- (height (count-gates name-list) type)
				   y)
				)
		    )
		  (set! ret-box
			(list
			 (min (car ret-box) (car ret-2))
			 (min (cadr ret-box) (cadr ret-2))
			 (max (caddr ret-box) (caddr ret-2))
			 (max (cadddr ret-box) (cadddr ret-2))
			 )
			)
		  (box.move (uminus strip) 0)
		  )
		#t
		)
	    ret-box
	    )
	  )
	)
      )
     
;------------------------------------------------------------------------
; Counts all the internal contacts if passed the stack with the first
; contact missing
;------------------------------------------------------------------------
     (count-all-but-last-contacts
      (lambda (stack)
	(if (null? (cdr stack)) 0
	    (+ (if (list? (car stack)) 1 0) (count-all-but-last-contacts
					     (cdr stack))
	       )
	    )
	)
      )
      
;------------------------------------------------------------------------
; Draws straight poly
;------------------------------------------------------------------------
     (draw-unjogged-contact-poly 
      (let ((sz (drc.min-width "contact"))
	    (sz2 (drc.min-spacing "contact-gate"))
	    (sz3 (drc.min-spacing "poly"))
	    (sz4 (drc.min-overhang "gate-poly"))
	    (sz5 (drc.min-width "poly"))
	    )
	(lambda (name-list type w last)
	  (if (null? name-list) #t
	      (begin
		(if (list? (car name-list))
		    (begin
		      (box.move 0 sz2)
		      (draw.layer (string-append type "dc") sz sz)
		      (if (positive? (string-length (caar name-list)))
			  (label.draw (caar name-list) (string-append type "dc"))
			  #t
			  )
		      (box.move 0 sz)
		      (draw-unjogged-contact-poly (cdr name-list) type w #t)
		      )
		    (begin
		      (box.move (uminus sz4) (if last sz2 sz3))
		      (draw.layer "poly" w sz5)
		      (if (positive? (string-length (car name-list)))
			  (label.draw (car name-list) "poly")
			  #t
			  )
		      (box.move sz4 sz5)
		      (draw-unjogged-contact-poly (cdr name-list) type w #f)
		      )
		    )
		)
	      )
	  )
	)
      )

;------------------------------------------------------------------------
; Draw stack without squished diffusion
;------------------------------------------------------------------------
     (pntall
      (lambda (name-list gate-width type)
	(begin
	  (box.push (getbox))
	  (define x (- (count-gates name-list) 1))
	  (define y (count-all-but-last-contacts (cdr name-list)))
	  (define ht (+
		      (+
		       (* x (+ (drc.min-spacing "poly") (drc.min-width "poly"))
			  )
		       (drc.min-width "poly")
		       )
		      (+
		       (* 2 (drc.min-overhang (string-append type "diff-gate")))
		       (* y (+ (drc.min-width "contact")
			       (-
				(* 2 (drc.min-spacing "contact-gate"))
				(drc.min-spacing "poly")
				)
			       )
			  )
		       )
		      )
	    )
	  (define ret-box
	    (draw.layer (string-append type "diff") gate-width ht)
	    )
	  (if (list? (car name-list))
	      (begin
		(box.move 
		 0 
		 (- (drc.min-overhang (string-append type "diff-gate"))
		    (+ (drc.min-spacing "contact-gate") 
		       (drc.min-width "contact"))
		    )
		 )
		(draw.layer (string-append type "dc") 
			    (drc.min-width "contact") (drc.min-width "contact")
			    )
		(if (positive? (string-length (caar name-list)))
		    (label.draw (caar name-list) (string-append type "dc"))
		    #t
		    )
		(box.move 0 (max (drc.min-width "contact")
				 (- (drc.min-overhang 
				     (string-append type "diff-gate"))
				    (drc.min-spacing "contact-gate")
				    )
				 )
			  )
		(draw-unjogged-contact-poly 
		 (cdr name-list) 
		 type 
		 (+ (* 2 (drc.min-overhang "gate-poly")) gate-width)
		 #t
		 )
		)
	      (begin
		(box.move 
		 0
		 (-
		  (drc.min-overhang (string-append type "diff-gate"))
		  (drc.min-spacing "poly")
		  )
		 )
		(draw-unjogged-contact-poly 
		 name-list 
		 type
		 (+ (* 2 (drc.min-overhang "gate-poly")) gate-width)
		 #f
		 )
		)
	      )
	  (box.pop)
	  ret-box
	  )
	)
      )
     (is-a-stack? 
      (lambda (stk)
	(cond ((null? stk) #t)
	      ((string? (car stk)) (is-a-stack? (cdr stk)))
	      ((list? (car stk))
	       (if (string? (caar stk)) (is-a-stack? (cdr stk)) #f)
	       )
	      (#t #f)
	      )
	)
      )
     )
  (begin
;------------------------------------------------------------------------
; Draw p-transistor stack, jogging poly to remove diffusion
;------------------------------------------------------------------------
    (set! stack.p 
	  (lambda (gate-width p-list) 
	    (begin
	      (if (and (number? gate-width) (is-a-stack? p-list))
		  #t
		  (error "Usage: stack.p <num> stack-desc")
		  )
	      (pn p-list gate-width "p")
	      )
	    )
	  )
;------------------------------------------------------------------------
; Draw n-transistor stack, jogging poly to remove diffusion
;------------------------------------------------------------------------
    (set! stack.n 
	  (lambda (gate-width n-list) 
	    (begin
	      (if (and (number? gate-width) (is-a-stack? n-list))
		  #t
		  (error "Usage: stack.n <num> stack-desc")
		  )
	      (pn n-list gate-width "n")
	      )
	    )
	  )
;------------------------------------------------------------------------
; Draw p-transistor stack, straight poly
;------------------------------------------------------------------------
    (set! stack.tallp 
	  (lambda (gate-width p-list) 
	    (begin
	      (if (and (number? gate-width) (is-a-stack? p-list))
		  #t
		   (error "Usage: stack.tallp <num> stack-desc")
		   )
	      (pntall p-list gate-width "p")
	      )
	    )
	  )
;------------------------------------------------------------------------
; Draw n-transistor stack, straight poly
;------------------------------------------------------------------------
    (set! stack.talln 
	  (lambda (gate-width n-list) 
	    (begin
	      (if (and (number? gate-width) (is-a-stack? n-list))
		  #t
		  (error "Usage: stack.talln <num> stack-desc")
		  )
	      (pntall n-list gate-width "n")
	      )
	    )
	  )
    )
  )
