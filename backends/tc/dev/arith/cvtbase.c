/*
 * cvtbase -- convert from one base to another.  Usage:
 *
 * cvtbase [-mNNN] <input-base-spec> <output-base-spec> [ input-file ... ]
 *
 * where a base spec is one of:
 *
 *	d
 *	D	Specifies "decimal" -- digits 0 through 9
 *
 *	x
 *	h	Specifies "hexadecimal" -- digits 0-9 and a-f
 *
 *	X
 *	H	Specifies "Hexadecimal" -- digits 0-9 and A-F
 *
 *	o
 *	O	Specifies "octal" -- digits 0 through 7
 *
 *	b
 *	B	Specifies "binary" -- digits 0 and 1
 *
 * or a string of two or more characters, which are the digits (eg. 012
 *  would use ternary notation).
 *
 * -m specifies the minimum number of digits to output in the target
 *  base.  The default is, of course, 1.  -m0 is like the default
 *  except that if the value is zero, nothing is output.
 *
 * Bases 0 and 1 are disallowed.
 *
 * If any input files are given, they are read and converted.  If no
 *  input files are given, the standard input is read and converted.
 *
 * The converted output appears on the standard output.
 *
 * This file is in the public domain.  It may be used by anyone in any
 *  way for any purpose.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

extern const char *__progname;

typedef struct number NUMBER;

struct number {
  unsigned int alloc : 16;
  unsigned int len : 16;
  unsigned char *bits;
  } ;

static int errs;

static int indig;
static const char *idigits;
static int ondig;
static const char *odigits;
static int mindig = 1;

static void num_init(NUMBER *n)
{
 n->alloc = 0;
 n->len = 0;
 n->bits = 0;
}

static void num_zero(NUMBER *n)
{
 n->len = 0;
}

static int num_iszero(NUMBER *n)
{
 return(n->len==0);
}

static void num_muladd(NUMBER *n, int m, int a)
{
 int i;

 for (i=0;i<n->len;i++)
  { a += n->bits[i] * m;
    n->bits[i] = a & 0xff;
    a >>= 8;
  }
 while (a)
  { if (n->len >= n->alloc) n->bits = realloc(n->bits,n->alloc=n->len+8);
    n->bits[n->len++] = a & 0xff;
    a >>= 8;
  }
}

static void num_divrem_int(NUMBER *n, int div, int *remp)
{
 int a;
 int i;

 a = 0;
 for (i=n->len-1;i>=0;i--)
  { a = (a << 8) + n->bits[i];
    n->bits[i] = a / div;
    a %= div;
  }
 *remp = a;
 while ((n->len > 0) && (n->bits[n->len-1] == 0)) n->len --;
}

static void get_number(NUMBER *n, int ndig, const char *digits)
{
 int c;
 char *cp;

 num_zero(n);
 while (1)
  { c = getchar();
    if (c == EOF) exit(feof(stdin)?0:1);
    if ((cp=index(digits,c)) != 0)
     { break;
     }
    putchar(c);
  }
 while (1)
  { num_muladd(n,ndig,cp-digits);
    c = getchar();
    if (c == EOF) break;
    if ((cp=index(digits,c)) == 0) break;
  }
 if (c != EOF) ungetc(c,stdin);
}

static void put_number(NUMBER *n, int ndig, const char *digits)
{
 static char *out = 0;
 static int outalloc = 0;
 int outlen;
 int r;

 outlen = 0;
 while ((outlen < mindig) || !num_iszero(n))
  { num_divrem_int(n,ndig,&r);
    if (outlen >= outalloc) out = realloc(out,outalloc=outlen+8);
    out[outlen++] = digits[r];
  }
 while (outlen > 0) putchar(out[--outlen]);
}

static void get_base(const char *arg, int *Ndig, const char **Digits)
#define ndig (*Ndig)
#define digits (*Digits)
{
 const char *origarg;

 origarg = arg;
 switch (*arg++)
  { case 'b': case 'B':
       digits = "01";
       break;
    case 'o': case 'O':
       digits = "01234567";
       break;
    case 'd': case 'D':
       digits = "0123456789";
       break;
    case 'h': case 'x':
       digits = "0123456789abcdef";
       break;
    case 'H': case 'X':
       digits = "0123456789ABCDEF";
       break;
    case '\0':
       fprintf(stderr,"%s: null base specifier `%s'\n",__progname,origarg);
       errs = 1;
       return;
       break;
    default:
       if (*arg == '\0')
	{ fprintf(stderr,"%s: unknown base key `%c' (or base 1)\n",
			__progname,arg[-1]);
	  errs = 1;
	  return;
	}
       digits = arg-1;
       arg = "";
       break;
  }
 if (*arg)
  { fprintf(stderr,"%s: junk `%s' at end of base spec `%s'\n",
				__progname,arg,origarg);
    errs = 1;
    return;
  }
 ndig = strlen(digits);
}
#undef ndig
#undef digits

int main(int, char **);
int main(int ac, char **av)
{
 NUMBER n;

 errs = 0;
 while ((ac > 0) && (av[1][0] == '-'))
  { if (av[1][1] == 'm')
     { mindig = atoi(av[1]+2);
       ac --;
       av ++;
       continue;
     }
    if (!strcmp(av[1],"--")) break;
    fprintf(stderr,"%s: unrecognized flag `%s'\n",__progname,av[1]);
    errs ++;
  }
 if (errs || (ac < 3))
  { fprintf(stderr,"Usage: %s [flags] <input-base> <output-base> [ files ... ]\n",
			__progname);
    exit(1);
  }
 get_base(av[1],&indig,&idigits);
 get_base(av[2],&ondig,&odigits);
 if (errs) exit(1);
 num_init(&n);
 while (1)
  { get_number(&n,indig,idigits);
    put_number(&n,ondig,odigits);
  }
}
