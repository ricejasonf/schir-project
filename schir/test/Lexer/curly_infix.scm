; RUN: schir-scheme --mode=read %s 2>&1 | FileCheck  %s --strict-whitespace --match-full-lines

; Neoteric expressions are not supported without the directive.
; CHECK:f
; CHECK:(x)
f(x)

; CHECK:f
; CHECK:(a b)
f(a b)

; CHECK:-
; CHECK:(a)
-(a)

; CHECK:(f)
; CHECK:(x)
(f)(x)

; CHECK:f
; CHECK:(x)
; CHECK:(y)
f(x)(y)

; CHECK:(quote f)
; CHECK:(x)
'f(x)

; CHECK:"f"
; CHECK:(x)
"f"(x)

; CHECK:#(1 2)
; CHECK:(x)
#(1 2)(x)

; CHECK:(f g (x))
(f g(x))


#!curly-infix


; Neoteric expressions only apply within curly-infix lists.
; CHECK:f
; CHECK:(x)
f(x)

; CHECK:f
; CHECK:(a b)
f(a b)

; CHECK:-
; CHECK:(a)
-(a)

; CHECK:(f)
; CHECK:(x)
(f)(x)

; CHECK:f
; CHECK:(x)
; CHECK:(y)
f(x)(y)

; CHECK:(quote f)
; CHECK:(x)
'f(x)

; CHECK:"f"
; CHECK:(x)
"f"(x)

; CHECK:#(1 2)
; CHECK:(x)
#(1 2)(x)

; CHECK:(f g (x))
(f g(x))


; Examples of c-expressions from SRFI-105.
; CHECK:(<= n 5)
{n <= 5}

; CHECK:(+ x 1)
{x + 1}

; CHECK:(+ a b c)
{a + b + c}

; CHECK:((unquote op) x y z)
{x ,op y ,op z}

; CHECK:(eqv? x (quasiquote a))
{x eqv? `a}

; CHECK:(eq? (quote a) b)
{'a eq? b}

; CHECK:(+ n-1 n-2)
{n-1 + n-2}

; CHECK:(* a (+ b c))
{a * {b + c}}

; CHECK:(+ a (- b c))
{a + {b - c}}

; CHECK:(- (+ a b) c)
{{a + b} - c}

; CHECK:(and (> a 0) (>= b 1))
{{a > 0} and {b >= 1}}

; CHECK:()
{}

; CHECK:5
{5}

; CHECK:(- x)
{- x}

; CHECK:(>= (length x) 6)
{length(x) >= 6}

; CHECK:(+ (f x) (g y) (h z))
{f(x) + g(y) + h(z)}

; CHECK:(+ (f a b) (g h))
{(f a b) + (g h)}

; CHECK:(+ (f a b) (g h))
{f(a b) + g(h)}

; CHECK:(quote (+ a (f b) x))
'{a + f(b) + x}

; CHECK:(/ (- a) b)
{(- a) / b}

; CHECK:(/ (- a) b)
{-(a) / b}

; CHECK:(cos q)
{cos(q)}

; CHECK:(e)
{e{}}

; CHECK:(pi)
{pi()}

; CHECK:(quote (f x))
{'f(x)}


; TODO Support SRFI-38 datum labels.
; expected: #1=(f #1#)
; {#1=f(#1#)}


; CHECK:(f (g (h x)))
{ (f (g h(x))) }

; CHECK:#(1 2 (f a) 4)
{#(1 2 f(a) 4)}

; CHECK:(f (h x))
{(f #;g(x) h(x))}

; CHECK:(map - ns)
{(map - ns)}

; CHECK:(map - ns)
{map(- ns)}

; CHECK:(* n (factorial (- n 1)))
{n * factorial{n - 1}}

; CHECK:(* 2 (sin (- x)))
{2 * sin{- x}}

; CHECK:($nfx$ 3 + 4 +)
{3 + 4 +}

; CHECK:($nfx$ 3 + 4 + 5 +)
{3 + 4 + 5 +}

; CHECK:($nfx$ a . z)
{a . z}

; CHECK:($nfx$ a + b - c)
{a + b - c}

; CHECK:(read . options)
{read(. options)}

; CHECK:((a x) y)
{a(x)(y)}

; CHECK:($bracket-apply$ x a)
{x[a]}

; CHECK:($bracket-apply$ y a b)
{y[a b]}

; CHECK:((f (- n 1)) x)
{f{n - 1}(x)}

; CHECK:((f (- n 1)) (- y 1))
{f{n - 1}{y - 1}}

; CHECK:($bracket-apply$ (f (- x)) y)
{f{- x}[y]}


; Additional cases

; CHECK:(f a b)
{f(a b)}

; CHECK:(f ())
{f{()}}

; CHECK:e
{(. e)}

; CHECK:(a b c)
{. (a b c)}


; Whitespace before the opening token prevents neoteric mapping.
; CHECK:(f (x))
{f (x)}
