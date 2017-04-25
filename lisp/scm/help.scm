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
;  $Id: help.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
;-------------------------------------------------------------------------


;------------------------------------------------------------------------
;
;  Help for scheme commands
;
;------------------------------------------------------------------------

(define scm-help ())
(define scm-add-help  ())

     (letrec (
	      (scm-help-list ())
	      (module-name (lambda (x) (car x)))
	      (module-long-form (lambda (x) (cadr x)))
	      (module-fns  (lambda (x) (caddr x)))
	      (dump-module-list 
	       (lambda (l)
		(if (null? l) #t
		 (begin
		  (echo 
		   (string-append
		    (string-append (module-name (car l)) " :: ")
		    (module-long-form (car l))
		    )
		   )
		  (dump-module-list (cdr l))
		  )
		 )
		)
	       )
	      (dump-help
	       (lambda (l name)
		(cond ((null? l) (echo "Module not found"))
		 ((string=? (module-name (car l)) name)
		  (begin
		   (echo)
		   (echo (module-fns (car l))))
		  )
		 (#t (dump-help (cdr l) name))
		 )
		)
	       )
	      )
      (begin
       (set! scm-help 
	(lambda (name)
	 (cond ((and (procedure? name)
		 (eqv? name ?))
		(begin
		 (echo "Available help:") 
		 (echo)
		 (dump-module-list scm-help-list)
		 (collect-garbage)
		 )
		)
	  ((not (string? name))
	   (echo "Argument to scm-help must be a string"))
	  ((string=? name "?")
	   (begin
	    (echo "Available modules:") (echo)
	    (dump-module-list scm-help-list)
	    (collect-garbage)
	    )
	   )
	  (#t (dump-help scm-help-list name))
	  )
	 )
	)
       (set! scm-add-help 
	(lambda (name long-form fns)
	 (set! scm-help-list (cons (list name long-form fns) scm-help-list))
	 )
	)
       )
  )



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;
;  Everyone should put their help file in their own particular
;  implementation directory.
;
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(scm-add-help
 "sel" "Selection commands"
"Selection:

  :sel.netlist \"nm\"   Selects a netlist labelled nm restricted by the box.
                      All labels named nm under the box are selected along
                      with whatever is electrically connected to them.

  :sel.push           Pushes current selection onto selection stack
  :sel.pop            Pops top of selection stack

  :sel.cmp            Compares current selection with the top-of-stack. The
                      List of labels that are different are bound to the label
                      search list and can be browsed using label.find-next."
)
  

(scm-add-help
 "prs" "Production-rule drawing"
"Production rule drawing:

  :prs.draw w \"rule\"      Draws transistor stacks for production rule with
                          diffusion width w.

  :prs.talldraw w \"rule\"  Same as prs.draw, only doesn't squish out diffusion
                          between poly and contacts for intermediate nodes.

  :prs.mgn pw nw \"r1\" ... Draws list of rules, sharing contacts if possible.

  :prs.tallmgn pw nw \"r1\" Same as before, except unsquished diffusion."
)

(scm-add-help
 "label" "Label manipulation"
"Label Manipulation

  :label.rename \"n1\" \"n2\"   renames all instances of n1 under the current
                            box with n2.

  :label.swap \"n1\" \"n2\"     renames all instances of n1 with n2 and n2 with
                            n1 under the current box.

  :label.search \"n\"         looks for all labels matching n under box.
  :label.find-next          moves box to the next label position."
)

(scm-add-help
 "gate" "Standard gates"
"Standard gate generation

The gate generation commands are of the form
  :command pw nw o i1 i2 ... iN
pw and nw specify the width of the p- and n-diffusion stacks. o is the output,
and i1 thru iN are the inputs.

  :gate.c pw nw  o i1 .. iN    C-element
  :gate.cf pw nw o i1 .. iN    Folded C-element
  :gate.inv pw nw o i          Inverter
  :gate.nor pw nw o i1 .. iN   NOR gate
  :gate.nand pw nw o i1 .. iN  NAND gate"
)
