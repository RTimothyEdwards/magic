;-------------------------------------------------------------------------
;
;  Labels
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
;  $Id: label.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
; Requires: box.scm
;
;-------------------------------------------------------------------------


;------------------------------------------------------------------------
;
; Create a labeller, horizontal or vertical
;
;------------------------------------------------------------------------
(define label.vert ())
(define label.horiz ())

(letrec ((labeller
	  (lambda (name)
	    (let ((x 0))
	      (lambda ()
		(begin (label (string-append name (number->string x)))
		       (set! x (+ x 1))
		       )
		)
	      )
	    )
	  ))
  (begin
    (set! label.vert
	  (lambda (name bit-pitch) 
	    (begin
	      (if (and (string? name) (number? bit-pitch))
		  #t
		  (error "Usage: label.vert \"name\" <bit-pitch>")
		  )
	      (let ((lbl (labeller name)))
		(lambda () (begin (lbl) (box.move 0 bit-pitch)))
		)
	      )
	    )
	  )
    (set! label.horiz
	  (lambda (name bit-pitch)
	    (begin
	      (if (and (string? name) (number? bit-pitch))
		  #t
		  (error "Usage: label.horiz \"name\" <bit-pitch>")
		  )
	      (let ((lbl (labeller name)))
		(lambda () (begin (lbl) (box.move bit-pitch 0)))
		)
	      )
	    )
	  )
    )
  )


;------------------------------------------------------------------------
; Place a label at (1,1) relative to the bottom left of the current
; box.
;------------------------------------------------------------------------
(define label.draw
  (lambda (name layer)
    (let* ((x (getbox)) (lx (+ 1 (car x))) (ly (+ 1 (cadr x))))
      (begin
	(if (and (string? name) (string? layer))
	    #t
	    (error "Usage: label.draw \"name\" \"layername\"")
	    )
	(box.push x)
	(eval (cons 'box '(lx ly lx ly)))
	(label name up layer)
	(box.pop)
	#t
	)
      )
    )
  )


;------------------------------------------------------------------------
; 
; Quote globbing characters
;
;------------------------------------------------------------------------
(define label.backslashify ())
(letrec (
	 ;
	 ; put the list of characters you want backslashified here
	 ;
	 (quote-list (list (string-ref "[" 0)
			   (string-ref "]" 0)
			   (string-ref "*" 0)))

	 ;
	 ; ah well . . . needs to be fixed, but can't without sacrificing
	 ; backward compatibility with old .src file; or by completely
	 ; changing command behavior when parsing .scm files. Ah well.
	 ;
	 (quoted-backslash (substring "\\" 0 1)) ; -sigh- whatever works

	 (string-to-list-helper
	  (lambda (str pos)
	    (if (=? pos (string-length str)) ()
		(cons (substring str pos (+ pos 1))
		      (string-to-list-helper str (+ 1 pos))
		      )
		)
	    )
	  )

	 ;
	 ;  Takes a string and cuts it into a list of strings each
	 ;  containing a single character
	 ;
	 (string-to-list (lambda (str) (string-to-list-helper str 0)))

	 ;
	 ;  Appends a list of strings into a single one
	 ;
	 (list-to-string 
	  (lambda (l)
	    (if (null? l) ""
		(string-append (car l) (list-to-string (cdr l)))
		)
	    )
	  )

	 ;
	 ;  Checks to see if x is a member of l
	 ;
	 (is-member? (lambda (x l)
		       (cond ((null? l) #f)
			     ((=? x (car l)) #t)
			     (#t (is-member? x (cdr l)))
			     )
		       )
		     )

	 ;
	 ;  Function that takes a one-character string and adds
	 ;  a backslash to it if it has to be backslashified.
	 ;
	 (convert
	  (lambda (str)
	    (if (is-member? (string-ref str 0) quote-list)
		(string-append quoted-backslash str) str)
	    )
	  )
	 )
;------------------------------------------------------------------------
;  After all those helper functions in the nested scope, this function
;  is just a one-liner . . . 
;------------------------------------------------------------------------
  (set! label.backslashify 
	(lambda (str) (list-to-string (mapcar convert (string-to-list str))))
	)
  )

;------------------------------------------------------------------------
; Rename all labels under the current box
;------------------------------------------------------------------------
(define label.rename ())
(define label.allren ())
(define label.swap ())

(letrec ((rename-helper
	  (lambda (poslist name)
	    (if (null? poslist)
		#t
		(begin (eval (cons 'box (cddar poslist)))
		       (erase label)
		       (label name up (cadar poslist))
		       (rename-helper (cdr poslist) name)
		       )
		)
	    )
	  ))
  (begin
    (set! label.rename
	  (lambda (name1 name2)
	    (begin
	      (if (and (string? name1) (string? name2))
		  #t
		  (error "Usage: label.rename \"name1\" \"name2\"")
		  )
	      (box.push (getbox))
	      (rename-helper (getlabel (label.backslashify name1)) name2)
	      (box.pop)
	      )
	    )
	  )
    (set! label.allren
	  (lambda (name1 name2)
	    (begin
	      (if (and (string? name1) (string? name2))
		  #t
		  (error "Usage: label.allren \"name1\" \"name2\"")
		  )
	      (box.push (getbox))
	      (rename-helper (getlabel name1) name2)
	      (box.pop)
	      )
	    )
          )
    (set! label.swap 
	  (lambda (name1 name2)
	    (begin
	      (if (and (string? name1) (string? name2))
		  #t
		  (error "Usage: label.swap \"name1\" \"name2\"")
		  )
	      (box.push (getbox))
	      (define x1 (getlabel (label.backslashify name1)))
	      (define x2 (getlabel (label.backslashify name2)))
	      (rename-helper x1 name2)
	      (rename-helper x2 name1)
	      (box.pop)
	      )
	    )
	  )
    )
  )


;------------------------------------------------------------------------
; Search for all labels matching a string under the current box.
;------------------------------------------------------------------------
(define label.search ())
(define label.find-next ())
(define label.set! ())
(define drc.search ())
(define drc.find-next ())

(let ((label-list ()) (drc-list ()))
  (begin
    (set! label.set!  (lambda (l) (set! label-list l)))
    (set! drc.search (lambda () (set! drc-list (mapcar (lambda (l) (cons "err" l)) (getpaint "err")))))
    (set! label.search 
	  (lambda (name) 
	    (begin
	      (if (string? name)
		  #t
		  (error "Usage: label.search \"name\"")
		  )
	      (set! label-list (getlabel name))
	      )
	    )
	  )
    (set! label.find-next
	  (lambda ()
	    (if (null? label-list) (echo "No more labels")
		(begin 
		  (eval (cons 'box (cddar label-list)))
		  (box w 2)
		  (box h 2)
		  (set! label-list (cdr label-list))
		  )
		)
	    )
	  )
    (set! drc.find-next
	  (lambda ()
	    (if (null? drc-list) (echo "No more labels")
		(begin 
		  (eval (cons 'box (cddar drc-list)))
		  (box w 2)
		  (box h 2)
		  (set! drc-list (cdr drc-list))
		  )
		)
	    )
	  )
    )
  )
