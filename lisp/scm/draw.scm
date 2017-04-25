;-------------------------------------------------------------------------
;
;  Simple rectangle drawing commands
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
;  $Id: draw.scm,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
;
; Requires: box.scm, drc.scm, label.scm
;
;-------------------------------------------------------------------------

;
; Draw a layer from current lower left corner of box
;
(define draw.layer-nr
  (lambda (layer dx dy)
    (let* ((pos (getbox))
	  (llx (car pos))
	  (lly (cadr pos)))
      (begin (define nx (+ llx dx))
	     (define ny (+ lly dy))
	     (box llx lly nx ny)
	     (paint layer)
             (list llx lly nx ny)
	     )
      )
    )
  )


(define draw.layer
  (lambda (layer dx dy)
    (let ((pos (getbox)))
      (begin (box.push pos)
	     (let ((x (draw.layer-nr layer dx dy))) 
	       (begin 
		 (box.pop) 
		 x
		 )
	       )
	     )
      )
    )
  )


;
;  Draw a series of vertical rectangles with labels.
;
(define draw.vert-rectangles-with-labels
  (lambda (layer-name dx dy spacing labels)
    (if (null? labels)
	#t
	(begin (draw.layer-nr layer-name dx dy)
	       (label.draw (car labels) layer-name)
	       (box.move 0 (+ spacing dy))
	       (draw.vert-rectangles-with-labels
		layer-name dx dy spacing (cdr labels))
	       )
	)
    )
  )

;
;  Draw a series of vertical rectangles
;
(define draw.vert-rectangles
  (lambda (layer-name dx dy spacing number)
    (repeat number
	    (lambda ()
	      (begin (draw.layer-nr layer-name dx dy)
		     (box.move 0 (+ spacing dy))
		     )
	      )
	    )
    )
  )

