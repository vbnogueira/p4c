.PHONY: all
all: tester calc cvtbase

tester: test-arith.o
	cc -o tester test-arith.o

test-arith.o: test-arith.c ../../gen-code/arithmetic.c
	cc -c -o test-arith.o test-arith.c

calc: calc.o
	cc -o calc calc.o

calc.o: calc.c lcs-cvt
	cc -E -o calc_.i calc.c && ./lcs-cvt calc_.i && cc -c -o calc.o calc_.i

lcs-cvt: lcs-cvt.c
	cc -o lcs-cvt lcs-cvt.c

cvtbase: cvtbase.c
	cc -o cvtbase cvtbase.c
