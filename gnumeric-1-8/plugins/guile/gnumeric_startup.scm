; -*- scheme -*-
;
; Gnumeric Guile plug-in system startup file
;
; Mark Probst (schani@unix.cslab.tuwien.ac.at)
;

;(display "Guile plug-in initializing\n")
(load "functions.scm")

;; Error handling
;; This function is from gnucash, but simplified
(define (gnm:error->string tag args)
  (define (write-error port)
    (false-if-exception
     (apply display-error (fluid-ref the-last-stack) port args))
    (force-output port))
    (false-if-exception
     (call-with-output-string write-error)))

; cell-refs
(define (make-cell-ref col row)
  (if (and (number? col) (number? row))
      (cons 'cell-ref (cons col row))
      '()))

(define (cell-ref? ob)
  (and (pair? ob) (eq? (car ob) 'cell-ref)))

(define cell-ref-col cadr)

(define cell-ref-row cddr)

; vars
(define (make-var cell-ref)
  (cons 'var cell-ref))

(define (var? ob)
  (and (pair? ob) (eq? (car ob) 'var)))

(define var-cell-ref cdr)

; operators
(define binary-operator? '())
(define binary-operator-name '())
(define binary-operator-function '())
(let ((binary-op-names
       (list (cons '= "=")
	     (cons '> ">")
	     (cons '< "<")
	     (cons '>= ">=")
	     (cons '<= "<=")
	     (cons '<> "<>")
	     (cons '+ "+")
	     (cons '- "-")
	     (cons '* "*")
	     (cons '/ "/")
	     (cons 'expt "^")
	     (cons 'string-append "&")))
      (binary-op-funcs
       (list (cons '= =)
	     (cons '> >)
	     (cons '< <)
	     (cons '>= >=)
	     (cons '<= <=)
	     (cons '<> (lambda (n1 n2) (not (= n1 n2))))
	     (cons '+ +)
	     (cons '- -)
	     (cons '* *)
	     (cons '/ /)
	     (cons 'expt expt)
	     (cons 'string-append string-append))))

  (set! binary-operator?
	(lambda (op)
	  (if (assq op binary-op-names) #t #f)))

  (set! binary-operator-name
	(lambda (op)
	  (cdr (assq op binary-op-names))))

  (set! binary-operator-function
	(lambda (op)
	  (cdr (assq op binary-op-funcs)))))

; exprs
;; this should really be coded in C
(define (unparse-expr expr)
  (define (unparse-subexpr expr)
    (cond ((number? expr)
	   (number->string expr))
	  ((string? expr)
	   (string-append "\"" expr "\"")) ; FIXME: should also quote "'s inside
	  ((var? expr)
	   (let ((cell-ref (var-cell-ref expr)))
	     (string-append
	      (string (integer->char (+ (char->integer #\A) (cell-ref-col cell-ref)))) ; FIXME: this only works if col < 26
	      (number->string (+ (cell-ref-row cell-ref) 1)))))
	  ((list? expr)
	   (let ((op (car expr)))
	     (cond ((binary-operator? op)
		    (string-append "("
				   (unparse-subexpr (cadr expr))
				   (binary-operator-name op)
				   (unparse-subexpr (caddr expr))
				   ")"))
		   ((eq? op 'neg)
		    (string-append "-(" (unparse-subexpr (cadr expr)) ")"))
		   ((eq? op 'funcall)
		    (string-append (cadr expr) "()"))	; FIXME: should unparse args
		   (else
		    "ERROR"))))
	  (else
	   "ERROR")))

  (string-append "=" (unparse-subexpr expr)))

;; this should also be coded in C
(define (eval-expr expr)
  (define (eval-expr-list expr-list)
    (if (null? expr-list)
	'()
	(cons (eval-expr (car expr-list)) (eval-expr-list (cdr expr-list)))))

  (cond ((number? expr) expr)
	((string? expr) expr)
	((var? expr) (cell-value (var-cell-ref expr)))
	((list? expr)
	 (let ((op (car expr)))
	   (cond ((binary-operator? op)
		  ((binary-operator-function op) (eval-expr (cadr expr)) (eval-expr (caddr expr))))
		 ((eq? op 'neg)
		  (- (eval-expr (cadr expr))))
		 ((eq? op 'funcall)
		  (gnumeric-funcall (cadr expr) (eval-expr-list (caddr expr))))
		 (else
		  "ERROR"))))
	(else
	 "ERROR")))



; symbolic differentiation with immediate evaluation
;; in case of a funcall this should do numeric differentiation
(define (differentiate expr var)
  (cond ((number? expr) 0)
	((var? expr)
	 (let ((cell-ref (var-cell-ref expr)))
	   (if (equal? var cell-ref)
	       1
	       (differentiate (cell-expr cell-ref) var))))
	((list? expr)
	 (let ((op (car expr)))
	   (cond ((binary-operator? op)
		  (let ((left-arg (cadr expr))
			(right-arg (caddr expr)))
		    (cond ((eq? op '+)
			   (+ (differentiate left-arg var) (differentiate right-arg var)))
			  ((eq? op '-)
			   (- (differentiate left-arg var) (differentiate right-arg var)))
			  ((eq? op '*)
			   (+ (* (eval-expr left-arg) (differentiate right-arg var))
			      (* (eval-expr right-arg) (differentiate left-arg var))))
			  ((eq? op '/)
			   (let ((v (eval-expr right-arg)))
			     (/ (- (* (differentiate left-arg var) v)
				   (* (differentiate right-arg var) (eval-expr left-arg)))
				(* v v))))
			  ((eq? op 'expt)
			   (let ((u (eval-expr left-arg))
				 (v (eval-expr right-arg))
				 (du (differentiate left-arg var))
				 (dv (differentiate right-arg var)))
			     (+ (* (expt u (- v 1)) v du) (* (expt u v) (log u) dv))))
			  (else
			   "ERROR"))))
		 ((eq? op 'neg)
		  (- (differentiate (cadr expr) var)))
		 (else
		  "ERROR"))))
	(else
	 "ERROR")))

; a little expression simplifier and constant folder
(define (simplify-expr expr)
  (define (constant? expr)
    (or (number? expr) (string? expr)))

  (cond ((or (number? expr) (string? expr) (var? expr))
	 expr)
	((list? expr)
	 (let ((op (car expr)))
	   (cond ((binary-operator? op)
		  (let* ((left-arg (simplify-expr (cadr expr)))
			 (right-arg (simplify-expr (caddr expr)))
			 (new-expr (list op left-arg right-arg)))
		    (cond ((and (constant? left-arg) (constant? right-arg))
			   (eval-expr new-expr))
			  ((and (eq? op '+) (number? left-arg) (zero? left-arg))
			   right-arg)
			  ((and (or (eq? op '+) (eq? op '-)) (number? right-arg) (zero? right-arg))
			   left-arg)
			  ((and (eq? op '*) (number? left-arg) (= left-arg 1))
			   right-arg)
			  ((and (or (eq? op '*) (eq? op '/)) (number? right-arg) (= right-arg 1))
			   left-arg)
			  ((and (eq? op 'expt) (number? left-arg) (or (zero? left-arg) (= left-arg 1)))
			   left-arg)
			  ((and (eq? op 'expt) (number? right-arg) (= right-arg 1))
			   left-arg)
			  (else
			   new-expr))))
		 ((eq? op 'neg)
		  (let* ((arg (simplify-expr (cadr expr)))
			 (new-expr (list op arg)))
		    (if (constant? arg)
			(eval-expr new-expr)
			new-expr)))
		 (else
		  expr))))		; should also handle functions without side effects
	(else
	 expr)))

; load user init-file if present
(let ((home-gnumericrc (string-append (getenv "HOME") "/.gnumeric/guile.scm")))
  (if (access? home-gnumericrc R_OK)
      (load home-gnumericrc)))

;(display "Guile plug-in initialization complete\n")










