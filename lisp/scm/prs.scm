;-------------------------------------------------------------------------
;
;  Drawing transistor stacks for production rules.
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
;  $Id: prs.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------


;------------------------------------------------------------------------------
;
; Production rule parser
; ~~~~~~~~~~~~~~~~~~~~~~
;
;  (prs.parse "production-rule-string")
;
;  Returns a parse tree for the production rule, or prints out an error
;  message.
;
;  A production rule is of the form:  expr -> rhs [+/-]
;  The resulting parse tree has the following format:
;
;     Top level:  (expr "rhs")
;          expr:  (and expr expr)
;                 (or expr expr)
;                 (not id)
;                 id
;
;  Observe that the production rules must be in negation-normal form.
;
;------------------------------------------------------------------------------
(define prs.parse ())

(letrec
    (
     (parse-string "")			; the current string being parsed
     (parse-position 0)			; current position in the string
     (parse-string-len 0)		; string length
     (or-char (string-ref "|" 0))	; constants
     (and-char (string-ref "&" 0))
     (not-char (string-ref "~" 0))
     (plus-char (string-ref "+" 0))
     (minus-char (string-ref "-" 0))
     (lparens-char (string-ref "(" 0))
     (rparens-char (string-ref ")" 0))
     
     (startid?				; #t if the character is a valid
					; start character for an identifier
      (let ((lc-a (string-ref "a" 0))
	    (uc-a (string-ref "A" 0))
	    (lc-z (string-ref "z" 0))
	    (uc-z (string-ref "Z" 0))
	    (us   (string-ref "_" 0))
	    )
	(lambda (x)
	  (cond
	   ((and (>=? x lc-a) (<=? x lc-z)) #t)
	   ((and (>=? x uc-a) (<=? x uc-z)) #t)
	   ((=? x us) #t)
	   (#t #f)
	   )
	  )
	)
      )

     (idchar?				; #t if the character is a valid
					; character in an identifier
      (let ((lc-a (string-ref "a" 0))
	    (uc-a (string-ref "A" 0))
	    (lc-z (string-ref "z" 0))
	    (uc-z (string-ref "Z" 0))
	    (lbrack (string-ref "[" 0))
	    (rbrack (string-ref "]" 0))
	    (us   (string-ref "_" 0))
	    (dot  (string-ref "." 0))
	    (bang (string-ref "!" 0))
	    (zero (string-ref "0" 0))
	    (nine (string-ref "9" 0))
	    )
	(lambda (x)
	  (cond
	   ((and (>=? x lc-a) (<=? x lc-z)) #t)
	   ((and (>=? x uc-a) (<=? x uc-z)) #t)
	   ((and (>=? x zero) (<=? x nine)) #t)
	   ((=? x us) #t)
	   ((=? x bang) #t)
	   ((=? x dot) #t)
	   ((=? x lbrack) #t)
	   ((=? x rbrack) #t)
	   (#t #f)
	   )
	  )
	)
      )

     (prs.error				; Print an error message out to the
					; screen, and abort evaluation
      (lambda (str)
	(begin
	  (echo
	   -n
	   (string-append 
	    "Error"
	    (if (zero? parse-position)
		","
		(string-append
		 ", parsed `"
		 (string-append (substring parse-string 0 parse-position) "',")
		 )
		)
	    )
	   )
	  (echo -n "looking at: ")
	  (echo
	   (string-append 
	    (string-append 
	     "`"
	     (substring parse-string parse-position parse-string-len)
	     )
	    "'"
	    )
	   )
	  (error str)
	  )
	)
      )

     (have?				; #t if the next character matches
					; its argument exactly. If so, the
					; position in the string is advanced
					; and skip trailing whitespace.
      (lambda (char)
	(if (=? (string-ref parse-string parse-position) char)
	    (begin
	      (set! parse-position (+ parse-position 1))
	      #t
	      )
	    #f
	    )
	)
      )

     (skipspace				; skip leading spaces
      (lambda ()
	(cond ((=? parse-position parse-string-len) #t)
	      ((=? (string-ref parse-string parse-position)
		   (string-ref " " 0))
	       (begin
		 (set! parse-position (+ parse-position 1))
		 (skipspace)
		 )
	       )
	      (#t #t)
	      )
	)
      )
     
     (skipchar				; Expect to see a specific character
					; and skip it. Report an error if
					; invalid character
      (lambda (char)
	(if (have? char) #t (prs.error (string-append 
					"Expected: "
					(string-set! " " 0 char))
				       )
	    )
	)
      )
     
     (skip				; Expect to see a string, and skip it
					; Reports an error otherwise.
					; len is the length of the string.
      (lambda (str len)
	(letrec
	    ((len2 (+ len parse-position))
	     (helper
	      (lambda (pos1 pos2)
		(cond
		 ((=? pos1 len) 
		  (begin
		    (set! parse-position pos2)
		    #t
		    ))
		 ((=? pos2 len2) 
		  (prs.error (string-append "Expected: " str)))
		 ((=? (string-ref parse-string pos2)
		      (string-ref str pos1))
		  (helper (+ pos1 1) (+ pos2 1))
		  )
		 (#t (prs.error (string-append "Expected: " str)))
		 )
		)
	      )
	     )
	  (helper 0 parse-position)
	  )
	)
      )

     (expr				; Parse an expression
      (lambda ()
	(begin
	  (define x (term))
	  (cond 
	   ((=? parse-position parse-string-len) x)
	   ((have? or-char) (begin (skipspace) (list 'or x (expr))))
	   (#t x)
	   )
	  )
	)
      )
     
     (term				; Parse a term
      (lambda ()
	(begin
	  (define x (factor))
	  (cond 
	   ((=? parse-position parse-string-len) x)
	   ((have? and-char) (begin (skipspace) (list 'and x (term))))
	   (#t x)
	   )
	  )
	)
      )

     (factor				; parse a factor
      (lambda ()
	(cond 
	 ((have? not-char) (begin (skipspace) (list 'not (variable))))
	 ((have? lparens-char)
	  (begin
	    (skipspace)
	    (define x (expr))
	    (skipchar rparens-char)
	    (skipspace)
	    x
	    )
	  )
	 (#t (variable))
	 )
	)
      )

     (variable				; parse a variable
      (lambda ()
	(letrec ((helper
		  (lambda (pos)
		    (cond
		     ((=? parse-string-len pos) pos)
		     ((idchar? (string-ref parse-string pos))
		      (helper (+ pos 1))
		      )
		     (#t pos)
		     )
		    )
		  ))
	  (begin
	    (if (=? parse-string-len parse-position)
		(prs.error "Expected an identifier")
		#t)
	    (if (startid? (string-ref parse-string parse-position))
		#t
		(prs.error "Expected an identifier")
		)
	    (define x (helper (+ parse-position 1)))
	    (define y (substring parse-string parse-position x))
	    (set! parse-position x)
	    (skipspace)
	    y
	    )
	  )
	)
      )
     )
  (set!
   prs.parse
   (lambda (str)
     (begin
       (set! parse-string str)
       (set! parse-position 0)
       (set! parse-string-len (string-length str))
       (skipspace)
       (define x (expr))
       (skipspace)
       (skip "->" 2)
       (skipspace)
       (define y (variable))
       (collect-garbage)
       (cond ((have? plus-char) (list x y))
	     ((have? minus-char) (list x y))
	     (#t (prs.error "Expected a `+' or `-'"))
	     )
       )
     )
   )
  )



;------------------------------------------------------------------------------
;
;  Drawing a production rule
;  ~~~~~~~~~~~~~~~~~~~~~~~~~
;
;  (prs.draw width "production-rule")
;
;  Draws the transistor stacks for the specified production rules,
;  with diffusion stacks "width" wide.
;
;
;  (prs.mgn widthp widthn "prs1" "prs2" . . .)
;
;  Draw transistor stacks for the production rules specified. The network
;  is generated assuming that the rules for all the pull-up networks are
;  pairwise mutually exclusive, and that the rules for all the pull-down
;  networks are pairwise mutually exclusive. This permits a degree of
;  gate-sharing. (Not currently implemented)
;
;------------------------------------------------------------------------------
;
; Network description:
;
;      ("node" ("gate" ref-to-node)) ("gate" ref-to-node) . . .)
;
;
; A stack:
;     ("node" "edge" "node" "edge" "node" "edge")
;
;
; Temporary description:
;    ("node" ref-to-a-stack ref-to-e1 ref-to-e2 . . .)
;
;                edge = (label ref-v1 ref-v2), or (label)
;
;

(define prs.net-add-edge ())
(define prs.net-find ())
(define prs.gen-stacks ())

(letrec
    (
     (stacks-so-far ())			; stacks that have been generated
					; by the algorithm so far

;------------------------------------------------------------------------
; Insert an empty list as the second element after the node for each    
; node in the network. This is used for folding loops back into the main
; transistor stack chain, if possible.
;------------------------------------------------------------------------
     (add-empty-path
      (lambda (net)
	(if (null? net) #t
	    (begin
	      (set-cdr! (car net) (cons () (cdar net)))
	      (add-empty-path (cdr net))
	      )
	    )
	)
      )

;------------------------------------------------------------------------
; Delete leading edges which have already been inspected by the stack
; generation algorithm. Inspected edges have their node references
; deleted, and so the list representing the edge has length 1.
;------------------------------------------------------------------------
     (strip-used-edges
      (lambda (noderef)
	(cond
	 ((null? (cddr noderef)) #t)
	 ((=? (length (caddr noderef)) 1)
	  (begin (set-cdr! (cdr noderef) (cdddr noderef))
		 (strip-used-edges noderef)
		 )
	  )
	 (#t #t)
	 )
	)
      )

;------------------------------------------------------------------------
; Generate one stack, eliminating edges used from the graph. The stack
; begins from the node pointed to by network.
;------------------------------------------------------------------------
     (generate-stack
      (lambda (network)
	(if (null? (cddr network)) (list network)
	    (cons network
		  (cons 
		   (car (caddr network))
		   (begin
		     (define edge (caddr network))
		     (define n1 (cadr edge))
		     (define n2 (caddr edge))
		     (set-cdr! (caddr network) ())
		     (set-cdr! (cdr network) (cdddr network))
		     (strip-used-edges n1)
		     (strip-used-edges n2)
		     (generate-stack (if (eqv? n1 network) n2 n1))
		     )
		   )
		  )
	    )
	)
      )


;------------------------------------------------------------------------
; Returns the last-but-1 cons cell in a stack, setting a node to a loop
; node if it has been used in a previously defined stack.
;------------------------------------------------------------------------
     (last-but-1-element
      (lambda (stk)
	(if (null? (cddr stk)) stk (last-but-1-element (cdr stk)))
	)
      )


;------------------------------------------------------------------------
; Generate all stacks. Iterate the stack generation phase until all edges
; have been inspected.
;------------------------------------------------------------------------
     (all-stacks
      (lambda (network)
	(cond
	 ((null? network) #t)
	 ((null? (cddar network)) (all-stacks (cdr network)))
	 (#t
	  (begin
	    (define stk (generate-stack (car network)))
	    (set! stacks-so-far (cons stk stacks-so-far))
	    (all-stacks network)
	    )
	  )
	 )
	)
      )

;------------------------------------------------------------------------
; Last member of a list
;------------------------------------------------------------------------
     (listlast
      (lambda (l)
	(if (null? (cdr l)) (car l) (listlast (cdr l)))
	)
      )
     

;------------------------------------------------------------------------
; Returns a list of all internal nodes in all stacks that need to be
; kept around. A node needs to be kept if there are two references to
; it.
;------------------------------------------------------------------------
     (all-used-contacts
      (lambda (stacks)
	(if (null? stacks) ()
	    (append (loose-ends (car stacks)) (allends (cdr stacks)))
	    )
	)
      )

;------------------------------------------------------------------------
; Return #t if string val is a member of list l.
;------------------------------------------------------------------------
     (ismember?
      (lambda (val l)
	(cond
	 ((null? l) #f)
	 ((string=? val (car l)) #t)
	 (#t (ismember? val (cdr l)))
	 )
	)
      )

;------------------------------------------------------------------------
; Strip internal nodes that are not from in list l from the transistor
; stack.
;------------------------------------------------------------------------
     (stripothers-1
	(lambda (stack)
	  (cond
	   ((null? stack) ())
	   ((list? (car stack))
	    (if (>? (cadar stack) 1)
		(cons (car stack) (stripothers-1 (cdr stack)))
		(stripothers-1 (cdr stack))
		)
	    )
	   (#t (cons (car stack) (stripothers-1 (cdr stack))))
	   )
	  )
	)

;------------------------------------------------------------------------
; Strip internal nodes that are not in list l from all the stacks.
;------------------------------------------------------------------------
     (stripothers
      (lambda (stacks)
	(if (null? stacks) ()
	    (cons 
	     (stripothers-1 (car stacks))
	     (stripothers (cdr stacks))
	     )
	    )
	)
      )

;------------------------------------------------------------------------
; #t if the character is a digit, #f otherwise.
;------------------------------------------------------------------------
     (digitchar?
      (let ((zero (string-ref "0" 0))
	    (nine (string-ref "9" 0))
	    )
	(lambda (x)
	  (and (>=? x zero) (<=? x nine))
	  )
	)
      )

;------------------------------------------------------------------------
; Returns #t if the string represents an internal node
;------------------------------------------------------------------------
     (internal-node? 
      (let ((x (string-ref "_" 0)))
	(lambda (str)
	  (if (=? (string-ref str 0) x)
	      (if (>? (string-length str) 1)
		  (if (digitchar? (string-ref str 1))
		      #t
		      #f
		      )
		  #f
		  )
	      #f
	      )
	  )
	)
      )

;------------------------------------------------------------------------
; Initialize a node's usecount
;------------------------------------------------------------------------
     (set-usecount-1
      (lambda (stack)
	(cond
	 ((null? stack) #t)
	 ((list? (car stack))
	  (begin
	    (set-car! (cdar stack)
		      (if (number? (cadar stack))
			  (+ 1 (cadar stack))
			  (if (internal-node? (caar stack)) 1 2)
			  )
		      )
	    (set-usecount-1 (cdr stack))
	    )
	  )
	 (#t (set-usecount-1 (cdr stack)))
	 )
	)
      )

     (set-usecount
      (lambda (stacks)
	(cond
	 ((null? stacks) #t)
	 ((null? (car stacks)) (set-usecount (cdr stacks)))
	 (#t (begin (set-usecount-1 (car stacks))
		    (set-usecount (cdr stacks)))
	     )
	 )
	)
      )

;------------------------------------------------------------------------
; Eliminate all internal nodes that are not required to construct the
; transistor stacks.
;------------------------------------------------------------------------
     (strip-dummy-contacts
      (lambda ()
	(begin
	  (set-usecount stacks-so-far)
	  (set! stacks-so-far (stripothers stacks-so-far))
	  )
	)
      )

;------------------------------------------------------------------------
; Returns #t if the stack is a loop stack.
;------------------------------------------------------------------------
     (isloop? 
      (lambda (stack)
	(eqv? (car stack) (cadr (last-but-1-element stack)))
	)
      )

;------------------------------------------------------------------------
; Separate loop and non-loop stacks.
;------------------------------------------------------------------------
     (split-stacks
      (lambda (stacks)
	(if (null? stacks) (list () () )
	    (let ((x (split-stacks (cdr stacks))))
	      (if (isloop? (car stacks))
		  (cons (cons (car stacks) (car x)) (cdr x))
		  (cons (car x) (list (cons (car stacks) (cadr x))))
		  )
	      )
	    )
	)
      )

;------------------------------------------------------------------------
; Add a path to an existing stack
;------------------------------------------------------------------------
     (addpath
      (lambda (head stk)
	(if (null? stk) #t
	    (begin
	      (if (list? (car stk))
		  (if (null? (cadar stk))
		      (set-car! (cdar stk) (list stk head))
		      #t
		      )
		  #t
		  )
	      (addpath head (cdr stk))
	      )
	    )
	)
      )

;------------------------------------------------------------------------
; See if there is a node on this path which belongs to an existing
; loop path
;------------------------------------------------------------------------
     (check-path
      (lambda (stack)
	(cond
	 ((null? stack) ())
	 ((list? (car stack))
	  (if (null? (cadar stack)) (check-path (cdr stack)) stack)
	  )
	 (#t (check-path (cdr stack)))
	 )
	)
      )

;------------------------------------------------------------------------
; Merge loop stacks
;------------------------------------------------------------------------
     (merge-loops 
      (lambda (stacks)
	(if (null? stacks) #t
	    (begin
	      (define lb1 (last-but-1-element (car stacks)))
	      (define cur (check-path (car stacks)))
	      (addpath stacks (car stacks))
	      (if (null? cur) (merge-loops (cdr stacks))
		  (begin
		    (define cell (caadar cur))
		    (define oldcdr (cdr cell))
		    (define head (car stacks))

		    (set-cdr! cell (cdr cur))
		    (set-cdr! lb1  head)
		    (set-cdr! cur oldcdr)

		    (set-car! stacks ())
		    (merge-loops (cdr stacks))
		    )
		  )
	      )
	    )
	)
      )

;------------------------------------------------------------------------
; Fix non-loops.
;------------------------------------------------------------------------
     (merge-nonloops
      (lambda (stacks)
	(if (null? stacks) #t
	    (begin
	      (define cur (check-path (car stacks)))
	      (if (null? cur) (merge-nonloops (cdr stacks))
		  (begin
		    (define cell (caadar cur))
		    (define oldcdr (cdr cur))
		    (define head (car (cdadar cur)))
		    (if (zero? (length (car head)))
			#t
			(begin
			  (define lb1 (last-but-1-element (car head)))
		    
			  (set-cdr! cur (cdr cell))
			  (set-cdr! lb1 (car head))
			  (set-cdr! cell oldcdr)
			  
			  (set-car! head ())
			  )
			)
		    (merge-nonloops (cdr stacks))
		    )
		  )
	      )
	    )
	)
      )

;------------------------------------------------------------------------
; Match first/last with first/last
;------------------------------------------------------------------------
     (find-stack-match-1
      (lambda (first last stack)
	(let ((x (car (reverse stack)))
	      (y (car stack)))
	  (cond
	   ((eqv? first y) 1)
	   ((eqv? first x) 2)
	   ((eqv? last  y) 3)
	   ((eqv? last  x) 4)
	   (#t ())
	   )
	  )
	)
      )

     (find-stack-match 
      (lambda (first last stacks)
	(cond ((null? stacks) ())
	      ((null? (car stacks)) (find-stack-match first last (cdr stacks)))
	      (#t (let ((x (find-stack-match-1 first last (car stacks))))
		    (if (null? x)
			(find-stack-match first last (cdr stacks))
			(list x stacks)
			)
		    )
		  )
	      )
	)
      )

;------------------------------------------------------------------------
; Fix straight lines that might now be linked because of the merge
; loops with non-loops phase.
;------------------------------------------------------------------------
     (fix-non-loops 
      (lambda (stacks)
	(if (null? stacks) #t
	    (if (null? (car stacks)) (fix-non-loops (cdr stacks))
		(begin
		  (define stk (find-stack-match
			       (caar stacks) 
			       (car (reverse (car stacks)))
			       (cdr stacks)
			       )
		    )
		  (if (null? stk) #t
		      (begin
			(define stks-new (cadr stk))
			(cond
			 ((=? (car stk) 1)
			  (begin
			    (define x (reverse (car stacks)))
			    (define y (last-but-1-element x))
			    (set-cdr! y (car stks-new))
			    (set-car! stks-new x)
			    )
			  )
			 ((=? (car stk) 2) 
			  (begin
			    (define x (car stks-new))
			    (define y (last-but-1-element x))
			    (set-cdr! y (car stacks))
			    )
			  )
			 ((=? (car stk) 3)
			  (begin
			    (define x (car stacks))
			    (define y (last-but-1-element x))
			    (set-cdr! y (car stks-new))
			    (set-car! stks-new x)
			    )
			  )
			 (#t 
			  (begin
			    (define x (reverse (car stacks)))
			    (define y (last-but-1-element (car stks-new)))
			    (set-cdr! y (car x))
			    )
			  )
			 )
			(set-car! stacks ())
			)
		      )
		  (fix-non-loops (cdr stacks))
		  )
		)
	    )
	)
      )

;------------------------------------------------------------------------
; Fix loops. Fold any loops into existing stacks, if possible.
;------------------------------------------------------------------------
     (fix-loops
      (lambda ()
	(begin
	  (define both (split-stacks stacks-so-far))
	  (define loop (car both))
	  (define non-loop (cadr both))
	  (merge-loops loop)
	  (merge-nonloops non-loop)
	  (set! stacks-so-far (append loop non-loop))
	  (fix-non-loops stacks-so-far)
	  )
	)
      )


;------------------------------------------------------------------------
; Convert network node references into node names in a transistor stack.
; Given a node reference in a stack (in which case it would be a contact,
; which is represented by a ("name")---see stack.scm), the name is the
; first member of the node list.
;------------------------------------------------------------------------
     (refs-to-names
      (lambda (stk)
	(if (null? stk) ()
	    (let ((x (if (list? (car stk)) (list (caar stk)) (car stk))))
	      (cons x (refs-to-names (cdr stk))))
	    )
	)
      )

;------------------------------------------------------------------------
; Convert all network node references into node names.
;------------------------------------------------------------------------
     (cleanup-stacks
      (lambda (stacks)
	(cond
	 ((null? stacks) ())
	 ((null? (car stacks)) (cleanup-stacks (cdr stacks)))
	 (#t (cons (refs-to-names (car stacks)) (cleanup-stacks (cdr stacks))))
	 )
	)
      )

;------------------------------------------------------------------------
; A contact is global if it ends in a !
;------------------------------------------------------------------------
     (global-node?
      (let ((bang (string-ref "!" 0)))
	(lambda (str)
	  (=? bang (string-ref str (- (string-length str) 1)))
	  )
	)
      )

;------------------------------------------------------------------------
; Locate a global variable contact if possible
;------------------------------------------------------------------------
     (locate-global-contact
      (lambda (stack)
	(if (null? stack) ()
	    (if (list? (car stack))
		(if (global-node? (caar stack)) stack
		    (locate-global-contact (cdr stack))
		    )
		(locate-global-contact (cdr stack))
		)
	    )
	)
      )

;------------------------------------------------------------------------
; Locate any contact
;------------------------------------------------------------------------
     (locate-any-contact
      (lambda (stack)
	(if (null? stack) ()
	    (if (list? (car stack))
		(if (internal-node? (caar stack))
		    (locate-any-contact (cdr stack))
		    stack
		    )
		(locate-any-contact (cdr stack))
		)
	    )
	)
      )

;------------------------------------------------------------------------
; Locate a contact that is not an internal node
;------------------------------------------------------------------------
     (user-contact
      (lambda (stack)
	(begin
	  (define x (locate-global-contact stack))
	  (if (null? x) (locate-any-contact stack) x)
	  )
	)
      )

;------------------------------------------------------------------------
; Rotate a single stack if possible so that the end-point is not an
; internal node
;------------------------------------------------------------------------
     (loop-unravel
      (lambda (stack)
	(begin
	  (define x (user-contact stack))
	  (if (null? x) stack
	      (begin
		(define hd (list (car x)))
		(define y (last-but-1-element stack))
		(set-cdr! hd (cdr x))
		(set-cdr! x ())
		(set-cdr! y stack)
		hd
		)
	      )
	  )
	)
      )

;------------------------------------------------------------------------
; If one of the final stacks is a loop stack, then you should try to make
; sure that the end-points are not internal nodes, and are preferably
; global nodes.
;------------------------------------------------------------------------
     (rotate-loops 
      (lambda (stacks)
	(if (null? stacks) #t
	    (cond 
	     ((null? (car stacks)) (rotate-loops (cdr stacks)))
	     ((isloop? (car stacks))
	      (begin
		(cond
		 ((internal-node? (caaar stacks))
		  (set-car! stacks (loop-unravel (car stacks)))
		  )
		 ((not (global-node? (caaar stacks)))
		  (set-car! stacks (loop-unravel (car stacks)))
		  )
		 (#t #t)
		 )
		(rotate-loops (cdr stacks))
		)
	      )
	     (#t (rotate-loops (cdr stacks)))
	     )
	    )
	)
      )
     )

  (begin

;------------------------------------------------------------------------
; Exported function: generate transistor stacks from a network
; description.
;------------------------------------------------------------------------
    (set!
     prs.gen-stacks
     (lambda (network)
       (begin
	 (set! stacks-so-far ())	; clear stacks
	 (add-empty-path network)	; add empty path
	 (all-stacks network)		; generate all stacks
	 (fix-loops)			; associate nodes with stacks
	 (rotate-loops stacks-so-far)	; rotate loops if possible so that
					; the stack ends are existing nodes
	 (strip-dummy-contacts)		; eliminate dummy nodes
	 (set! stacks-so-far (cleanup-stacks stacks-so-far))
	 stacks-so-far			; return
	 )
       )
     )

;------------------------------------------------------------------------
; find a node in a network.
;------------------------------------------------------------------------
     (set!
      prs.net-find
      (lambda (net node)
	(cond
	 ((null? net) ())
	 ((string=? (caar net) node) (car net))
	 (#t (prs.net-find (cdr net) node))
	 )
	)
      )

;------------------------------------------------------------------------
; Add an edge to a network. Use this function to construct the network
; graph.
;------------------------------------------------------------------------
    (set!
     prs.net-add-edge
     (lambda (network n1 g n2)
       (begin
	 (define ref-n1 (prs.net-find network n1))  ; find node 1
	 (define ref-n2 (prs.net-find network n2))  ; find node 2
	 (define edge (list g ref-n1 ref-n2))       ; create edge
	 (set-cdr! ref-n1 (cons edge (cdr ref-n1))) ; add edge to node 1
	 (set-cdr! ref-n2 (cons edge (cdr ref-n2))) ; add edge to node 2
	 )
       )
     )
    )
  )


;------------------------------------------------------------------------
(define prs.mgn ())
(define prs.mgn-node ())
(define prs.mgn-internal-node ())
(define prs.mgn-init-p-net ())
(define prs.mgn-init-n-net ())
(define prs.mgn-edge ())
(define prs.mgn-draw-p ())
(define prs.mgn-draw-n ())
(define prs.mgn-draw-tallp ())
(define prs.mgn-draw-talln ())
(define prs.draw ())
(define prs.tallmgn ())
(define prs.talldraw ())
(define prs.draw-net ())

(letrec
    (
     (gate.network ())
     (nodenumber 0)

;------------------------------------------------------------------------
; Generates a fresh internal node name
;------------------------------------------------------------------------
     (fresh-internal-node!
      (lambda ()
	(begin 
	  (define nn 
	    (string-append (string-append "_" (number->string nodenumber)) "#")
	    )
	  (set! nodenumber (+ 1 nodenumber))
	  nn
	  )
	)
      )

;------------------------------------------------------------------------
; Checks if "char" is the last non-whitespace character in "str"
;------------------------------------------------------------------------
     (ischarend?
      (lambda (str char)
	(letrec ((len (string-length str))
		 (space (string-ref " " 0))
		 (helper
		  (lambda (pos)
		    (cond 
		     ((zero? pos) #f)
		     ((=? char (string-ref str pos)) #t)
		     ((=? space (string-ref str pos)) (helper (- pos 1)))
		     (#t #f)
		     )
		    )
		  ))
	  (helper (- len 1))
	  )
	)
      )

;------------------------------------------------------------------------
; Extracts production rules ending with the character specified by the
; first character in string "last". The production rules are specified
; by a list of strings.
;------------------------------------------------------------------------
     (getprs
      (lambda (rule-list last)
	(cond ((null? rule-list) ())
	      ((ischarend? (car rule-list) (string-ref last 0))
	       (cons (car rule-list) (getprs (cdr rule-list) last)))
	      (#t (getprs (cdr rule-list) last))
	      )
	)
      )

;------------------------------------------------------------------------
;
; Simple transistor network generation
; ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
;
; Direct generation of a simple network for a production rule is done by
; the intuitive rules for drawing transistors for a pull-up/pull-down:
;
; Given two gate networks *-n1-* and *-n2-*, their and-combination is
; given by *-n1-+-n2-*, and their or-combination is given by
;              *-n1-*
;              `-n2-'
; In the first case, we need to generate a name for the intermediate
; node name in the network graph.
;
; A partial network *-n-* is represented by a list of potential edges.
; An edge (transistor) is a list (x "label" y), where x and y represent 
; the end-points. If an end-point is not connected, it is a number (0).
; Connected end-points are strings.
;
; The or-combination of two networks is simply the union of the two
; networks, and the and-combination is generated by generating a new
; name for the intermediate node, connecting all right end-points of
; network n1 to that node, and connecting all left end-points of network
; n2 to the same node. The final network is the union of the two networks.
;
; The left and right end-points for the entire network corresponding to
; a pull-up/pull-down are connected to the power supply and output
; respectively.
;
;------------------------------------------------------------------------

;------------------------------------------------------------------------
; Connect the right hanging nodes to str deleting any edges that were
; generated as a result, and return the new edge list.
;------------------------------------------------------------------------
     (fillright 
      (lambda (l str)
	(cond
	 ((null? l) ())
	 ((number? (caddar l))
	  (begin
	    (set-car! (cddar l) str)
	    (if (number? (caar l))
		(cons (car l) (fillright (cdr l) str))
		(begin
		  (prs.net-add-edge gate.network (caar l) (cadar l) (caddar l))
		  (fillright (cdr l) str)
		  )
		)
	    )
	  )
	 (#t (cons (car l) (fillright (cdr l) str)))
	 )
	)
      )
     
;------------------------------------------------------------------------
; Connect the left hanging edges to node str deleting any edges that
; were generated as a result, and return the new edge list.
;------------------------------------------------------------------------
     (fillleft
      (lambda (l str)
	(cond
	 ((null? l) ())
	 ((number? (caar l))
	  (begin
	    (set-car! (car l) str)
	    (if (number? (caddar l))
		(cons (car l) (fillleft (cdr l) str))
		(begin
		  (prs.net-add-edge gate.network (caar l) (cadar l) (caddar l))
		  (fillleft (cdr l) str)
		  )
		)
	    ))
	 (#t (cons (car l) (fillleft (cdr l) str)))
	 )
	)
      )


;------------------------------------------------------------------------
; Generate the network for a boolean expression. "tree" is the parse tree
; for the expression, and "type" is zero for a pull-down chain and one
; for a pull-up chain.
;------------------------------------------------------------------------
     (gen-1-network
      (lambda (tree type)
	(cond
	 ((string? tree)
	  (if (zero? type)
	      (list (list 0 tree 0))
	      (error "A pull-up must use inverted variables only")
	      )
	  )
	 ((eqv? 'not (car tree))
	  (if (zero? type)
	      (error "A pull-down must use uninverted variables only")
	      (list (list 0 (cadr tree) 0))
	      )
	  )
	 (#t
	  (begin
	    (define l (gen-1-network (cadr tree) type))
	    (define r (gen-1-network (caddr tree) type))
	    (if (eqv? 'and (car tree))
		(begin
		  (define nn (fresh-internal-node!))
		  (set! gate.network (cons (list nn) gate.network))
		  (set! l (fillright l nn))
		  (set! r (fillleft r nn))
		  )
		#t
		)
	    (append l r)
	    )
	  )
	 (#t (error "This should not happen!"))
	 )
	)
      )

;------------------------------------------------------------------------
; Generate network corresponding to a production rule.
;------------------------------------------------------------------------
     (gen-one-network
      (lambda (rule type)
	(begin
	  (define prs (prs.parse rule))
	  (define l (gen-1-network (car prs) type))
	  (if (null? (prs.net-find gate.network (cadr prs)))
	      (set! gate.network (cons (list (cadr prs)) gate.network))
	      #t
	      )
	  (fillleft l (if (zero? type) "GND!" "Vdd!"))
	  (fillright l (cadr prs))
	  )
	)
      )

;------------------------------------------------------------------------
; Generate a network corresponding to all the rules. The rules must all be
; either describing pull-ups or pull-downs.
;------------------------------------------------------------------------
     (gen-network
      (lambda (rules type)
	(if (null? rules) #t
	    (begin
	      (gen-one-network (car rules) type)
	      (gen-network (cdr rules) type)
	      )
	    )
	)
      )


;------------------------------------------------------------------------
; Draw all the stacks in "stacks" with width "width" using function
; "draw", spaced horizontally by "spacing".
;------------------------------------------------------------------------
     (drawstacks
      (lambda (draw width stacks spacing)
	(if (null? stacks)
	    (begin
	      (box.move (uminus spacing) 0)
	      ()
	      )
	    (begin
	      (define ret-box (draw width (car stacks)))
	      (box.move spacing 0)
	      (define ret2-box (drawstacks draw width (cdr stacks) spacing))
	      (if (null? ret2-box)
		  ret-box
		  (list (min (car ret-box) (car ret2-box))
			(min (cadr ret-box) (cadr ret2-box))
			(max (caddr ret-box) (caddr ret2-box))
			(max (cadddr ret-box) (cadddr ret2-box))
			)
		  )
	      )
	    )
	)
      )

;------------------------------------------------------------------------
; Create and draw all the stacks for a set of rules.
;------------------------------------------------------------------------
     (genstacks
      (lambda (draw width rules type supply)
	(begin
	  (echo -n "Generating network...")
	  (set! gate.network (list (list supply)))
	  (gen-network rules type)
	  (echo -n "generating stacks...")
	  (define stacks (prs.gen-stacks gate.network))
	  (echo "done.")
	  (drawstacks draw width stacks
		      (+ width
			 (max 
			  (drc.min-spacing
			   (if (zero? type) "ndiff-ndiff" "pdiff-pdiff"))
			  (+ (drc.min-spacing "poly")
			     (* 2 (drc.min-overhang "gate-poly"))
			     )
			  )
			 )
		      )
	  )
	)
      )
     )
  (begin
    (set!
     prs.mgn-internal-node
     (lambda ()
       (begin
	 (define nn (fresh-internal-node!))
	 (set! gate.network (cons (list nn) gate.network))
	 nn
	 )
       )
     )
    (set!
     prs.mgn-node
     (lambda (name)
       (begin
	 (if (string? name) #t
	     (error "Usage: prs.mgn-node \"name\"")
	     )
	 (set! gate.network (cons (list name) gate.network))
	 name
	 )
       )
     )
    (set!
     prs.mgn-init-p-net
     (lambda ()
       (set! gate.network (list (list "Vdd!")))
       )
     )
    (set!
     prs.mgn-init-n-net
     (lambda ()
       (set! gate.network (list (list "GND!")))
       )
     )
    (set!
     prs.mgn-edge
     (lambda (n1 lab n2)
       (if (string-list? (list n1 lab n2))
	   (prs.net-add-edge gate.network n1 lab n2)
	   (error "Usage: prs.mgn-edge node1 \"gate\" node2")
	   )
       )
     )
    (set!
     prs.mgn-draw-p
     (lambda (width)
       (begin
	 (if (number? width)
	     #t
	     (error "Usage: prs.mgn-draw-p <width>")
	     )
         (box.push (getbox))
	 (echo -n "generating stacks...")
	 (define stacks (prs.gen-stacks gate.network))
	 (echo "done.")
         (define d
	 (drawstacks stack.p width stacks
		     (+ width
			(max 
			 (drc.min-spacing "pdiff-pdiff")
			 (+ (drc.min-spacing "poly")
			    (* 2 (drc.min-overhang "gate-poly"))
			    )
			 )
			)
		     ))
         (box.pop)
         (collect-garbage)
         d
	 )
       )
     )
    (set!
     prs.mgn-draw-n
     (lambda (width)
       (begin
	 (if (number? width) #t
	     (error "Usage: prs.mgn-draw-n <width>")
	     )
         (box.push (getbox))
	 (echo -n "generating stacks...")
	 (define stacks (prs.gen-stacks gate.network))
	 (echo "done.")
	 (define d 
         (drawstacks stack.n width stacks
		     (+ width
			(max 
			 (drc.min-spacing "ndiff-ndiff")
			 (+ (drc.min-spacing "poly")
			    (* 2 (drc.min-overhang "gate-poly"))
			    )
			 )
			)
		     ))
         (box.pop)
         (collect-garbage)
         d
	 )
       )
     )
    (set!
     prs.mgn-draw-tallp
     (lambda (width)
       (begin
	 (if (number? width) #t
	     (error "Usage: prs.mgn-draw-tallp <width>")
	     )
         (box.push (getbox))
	 (echo -n "generating stacks...")
	 (define stacks (prs.gen-stacks gate.network))
	 (echo "done.")
         (define d
	 (drawstacks stack.tallp width stacks
		     (+ width
			(max 
			 (drc.min-spacing "pdiff-pdiff")
			 (+ (drc.min-spacing "poly")
			    (* 2 (drc.min-overhang "gate-poly"))
			    )
			 )
			)
		     ))
         (box.pop)
         (collect-garbage)
         d
	 )
       )
     )
    (set!
     prs.mgn-draw-talln
     (lambda (width)
       (begin
	 (if (number? width) #t
	     (error "Usage: prs.mgn-draw-talln <width>")
	     )
         (box.push (getbox))
	 (echo -n "generating stacks...")
	 (define stacks (prs.gen-stacks gate.network))
	 (echo "done.")
         (define d
	 (drawstacks stack.talln width stacks
		     (+ width
			(max 
			 (drc.min-spacing "ndiff-ndiff")
			 (+ (drc.min-spacing "poly")
			    (* 2 (drc.min-overhang "gate-poly"))
			    )
			 )
			)
		     ))
         (box.pop)
         (collect-garbage)
         d
	 )
       )
     )
    (set!
     prs.mgn
     (eval (list
	    'lambda 
	    (cons 'widthp (cons 'widthn 'rule-list))
	    '(let* ((p-rules (getprs rule-list "+"))
		    (n-rules (getprs rule-list "-"))
		    )
	       (begin
		 (if (and (and (number? widthp) (number? widthn))
			  (string-list? rule-list))
		     #t
		     (error "Usage: prs.mgn <p-width> <n-width> \"prs1\" ...")
		     )
		 (box.push (getbox))
		 (define r1
		   (genstacks stack.p widthp p-rules 1 "Vdd!")
		   )
		 (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
		 (define r2
		   (genstacks stack.n widthn n-rules 0 "GND!")
		   )
		 (box.pop)
                 (collect-garbage)
		 (list r1 r2)
		 )
	       )
	    )
	   )
     )
    (set!
     prs.draw
      (lambda (width rule)
	(let 
	    ((x (list rule)))
	  (begin
	    (if (and (number? width) (string? rule)) #t
		(error "Usage: prs.draw <width> \"prs\"")
		)
	    (if (ischarend? rule (string-ref "+" 0))
		(genstacks stack.p width x 1 "Vdd!")
		(genstacks stack.n width x 0 "GND!")
		)
	    )
	  )
	)
      )
    (set! 
     prs.draw-net
     (lambda (rule)
       (begin
	 (if (string? rule) #t
	     (error "Usage: prs.draw-net \"prs\"")
	     )
	 (if (ischarend? rule (string-ref "+" 0))
	     (gen-one-network rule 1)
	     (gen-one-network rule 0)
	     )
	 )
       )
     )
    (set!
     prs.tallmgn
     (eval (list
	    'lambda 
	    (cons 'widthp (cons 'widthn 'rule-list))
	    '(let* ((p-rules (getprs rule-list "+"))
		    (n-rules (getprs rule-list "-"))
		    )
	       (begin
		 (if (and (and (number? widthp) (number? widthn))
			  (string-list? rule-list))
		     #t
		     (error "Usage: prs.tallmgn <p-width> <n-width> \"prs1\" ...")
		     )
		 (box.push (getbox))
		 (define r1
		   (genstacks stack.tallp widthp p-rules 1 "Vdd!")
		   )
		 (box.move (+ widthp (drc.min-spacing "pdiff-ndiff")) 0)
		 (define r2
		   (genstacks stack.talln widthn n-rules 0 "GND!")
		   )
		 (box.pop)
                 (collect-garbage)
		 (list r1 r2)
		 )
	       )
	    )
	   )
     )
    (set!
     prs.talldraw
      (lambda (width rule)
	(let 
	    ((x (list rule)))
	  (begin
	    (if (and (number? width) (string? rule)) #t
		(error "Usage: prs.talldraw <width> \"prs\"")
		)
	    (if (ischarend? rule (string-ref "+" 0))
		(genstacks stack.tallp width x 1 "Vdd!")
		(genstacks stack.talln width x 0 "GND!")
		)
	    )
	  )
	)
      )
    )
  )


(define prs.mgn-fresh-node
  (let ((x 0))
    (lambda ()
      (begin 
	(define name (string-append 
		      (string-append "_i" (number->string x))
		      "#"
		      )
	  ) 
	(set! x (+ x 1)) 
	(prs.mgn-node name)
	)
      )
    )
  )

(define prs.mgn-output-edge 
  (lambda (a b c)
    (prs.mgn-edge a b c)
    )
  )

