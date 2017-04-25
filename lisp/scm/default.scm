;-------------------------------------------------------------------------
;
;  Some standard scheme functions.
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
;  $Id: default.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------

; 
; Manipulating booleans
;
(define and (lambda (x y) (if x y #f)))
(define or (lambda (x y) (if x #t y)))
(define not (lambda (x) (if x #f #t)))

;
; MIT-scheme defines "sequence"
;
(define sequence begin)

;
; A looping construct
;
(define repeat				
  (lambda (N obj-with-side-effects) 
    (if (positive? N) 
	(begin (obj-with-side-effects) 
	       (repeat (- N 1) obj-with-side-effects))
	#t
	)
    )
  )


;
; More list stuff . . .
;
(define caar (lambda (x) (car (car x))))
(define cadr (lambda (x) (car (cdr x))))
(define cdar (lambda (x) (cdr (car x))))
(define cddr (lambda (x) (cdr (cdr x))))
(define caaar (lambda (x) (car (car (car x)))))
(define caadr (lambda (x) (car (car (cdr x)))))
(define cadar (lambda (x) (car (cdr (car x)))))
(define cdaar (lambda (x) (cdr (car (car x)))))
(define caddr (lambda (x) (car (cdr (cdr x)))))
(define cdadr (lambda (x) (cdr (car (cdr x)))))
(define cddar (lambda (x) (cdr (cdr (car x)))))
(define cdddr (lambda (x) (cdr (cdr (cdr x)))))
(define caaaar (lambda (x) (car (car (car (car x))))))
(define caaadr (lambda (x) (car (car (car (cdr x))))))
(define caadar (lambda (x) (car (car (cdr (car x))))))
(define cadaar (lambda (x) (car (cdr (car (car x))))))
(define caaddr (lambda (x) (car (car (cdr (cdr x))))))
(define cadadr (lambda (x) (car (cdr (car (cdr x))))))
(define caddar (lambda (x) (car (cdr (cdr (car x))))))
(define cadddr (lambda (x) (car (cdr (cdr (cdr x))))))
(define cdaaar (lambda (x) (cdr (car (car (car x))))))
(define cdaadr (lambda (x) (cdr (car (car (cdr x))))))
(define cdadar (lambda (x) (cdr (car (cdr (car x))))))
(define cddaar (lambda (x) (cdr (cdr (car (car x))))))
(define cdaddr (lambda (x) (cdr (car (cdr (cdr x))))))
(define cddadr (lambda (x) (cdr (cdr (car (cdr x))))))
(define cdddar (lambda (x) (cdr (cdr (cdr (car x))))))
(define cddddr (lambda (x) (cdr (cdr (cdr (cdr x))))))


(define append 
  (lambda (l1 l2)
    (if (null? l1) l2
	(cons (car l1) (append (cdr l1) l2))
	)
    )
  )

(define reverse
  (letrec ((reverse-helper
	    (lambda (name rest) 
	      (if (null? rest) name
		  (reverse-helper (cons (car rest) name) (cdr rest))
		  )
	      )
	    )
	   )
    (lambda (l)
      (reverse-helper () l)
      )
    )
  )



;
; Some arithmetic
;
(define =? (lambda (x y) (zero? (- x y))))
(define <? (lambda (x y) (negative? (- x y))))
(define >? (lambda (x y) (positive? (- x y))))
(define >=? (lambda (x y) (or (>? x y) (=? x y))))
(define <=? (lambda (x y) (or (<? x y) (=? x y))))

(define uminus (lambda (x) (- 0 x)))
(define max (lambda (x y) (if (>? x y) x y)))
(define min (lambda (x y) (if (<? x y) x y)))
(define abs (lambda (x) (if (negative? x) (uminus x) x)))



;
; String functions
;
(define string=? (lambda (x y) (zero? (string-compare x y))))
(define string<? (lambda (x y) (negative? (string-compare x y))))
(define string>? (lambda (x y) (positive? (string-compare x y))))

(define string-list? 
  (lambda (x)
    (cond ((null? x) #t)
	  ((string? (car x)) (string-list? (cdr x)))
	  (#t #f)
	  )
    )
  )


;
; Generally useful functions . . .
;
(define mapcar 
  (lambda (f l) (if (null? l) l (cons (f (car l)) (mapcar f (cdr l)))))
  )

;
; Debugging support
;
(define debug-object (lambda (x) (begin (display-object x) x)))

;
; Initial value of various constants.
;
(define scm-echo-result #f)		; disable echoing of results

(define scm-trace-magic #f)		; don't trace magic commands

(define scm-echo-parser-input #f)	; don't display parser input string

(define scm-echo-parser-output #f)	; don't display parser output

(define scm-stack-display-depth 0)	; default # of items displayed
                                        ; increase this when debugging scm code

(define scm-gc-frequency 5)		; collect garbage every so often


;
; Unix "system"
;
(define system (lambda (str) (wait (spawn "sh" "-c" str))))
