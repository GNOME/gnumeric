;;;;;;;;;;;;;;;;;;;;;;;;
;; Ariel Rios         ;;
;; ariel@arcavia.com  ;;
;;;;;;;;;;;;;;;;;;;;;;;;

;; Licensed under GPL
;; Copyright 1999-2000 Ariel Rios

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;fibonacci function
;;Usage: =FIBO(number)
;;Calculates the fibonacci number of number.

(define (fibo k)
  (let ((n (value-get-as-int k)))
    (letrec ((fibof
	      (lambda (n a b)
		(if (<= n 2)
		    b
		    (fibof (- n 1) b (+ a b))))))
      (value-new-float (fibof n 1 1)))))

(register-function 
"fibo" "f"
"@FUNCTION=FIBO
@SYNTAX=FIBO(num)
@DESCRIPTION=Returns the fibonnacci computation for number."
"Guile"
fibo)

;;;;;;;;;;;;proof

;(define (test n)
;  1)

;(register-function
;"test" "a"
;"@FUNCTION=TEST
;@SYNTAX=TEST(num)
;@DESCRIPTION=test"
;test)

;; Guile-gtk example use with care!

;(use-modules (gtk gtk))  
                  
;(define (ggtest n)
;(call-with-current-continuation
; (lambda(val)
; (let ((window (gtk-window-new 'toplevel))
;       (calendar (gtk-calendar-new)))
; (gtk-container-add window calendar)
; (gtk-widget-show-all window)
; (gtk-standalone-main window)

;(break n)))))


;(register-function
;"ggtest" "f"
;"@FUNCTION=GGTEST
;@SYNTAX=GGTEST(num)
;@DESCRIPTION=Guile-gtk test"
;"Guile"
;ggtest)








