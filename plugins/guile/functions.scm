;; Guile Plugin Functions

;; Originally coded by Mark Probst

;; Modified by Ariel Rios
;; jarios@usa.net
;; Licensed under GPL
;; December 1999

;;sign function 
;;Usage: =SIGN(number)
;;returns -1, 0 or 1.

(define (sign num)
  (cond ((negative? num) -1)
	((zero? num) 0)
	(else 1)))



(register-function
 "sign" "f"
 "@FUNCTION=SIGN
@SYNTAX=SIGN(number)
@DESCRIPTION=Returns -1 if NUMBER is less than 0, 1 if NUMBER
is greater than 0 and 0 if NUMBER is equal 0."
 sign)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;lcm function
;;Usage: =LCM(num, num)
;;returns the least common multiplier of N1 and N2
;;Please note that there is not lcm definition here
;;for it's defined on R4RS

(register-function
 "lcm" "ff"
 "@FUNCTION=LCM
@SYNTAX=LCM(n1,n2)
@DESCRIPTION=Returns the least common multiplier of N1 and N2."
 lcm)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;gcd function
;;Usage: =GCD(num, num)

(register-function
 "gcd" "ff"
 "@FUNCTION=GCD
@SYNTAX=GCD(n1,n2)
@DESCRIPTION=Returns the greatest common divisor of N1 and N2."
 gcd)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;fibonacci function
;;Usage: =FIBO(number)
;;Calculates the fibonacci number of number.
;;This function shows GP problem in handling great numbers
;;Until I arrange this the scheme functions checks whether
;;the function is bigger than 2 ^ 32
;;if so it returns a string.

(define (fibo n)
  (letrec ((fibof
            (lambda (n a b)
             (if (<= n 2)
                 b
                 (fibof (- n 1) b (+ a b))))))

    (fibof n 1 1)))



(register-function 
"fibo" "f"
"@FUNCTION=FIBO
@SYNTAX=FIBO(num)
@DESCRIPTION=Returns the fibonnacci computation for number."
fibo)



