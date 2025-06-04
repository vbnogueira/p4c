#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <strings.h>

/*
 * See `struct num', below; it avoids memory waste for
 *  2+BASEBITS+(2*PRECBITS) bits to fit in a machine word.  BASEBITS is
 *  the number of bits necessary to hold any value MINBASE..MAXBASE;
 *  PRECBITS likewise for MINPREC..MAXPREC.
 */
#define MINBASE     2
#define MAXBASE   255
#define MINPREC     1
#define MAXPREC  2047
#define MINEXP  -2048
#define MAXEXP   2047
typedef unsigned char MANTTYPE;
#define BASEBITS  8
#define PRECBITS 11
#define KARATSUBA_THRESHOLD 16

extern const char *__progname;

enum nodetype {
  NT_CONST,
  NT_VAR,
  NT_HIST,
  NT_UNARY,
  NT_BINARY,
  NT_FUNCTION
  } ;

enum unaryop {
  U_PLUS,
  U_MINUS,
  U_RECIP
  } ;

enum binaryop {
  B_PLUS,
  B_MINUS,
  B_TIMES,
  B_DIVIDE,
  B_ASSIGN
  } ;

enum toktype {
  TOK_EOF,
  TOK_EOL,
  TOK_ERR,
  TOK_NUMBER,
  TOK_INUMBER,
  TOK_VAR,
  TOK_CHAR,
  TOK_FLAGS
#define TOKF_ECHO   0x00000001
#define TOKF_NOHIST 0x00000002
#define TOKF_NONL   0x00000004
#define TOKF_QUIET  0x00000008
  } ;

typedef enum binaryop BINARYOP;
typedef enum nodetype NODETYPE;
typedef enum toktype TOKTYPE;
typedef enum unaryop UNARYOP;
typedef struct fxn FXN;
typedef struct node NODE;
typedef struct num NUM;
typedef struct numlist NUMLIST;
typedef struct var VAR;
typedef struct memblk MEMBLK;

struct memblk {
  MEMBLK *flink;
  MEMBLK *blink;
  unsigned int len;
  int line;
  int printed;
  } __attribute__((__aligned__));

/* if zero is set, neg must be zero, and exp, digs, mant are ignored -
   but mant must still be allocated or nil. */
/* note that mant[0] is the most, not least, significant digit */
struct num {
  unsigned int neg : 1;
  unsigned int zero : 1;
  unsigned int base : BASEBITS;
  unsigned int prec : PRECBITS;
  unsigned int digs : PRECBITS;
  signed int exp;
  unsigned int refcnt;
  MANTTYPE *mant;
  } ;

struct numlist {
  NUMLIST *link;
  int n;
  NUM *value;
  } ;

struct node {
  NODETYPE type;
  union {
    NUM *c;
    char *var;
    int hist;
    struct {
      UNARYOP op;
      NODE *arg;
      } u;
    struct {
      BINARYOP op;
      NODE *lhs;
      NODE *rhs;
      } b;
    struct {
      char *name;
      int inx;
      int nargs;
      NODE **args;
      } f;
    } u;
  } ;

struct var {
  VAR *link;
  char *name;
  NUM *value;
  } ;

struct fxn {
  const char *name;
  union {
    NUM *(*numfn)(int, NUM **);
    NUM *(*nodefn)(int, NODE **);
    } u;
  unsigned int flags;
#define FF_NOEVAL 0x00000001
  } ;
#define NUMFN(name) { #name, { numfn: C_##name }, 0 }
#define NODEFN(name) { #name, { nodefn: C_##name }, FF_NOEVAL }

static int exprno;
static VAR *vars;
static NUMLIST *history;
static int untokened;
static TOKTYPE toktype;
static int toklen;
static char *tokbuf;
static int tokhave;
static NUM *toknum;
static int tokinum;
static int ungetval;
static int ungotten;
static int defprec;
static int defbase;
static int pminexp;
static int pmaxexp;
static int debugging;
static MEMBLK *memblks;
static unsigned int warned;
#define WARN_FRACDROP 0x00000001
#define WARN_OVERFLOW 0x00000002
#define WARN_LOSSMULT 0x00000004
#define WARN_LOSSADD  0x00000008

static int qflag = 0;
static int hflag = 0;
static int mflag = 0;
static int lflags = 0;

static NUM *C_chop(int, NUM **);
static NUM *C_round(int, NUM **);
static NUM *C_cvt(int, NUM **);
static NUM *C_pow(int, NUM **);
static NUM *C_abs(int, NUM **);
static NUM *C_sqrt(int, NUM **);
static NUM *C_BASE(int, NUM **);
static NUM *C_PREC(int, NUM **);
static NUM *C_EXP(int, NUM **);
static NUM *C_MANT(int, NUM **);
static NUM *C_ULP(int, NUM **);
static NUM *C_int(int, NUM **);
static NUM *C_gcd(int, NUM **);
static NUM *C_baseline(int, NUM **);
static NUM *C_log(int, NUM **);
static NUM *C_solve(int, NODE **);
static NUM *C_const(int, NODE **);
static NUM *C_let(int, NODE **);
static FXN fxns[] = { NUMFN(chop),
		      NUMFN(round),
		      NUMFN(cvt),
		      NUMFN(pow),
		      NUMFN(abs),
		      NUMFN(sqrt),
		      NUMFN(int),
		      NUMFN(BASE),
		      NUMFN(PREC),
		      NUMFN(EXP),
		      NUMFN(MANT),
		      NUMFN(ULP),
		      NUMFN(gcd),
		      NUMFN(baseline),
		      NUMFN(log),
		      NODEFN(solve),
		      NODEFN(const),
		      NODEFN(let),
		      { 0 } };

/* forward */
static NODE *parse_top(void);

static void init(void)
{
 exprno = 1;
 vars = 0;
 history = 0;
 untokened = 0;
 tokbuf = 0;
 tokhave = 0;
 toknum = 0;
 ungotten = 0;
 defprec = 64;
 defbase = 10;
 pminexp = -5;
 pmaxexp = 10;
 debugging = 0;
 memblks = 0;
}

static int get(void)
{
 if (ungotten)
  { ungotten = 0;
    return(ungetval);
  }
 return(getchar());
}

static void unget(int c)
{
 ungotten = 1;
 ungetval = c;
}

static void flushnl(void)
{
 int c;

 do
  { c = get();
  } while ((c != '\n') && (c != EOF));
}

static void pwarn(int warnbit, const char *msg)
{
 if (warned & warnbit) return;
 warned |= warnbit;
 printf("warning: %s\n",msg);
}

static void *malloc_blk(unsigned int nb, int lno)
{
 MEMBLK *b;

 if (nb < 1) return(0);
 if (! mflag) return(malloc(nb));
 b = malloc(sizeof(MEMBLK)+nb);
 b->len = nb;
 b->line = lno;
 b->printed = 0;
 if (memblks) memblks->blink = b;
 b->flink = memblks;
 b->blink = 0;
 memblks = b;
 return(b+1);
}
#define malloc(x) malloc_blk((x),__LINE__)

static void free_blk(void *old)
{
 MEMBLK *b;

 if (! old) return;
 if (! mflag)
  { free(old);
    return;
  }
 b = ((MEMBLK *)old) - 1;
 if (b->flink) b->flink->blink = b->blink;
 if (b->blink) b->blink->flink = b->flink; else memblks = b->flink;
 free(b);
}
#define free(x) free_blk(x)

static void *realloc_blk(void *old, unsigned int nb, int lno)
{
 MEMBLK *b;
 MEMBLK *ob;

 if (nb < 1)
  { free_blk(old);
    return(0);
  }
 if (! mflag) return(realloc(old,nb));
 if (! old) return(malloc_blk(nb,lno));
 ob = ((MEMBLK *)old) - 1;
 b = realloc(ob,sizeof(MEMBLK)+nb);
 b->len = nb;
 b->line = lno;
 if (b != ob)
  { if (b->flink) b->flink->blink = b;
    if (b->blink) b->blink->flink = b; else memblks = b;
  }
 return(b+1);
}
#define realloc(x,n) realloc_blk((x),(n),__LINE__)

static char *strdup_blk(const char *s, int lno)
{
 char *rv;

 rv = malloc_blk(strlen(s)+1,lno);
 strcpy(rv,s);
 return(rv);
}
#define strdup(s) strdup_blk((s),__LINE__)

static MANTTYPE *mmalloc_blk(int n, int lno)
{
 return(malloc_blk(n*sizeof(MANTTYPE),lno));
}
#define mmalloc(n) mmalloc_blk((n),__LINE__)

static MANTTYPE *mrealloc_blk(MANTTYPE *old, int n, int lno)
{
 return(realloc_blk(old,n*sizeof(MANTTYPE),lno));
}
#define mrealloc(x,n) mrealloc_blk((x),(n),__LINE__)

static void leakcheck(void)
{
 if (memblks)
  { MEMBLK *b;
    unsigned int i;
    for (b=memblks;b;b=b->flink)
     { if (! b->printed)
	{ printf("line %d: %u %p =",b->line,b->len,(void *)(b+1));
	  for (i=0;i<b->len;i++) printf(" %02x",((unsigned char *)(b+1))[i]);
	  printf("\n");
	  b->printed = 1;
	}
     }
  }
}

static void mzero(MANTTYPE *m, int n)
{
 if (n > 0) bzero(m,n*sizeof(MANTTYPE));
}

static void mcopy(MANTTYPE *f, MANTTYPE *t, int n)
{
 bcopy(f,t,n*sizeof(MANTTYPE));
}

static NUM *num_alloc(void)
{
 NUM *rv;

 rv = malloc(sizeof(NUM));
 rv->neg = 0;
 rv->zero = 0;
 rv->base = defbase;
 rv->prec = defprec;
 rv->digs = 0;
 rv->exp = 0;
 rv->refcnt = 1;
 rv->mant = 0;
 return(rv);
}

static void num_free(NUM *n)
{
 free(n->mant);
 memset(n,'x',sizeof(NUM));
 free(n);
}

static NUM *num_ref(NUM *n)
{
 n->refcnt ++;
 if (n->refcnt > 100) abort();
 return(n);
}

static void num_deref(NUM *n)
{
 if (n == 0) return;
 if (n->refcnt == 0) abort();
 n->refcnt --;
 if (n->refcnt > 100) abort();
 if (n->refcnt == 0) num_free(n);
}

static void node_free(NODE *n)
{
 switch (n->type)
  { case NT_CONST:
       num_deref(n->u.c);
       break;
    case NT_VAR:
       free(n->u.var);
       break;
    case NT_HIST:
       break;
    case NT_UNARY:
       node_free(n->u.u.arg);
       break;
    case NT_BINARY:
       node_free(n->u.b.lhs);
       node_free(n->u.b.rhs);
       break;
    case NT_FUNCTION:
	{ int i;
	  free(n->u.f.name);
	  for (i=0;i<n->u.f.nargs;i++) node_free(n->u.f.args[i]);
	  free(n->u.f.args);
	}
       break;
  }
 free(n);
}

static void printdigit(MANTTYPE digit, unsigned int base)
{
 if (base <= 36) putchar("0123456789abcdefghijklmnopqrstuvwxyz"[digit]);
 else            printf(":%lu",(unsigned long int)digit);
}

static void printnum(NUM *n)
{
 int i;

 if (n->zero)
  { printdigit(0,10);
    return;
  }
 if (n->neg) putchar('-');
 if (n->base != defbase)
  { putchar('#');
    switch (n->base)
     { case 2:  putchar('b'); break;
       case 8:  putchar('o'); break;
       case 10: putchar('d'); break;
       case 16: putchar('x'); break;
       default: printf("%d#",n->base); break;
     }
  }
 if (n->prec != defprec)
  { printf("_%d_",n->prec);
  }
 if ( ((n->exp >= pminexp) && (n->exp <= pmaxexp)) ||
      ((n->exp > 0) && (n->digs <= n->exp) && ((n->digs*3) > (n->exp*2))) )
  { if (n->exp > 0)
     { if ((n->prec == defprec) && (defprec < n->exp))
	{ printf("_%d_",defprec);
	}
       if (n->digs <= n->exp)
	{ for (i=0;i<n->digs;i++) printdigit(n->mant[i],n->base);
	  for (;i<n->exp;i++) printdigit(0,n->base);
	}
       else
	{ for (i=0;i<n->exp;i++) printdigit(n->mant[i],n->base);
	  putchar('.');
	  for (;i<n->digs;i++) printdigit(n->mant[i],n->base);
	}
     }
    else
     { putchar('.');
       for (i=n->exp;i<0;i++) printdigit(0,n->base);
       for (i=0;i<n->digs;i++) printdigit(n->mant[i],n->base);
     }
  }
 else
  { putchar('.');
    for (i=0;i<n->digs;i++) printdigit(n->mant[i],n->base);
    if (n->exp) printf("@%d",n->exp);
  }
}

static int roundnum(MANTTYPE *dp, unsigned int base, unsigned int leftdigs, unsigned int rightdigs)
{
 if (base & 1)
  { MANTTYPE *rp;
    unsigned int dv;
    dv = (base - 1) / 2;
    rp = dp;
    while ((rightdigs-- > 0) && (*rp++ == dv)) ;
    if ((rightdigs == -1U) || (*rp < dv)) return(0);
  }
 else
  { if (*dp < base/2) return(0);
  }
 while ((leftdigs-- > 0) && (*--dp == base-1)) *dp = 0;
 if (leftdigs == -1U)
  { return(1);
  }
 else
  { ++*dp;
    return(0);
  }
}

static NUM *copynum(NUM *n)
{
 NUM *rv;

 rv = malloc(sizeof(NUM));
 *rv = *n;
 rv->refcnt = 1;
 if (! n->zero)
  { rv->mant = mmalloc(rv->digs);
    mcopy(n->mant,rv->mant,rv->digs);
  }
 else
  { rv->mant = 0;
  }
 return(rv);
}

static NUM *num_modify(NUM *n, int minrefs)
{
 if (n->refcnt > minrefs)
  { num_deref(n);
    n = copynum(n);
  }
 return(n);
}

static int num_intval(NUM *n)
{
 int iv;
 int i;
 int nd;

 if (n->zero) return(0);
 if (n->digs == 0) return(0);
 if (n->digs > n->exp)
  { pwarn(WARN_FRACDROP,"discarding fractional part");
    nd = n->exp;
  }
 else
  { nd = n->digs;
  }
 if (n->exp < 1) return(0);
 iv = 0;
 for (i=0;i<nd;i++) iv = (iv * n->base) + n->mant[i];
 i = n->exp - nd;
 if (i > sizeof(int)*CHAR_BIT)
  { pwarn(WARN_OVERFLOW,"overflow");
    i = sizeof(int) * CHAR_BIT;
  }
 for (;i>0;i--) iv *= n->base;
 if (n->neg) iv = - iv;
 return(iv);
}

static void setmant1(NUM *n)
{
 n->mant[0] = 1;
 mzero(&n->mant[1],n->digs-1);
}

static NODE *node_make(int type, ...)
{
 NODE *n;
 va_list ap;

 n = malloc(sizeof(NODE));
 n->type = type;
 va_start(ap,type);
 switch (type)
  { default: abort(); break;
    case NT_CONST:
       n->u.c = va_arg(ap,NUM *);
       break;
    case NT_VAR:
       n->u.var = va_arg(ap,char *);
       break;
    case NT_HIST:
       n->u.hist = va_arg(ap,int);
       break;
    case NT_UNARY:
       n->u.u.op = va_arg(ap,int);
       n->u.u.arg = va_arg(ap,NODE *);
       break;
    case NT_BINARY:
       n->u.b.lhs = va_arg(ap,NODE *);
       n->u.b.op = va_arg(ap,int);
       n->u.b.rhs = va_arg(ap,NODE *);
       break;
    case NT_FUNCTION:
	{ int i;
	  n->u.f.name = va_arg(ap,char *);
	  n->u.f.inx = -1;
	  n->u.f.nargs = va_arg(ap,int);
	  n->u.f.args = malloc(n->u.f.nargs*sizeof(NODE *));
	  for (i=0;i<n->u.f.nargs;i++) n->u.f.args[i] = va_arg(ap,NODE *);
	}
       break;
  }
 va_end(ap);
 return(n);
}

static NUM *num_for_int_detail(int v, int base, int prec)
{
 NUM *n;
 int i;
 int t;

 n = num_alloc();
 n->base = base;
 n->prec = prec;
 if (v == 0)
  { n->zero = 1;
    return(n);
  }
 if (v < 0)
  { n->neg = 1;
    v = - v;
  }
 for (i=1,t=v;t>=base;i++,t/=base) ;
 n->digs = i;
 n->exp = i;
 n->mant = mmalloc(i);
 for (i--,t=v;i>=0;i--,t/=base) n->mant[i] = t % base;
 return(n);
}

static NUM *num_for_int(int v)
{
 return(num_for_int_detail(v,defbase,defprec));
}

#define SETREF(loc,num) do { num_deref(loc); (loc) = num_ref(num); } while (0)

static NUM *num_chop(NUM *n, int p)
{
 n = num_ref(n);
 if (p < n->prec)
  { n = num_modify(n,1);
    n->prec = p;
  }
 if (p < n->digs)
  { int i;
    n = num_modify(n,1);
    for (i=p-1;(i>=0)&&(n->mant[i]==0);i--) ;
    if (i < 0) n->zero = 1; else n->digs = i + 1;
  }
 return(n);
}

static NUM *C_chop(int nargs, NUM **args)
{
 NUM *rv;
 int p;

 if (nargs != 2)
  { printf("usage: chop(number,digits)\n");
    return(0);
  }
 p = num_intval(args[1]);
 rv = num_chop(args[0],p);
 return(rv);
}

static NUM *C_round(int nargs, NUM **args)
{
 NUM *rv;
 int p;

 if (nargs != 2)
  { printf("usage: round(number,digits)\n");
    return(0);
  }
 rv = num_ref(args[0]);
 p = num_intval(args[1]);
 if (p < rv->prec) rv->prec = p;
 if (p < rv->digs)
  { int nr;
    rv = num_modify(rv,2);
    nr = rv->digs - p;
    rv->digs = p;
    if (roundnum(rv->mant+p,rv->base,p,nr))
     { if (rv->exp == MAXEXP)
	{ printf("overflow in round(), clipping exponent to @%d\n",MAXEXP);
	}
       else
	{ rv->exp ++;
	}
       rv->digs = 1;
       setmant1(rv);
     }
    else
     { for (p=rv->digs-1;(p>=0)&&(rv->mant[p]==0);p--) ;
       if (p < 0) rv->zero = 1; else rv->digs = p + 1;
     }
  }
 return(rv);
}

static NUM *C_cvt(int nargs, NUM **args)
{
 NUM *rv;
 unsigned int b;
 unsigned int p;
 unsigned int ob;
 unsigned int op;
 MANTTYPE *it;
 unsigned int nt;
 unsigned int toff;
 MANTTYPE *tmp;
 int i;
 int oexp;
 int nexp;

 if ((nargs < 2) || (nargs > 3))
  { printf("usage: cvt(number,base[,precision])\n");
    return(0);
  }
 b = num_intval(args[1]);
 p = (nargs > 2) ? num_intval(args[2]) : defprec;
 if ((b < MINBASE) || (b > MAXBASE))
  { printf("invalid base in cvt()\n");
    return(0);
  }
 if ((p < MINPREC) || (p > MAXPREC))
  { printf("invalid precision in cvt()\n");
    return(0);
  }
 if (args[0]->zero)
  { // converting zero is rare, but should work
    rv = num_alloc();
    rv->base = b;
    rv->prec = p;
    rv->zero = 1;
    return(rv);
  }
 toff = (sizeof(int) * CHAR_BIT) + 1;
 nt = p + BASEBITS + 2;
 tmp = mmalloc(nt+toff) + toff;
 ob = args[0]->base;
 op = args[0]->digs;
 it = mmalloc(op);
 mcopy(args[0]->mant,it,op);
 i = 0;
 nexp = 0;
 while (i < nt)
  { int a;
    int j;
    a = 0;
    if (op > 0)
     { for (j=op-1;j>=0;j--)
	{ a += it[j] * b;
	  it[j] = a % ob;
	  a /= ob;
	}
       if (it[op-1] == 0) op --;
     }
    if (a || i) tmp[i++] = a; else nexp--;
  }
 free(it);
 oexp = args[0]->exp;
 while (oexp > 0)
  { unsigned int power;
    unsigned int pmax;
    int j;
    unsigned int a;
    pmax = UINT_MAX / (b * ob);
    for (power=1;oexp&&(power<pmax);oexp--,power*=ob) ;
    a = 0;
    for (j=nt-1;j>=0;j--)
     { a += tmp[j] * power;
       tmp[j] = a % b;
       a /= b;
     }
    for (;a;j--)
     { tmp[j] = a % b;
       a /= b;
     }
    j ++;
    if (j < 0)
     { nexp -= j;
       mcopy(tmp+j,tmp,nt);
     }
  }
 if (oexp < 0)
  { mcopy(tmp,tmp-toff,nt);
    tmp -= toff;
    mzero(tmp+nt,toff);
    nt += toff;
    while (oexp < 0)
     { unsigned int power;
       unsigned int pmax;
       int j;
       unsigned int a;
       pmax = UINT_MAX / (b * ob);
       for (power=1;oexp&&(power<pmax);oexp++,power*=ob) ;
       a = 0;
       for (j=0;j<nt;j++)
	{ a = (a * b) + tmp[j];
	  tmp[j] = a / power;
	  a %= power;
	}
       for (i=0;tmp[i]==0;i++) ;
       if (i > 0)
	{ mcopy(tmp+i,tmp,nt-i);
	  nexp -= i;
	  for (j-=i;j<nt;j++)
	   { a *= b;
	     tmp[j] = a / power;
	     a %= power;
	   }
	}
     }
    nt -= toff;
    tmp += toff;
    mcopy(tmp-toff,tmp,nt);
  }
 if (roundnum(tmp+p,b,p,nt-p))
  { nexp ++;
    tmp[0] = 1;
    mzero(tmp+1,nt-1);
  }
 rv = num_alloc();
 rv->base = b;
 rv->prec = p;
 for (i=p-1;(i>=0)&&(tmp[i]==0);i--) ;
 if (i < 0)
  { rv->zero = 1;
  }
 else
  { rv->digs = i + 1;
    if (nexp < MINEXP)
     { printf("exponent underflow in cvt(), returning zero\n");
       rv->zero = 1;
     }
    else
     { if (nexp > MAXEXP)
	{ printf("exponent overflow in cvt(), clipping to @%d\n",MAXEXP);
	  nexp = MAXEXP;
	}
       rv->exp = nexp;
       rv->mant = mmalloc(rv->digs);
       mcopy(tmp,rv->mant,rv->digs);
     }
  }
 free(tmp-toff);
 return(rv);
}

static void mixedbase(void)
{
 printf("mixed-base arithmetic unimplemented\n");
}

/*
 * Return -1, 0, or 1 according as a is <, =, or > b.
 */
static int num_cmp(NUM *a, NUM *b)
{
 int i;
 int d;
 int gt;

 if (a == b) return(0);
 if (a->zero) return(b->zero?0:b->neg?1:-1);
 if (b->zero) return(a->neg?-1:1);
 if (a->neg != b->neg) return(a->neg?-1:1);
 gt = a->neg ? -1 : 1;
 if (a->exp != b->exp) return((a->exp>b->exp)?gt:-gt);
 d = (a->digs < b->digs) ? a->digs : b->digs;
 for (i=0;i<d;i++) if (a->mant[i] != b->mant[i]) return((a->mant[i]>b->mant[i])?gt:-gt);
 if (a->digs < b->digs) return(-gt);
 if (a->digs > b->digs) return(gt);
 return(0);
}

/*
 * We use Karatsuba's multiplier algorithm.  See karatsuba.doc for
 *  details on just what this means.
 */

#ifdef MULTIPLIER_TRACE
static int ktsu_dd = 0;
#endif

/*
 * Baseline multiplication.  The grounding case for the multiplier's
 *  recursion.  We could do this the log-space column-by-column way
 *  instead, but it doesn't make enough difference to care about.
 */
static void mul_baseline(unsigned int base, MANTTYPE *a, int ad, MANTTYPE *b, int bd, MANTTYPE *p, int pd)
{
 int ax;
 int bx;
 int px0;
 int px;
 unsigned int prod;

#ifdef MULTIPLIER_TRACE
 int i;
 ktsu_dd ++;
 printf("%*smul_baseline: a=[%d]",ktsu_dd*2,"",ad);
 for (i=0;i<ad;i++) printdigit(a[i],base);
 printf(" b=[%d]",bd);
 for (i=0;i<bd;i++) printdigit(b[i],base);
 printf("\n");
#endif
 if ((ad < 1) || (bd < 1) || (pd < 1)) abort();
 mzero(p,pd);
 px0 = pd - ad - bd + 1;
 for (bx=bd-1;bx>=0;bx--)
  { for (ax=ad-1;ax>=0;ax--)
     { px = px0 + ax + bx;
       prod = a[ax] * b[bx];
       while (prod)
	{ if (px < 0) abort();
	  prod += p[px];
	  p[px] = prod % base;
	  prod /= base;
	  px --;
	}
     }
  }
#ifdef MULTIPLIER_TRACE
 printf("%*sp=[%d]",ktsu_dd*2,"",pd);
 for (i=0;i<pd;i++) printdigit(p[i],base);
 printf("\n");
 ktsu_dd --;
#endif
}

/*
 * Auxiliary Karatsuba routine.
 *
 * Adds a, length ad, to s, length sd.  s is modified in-place, and the
 *  low digits are lined up (s will usually have more digits off the
 *  low end, but they are hidden by reducing sd to the appropriate
 *  value when calling this).  ad must be <= sd, and all carries must
 *  fit within s.
 */
static void mul_karatsuba_add(unsigned int base, MANTTYPE *a, int ad, MANTTYPE *s, int sd)
{
 unsigned int acc;
 int ax;
 int sx;

#ifdef MULTIPLIER_TRACE
 int i;
 ktsu_dd ++;
 printf("%*smul_karatsuba_add: a=[%d]",ktsu_dd*2,"",ad);
 for (i=0;i<ad;i++) printdigit(a[i],base);
 printf(" s=[%d]",sd);
 for (i=0;i<sd;i++) printdigit(s[i],base);
 printf("\n");
#endif
 if ((ad < 1) || (sd < 1) || (ad > sd)) abort();
 acc = 0;
 for (ax=ad-1,sx=sd-1;ax>=0;ax--,sx--)
  { acc += a[ax] + s[sx];
    if (acc >= base)
     { s[sx] = acc - base;
       acc = 1;
     }
    else
     { s[sx] = acc;
       acc = 0;
     }
  }
 while (acc)
  { if (sx < 0) abort();
    acc += s[sx];
    if (acc >= base)
     { s[sx] = acc - base;
       acc = 1;
     }
    else
     { s[sx] = acc;
       acc = 0;
     }
    sx --;
  }
#ifdef MULTIPLIER_TRACE
 printf("%*sresult=[%d]",ktsu_dd*2,"",sd);
 for (i=0;i<sd;i++) printdigit(s[i],base);
 printf("\n");
 ktsu_dd --;
#endif
}

/*
 * Auxiliary Karatsuba routine.
 *
 * Subtracts m, length md, from s, length sd.  s is modified in-place,
 *  and the low digits are lined up (s will usually have more digits
 *  off the low end, but they are hidden by reducing sd to the
 *  appropriate value when calling this).  ad must be <= sd, and there
 *  must be no borrow from the top of s.
 */
static void mul_karatsuba_sub(unsigned int base, MANTTYPE *m, int md, MANTTYPE *s, int sd)
{
 int acc;
 int mx;
 int sx;

#ifdef MULTIPLIER_TRACE
 int i;
 ktsu_dd ++;
 printf("%*smul_karatsuba_sub: m=[%d]",ktsu_dd*2,"",md);
 for (i=0;i<md;i++) printdigit(m[i],base);
 printf(" s=[%d]",sd);
 for (i=0;i<sd;i++) printdigit(s[i],base);
 printf("\n");
#endif
 if ((md < 1) || (sd < 1) || (md > sd)) abort();
 acc = 0;
 for (mx=md-1,sx=sd-1;mx>=0;mx--,sx--)
  { acc += (int)s[sx] - (int)m[mx];
    if (acc < 0)
     { s[sx] = acc + base;
       acc = -1;
     }
    else
     { s[sx] = acc;
       acc = 0;
     }
  }
 while (acc)
  { if (sx < 0) abort();
    acc += s[sx];
    if (acc < 0)
     { s[sx] = acc + base;
       acc = -1;
     }
    else
     { s[sx] = acc;
       acc = 0;
     }
    sx --;
  }
#ifdef MULTIPLIER_TRACE
 printf("%*sresult=[%d]",ktsu_dd*2,"",sd);
 for (i=0;i<sd;i++) printdigit(s[i],base);
 printf("\n");
 ktsu_dd --;
#endif
}

/*
 * Auxiliary Karatsuba routine.
 *
 * Computes the difference between a and b, that is, |a-b|.  a and b
 *  are each n digits long.  The difference is written into d, which is
 *  also n digits long.  If a-b is negative, that is, if a<b, then
 *  *negp is set to 1, otherwise to 0.
 */
static void mul_karatsuba_diff(unsigned int base, MANTTYPE *a, MANTTYPE *b, int n, MANTTYPE *d, int *negp)
{
 int i;
 int j;
 int acc;

#ifdef MULTIPLIER_TRACE
 ktsu_dd ++;
 printf("%*smul_karatsuba_diff: a=[%d]",ktsu_dd*2,"",n);
 for (i=0;i<n;i++) printdigit(a[i],base);
 printf(" b=[%d]",n);
 for (i=0;i<n;i++) printdigit(b[i],base);
 printf("\n");
#endif
 i = 0;
 while (1)
  { if (i >= n)
     { mzero(d,n);
       *negp = 0;
#ifdef MULTIPLIER_TRACE
       printf("%*smul_karatsuba_diff: equal, returning zero\n",ktsu_dd*2,"");
       ktsu_dd --;
#endif
       return;
     }
    if (a[i] != b[i]) break;
    d[i] = 0;
    i ++;
  }
#ifdef MULTIPLIER_TRACE
 printf("%*sdifference at [%d]\n",ktsu_dd*2,"",i);
#endif
 acc = 0;
 if (a[i] > b[i])
  { *negp = 0;
#ifdef MULTIPLIER_TRACE
    printf("%*sa > b, neg=0\n",ktsu_dd*2,"");
#endif
  }
 else
  { *negp = 1;
     { MANTTYPE *t; t = a; a = b; b = t; }
#ifdef MULTIPLIER_TRACE
    printf("%*sa < b, neg=1, swapping\n",ktsu_dd*2,"");
#endif
  }
 for (j=n-1;j>=i;j--)
  { acc += (int)a[j] - (int)b[j];
    if (acc < 0)
     { d[j] = acc + base;
       acc = -1;
     }
    else
     { d[j] = acc;
       acc = 0;
     }
  }
 if (acc != 0) abort();
#ifdef MULTIPLIER_TRACE
 printf("%*sd=[%d]",ktsu_dd*2,"",n);
 for (i=0;i<n;i++) printdigit(d[i],base);
 printf(" neg=%d\n",*negp);
 ktsu_dd --;
#endif
}

/*
 * Main Karatsuba call.
 *
 * Multiplies a by b, each of which is d digits long, putting the
 *  result in p, which is 2d digits long.  d must be a power of two.
 */
static void mul_karatsuba(unsigned int base, MANTTYPE *a, MANTTYPE *b, int d, MANTTYPE *p)
{
 MANTTYPE *ah_bh;
 MANTTYPE *al_bl;
 MANTTYPE *n1;
 MANTTYPE *n2;
 MANTTYPE *np;
 int neg1;
 int neg2;
 int d2;

#ifdef MULTIPLIER_TRACE
 int i;
 ktsu_dd ++;
 printf("%*smul_karatsuba: a=[%d]",ktsu_dd*2,"",d);
 for (i=0;i<d;i++) printdigit(a[i],base);
 printf(" b=[%d]",d);
 for (i=0;i<d;i++) printdigit(b[i],base);
 printf("\n");
#endif
 if ((d < 1) || (d & (d-1))) abort();
 if (d <= KARATSUBA_THRESHOLD)
  {
#ifdef MULTIPLIER_TRACE
    printf("%*sdoing baseline mult, %d <= %d\n",ktsu_dd*2,"",d,KARATSUBA_THRESHOLD);
#endif
    mul_baseline(base,a,d,b,d,p,d+d);
#ifdef MULTIPLIER_TRACE
    ktsu_dd --;
#endif
    return;
  }
 d2 = d >> 1;
 ah_bh = mmalloc(d);
 al_bl = mmalloc(d);
 mul_karatsuba(base,a,b,d2,ah_bh);
#ifdef MULTIPLIER_TRACE
 printf("%*sah*bh = ",ktsu_dd*2,"");
 for (i=0;i<d;i++) printdigit(ah_bh[i],base);
 printf("\n");
#endif
 mul_karatsuba(base,a+d2,b+d2,d2,al_bl);
#ifdef MULTIPLIER_TRACE
 printf("%*sal*bl = ",ktsu_dd*2,"");
 for (i=0;i<d;i++) printdigit(al_bl[i],base);
 printf("\n");
#endif
 n1 = mmalloc(d2);
 n2 = mmalloc(d2);
 mul_karatsuba_diff(base,a,a+d2,d2,n1,&neg1);
#ifdef MULTIPLIER_TRACE
 printf("%*sah-al=%c[%d]",ktsu_dd*2,"",neg1?'-':'+',d2);
 for (i=0;i<d2;i++) printdigit(n1[i],base);
 printf("\n");
#endif
 mul_karatsuba_diff(base,b,b+d2,d2,n2,&neg2);
#ifdef MULTIPLIER_TRACE
 printf("%*sbh-bl=%c[%d]",ktsu_dd*2,"",neg2?'-':'+',d2);
 for (i=0;i<d2;i++) printdigit(n2[i],base);
 printf("\n");
#endif
 np = mmalloc(d);
 mul_karatsuba(base,n1,n2,d2,np);
 mzero(p,d2);
 mzero(p+(3*d2),d2);
 mcopy(ah_bh,p+d2,d);
#ifdef MULTIPLIER_TRACE
 printf("%*sp #1 = [%d]",ktsu_dd*2,"",d+d);
 for (i=0;i<d+d;i++) printdigit(p[i],base);
 printf("\n");
#endif
 mul_karatsuba_add(base,al_bl,d,p,3*d2);
#ifdef MULTIPLIER_TRACE
 printf("%*sp #2 = [%d]",ktsu_dd*2,"",d+d);
 for (i=0;i<d+d;i++) printdigit(p[i],base);
 printf("\n");
#endif
 if (neg1 ? !neg2 : neg2) mul_karatsuba_add(base,np,d,p,3*d2); else mul_karatsuba_sub(base,np,d,p,3*d2);
#ifdef MULTIPLIER_TRACE
 printf("%*sp #3 = [%d]",ktsu_dd*2,"",d+d);
 for (i=0;i<d+d;i++) printdigit(p[i],base);
 printf("\n");
#endif
 mul_karatsuba_add(base,al_bl,d,p,2*d);
#ifdef MULTIPLIER_TRACE
 printf("%*sp #4 = [%d]",ktsu_dd*2,"",d+d);
 for (i=0;i<d+d;i++) printdigit(p[i],base);
 printf("\n");
#endif
 mul_karatsuba_add(base,ah_bh,d,p,d);
#ifdef MULTIPLIER_TRACE
 printf("%*sp #5 = [%d]",ktsu_dd*2,"",d+d);
 for (i=0;i<d+d;i++) printdigit(p[i],base);
 printf("\n");
#endif
 free(ah_bh);
 free(al_bl);
 free(n1);
 free(n2);
 free(np);
#ifdef MULTIPLIER_TRACE
 ktsu_dd --;
#endif
}

/*
 * Top-level Karatsuba call.  We could move all this stuff into
 *  mul_karatsuba, but there's no reason to pay that price for every
 *  call when most of it is needed for only the top-level call.  b must
 *  not be the larger of the two numbers (in terms of digit counts, not
 *  algebraeic comparison - that is, bd must be <= ad).
 */
static void mul_karatsuba_top(unsigned int base, MANTTYPE *a, int ad, MANTTYPE *b, int bd, MANTTYPE *p, int pd)
{
 MANTTYPE *mt;
 int d2;
 int i;

#ifdef MULTIPLIER_TRACE
 ktsu_dd ++;
 printf("%*smul_karatsuba_top: a=[%d]",ktsu_dd*2,"",ad);
 for (i=0;i<ad;i++) printdigit(a[i],base);
 printf(" b=[%d]",bd);
 for (i=0;i<bd;i++) printdigit(b[i],base);
 printf(" pd=%d\n",pd);
#endif
 if ((ad < 1) || (bd < 1) || (pd < 1) || (ad < bd) || (pd < ad+bd)) abort();
 if (bd <= KARATSUBA_THRESHOLD)
  {
#ifdef MULTIPLIER_TRACE
    printf("%*sdoing baseline mult, %d <= %d\n",ktsu_dd*2,"",bd,KARATSUBA_THRESHOLD);
#endif
    mul_baseline(base,a,ad,b,bd,p,pd);
#ifdef MULTIPLIER_TRACE
    ktsu_dd --;
#endif
    return;
  }
 d2 = bd;
 while (d2 & (d2-1)) d2 &= d2-1;
#ifdef MULTIPLIER_TRACE
 printf("%*sd2=%d\n",ktsu_dd*2,"",d2);
#endif
 mzero(p,pd);
 mt = mmalloc(d2+d2);
 if (d2 < bd)
  {
#ifdef MULTIPLIER_TRACE
    printf("%*sdoing high %d of b\n",ktsu_dd*2,"",bd-d2);
#endif
    for (i=ad-d2;i>=0;i-=d2)
     { mul_karatsuba_top(base,a+i,d2,b,bd-d2,mt,bd);
       mul_karatsuba_add(base,mt,bd,p,pd+i-ad);
     }
    if (i > -d2)
     {
#ifdef MULTIPLIER_TRACE
       printf("%*sdoing high %d of b * high %d of a\n",ktsu_dd*2,"",bd-d2,i+d2);
#endif
       if (i+d2 < bd-d2)
	{ mul_karatsuba_top(base,b,bd-d2,a,i+d2,mt,bd+i);
	}
       else
	{ mul_karatsuba_top(base,a,i+d2,b,bd-d2,mt,bd+i);
	}
       mul_karatsuba_add(base,mt,bd+i,p,pd+i-ad);
     }
  }
#ifdef MULTIPLIER_TRACE
 printf("%*sdoing low %d of b * chunks of a\n",ktsu_dd*2,"",d2);
#endif
 for (i=ad-d2;i>=0;i-=d2)
  { mul_karatsuba(base,a+i,b+bd-d2,d2,mt);
    mul_karatsuba_add(base,mt,d2+d2,p,pd+i+d2-ad);
  }
 if (i > -d2)
  {
#ifdef MULTIPLIER_TRACE
    printf("%*sdoing low %d of b * high %d of a\n",ktsu_dd*2,"",d2,i+d2);
#endif
    mul_karatsuba_top(base,b+bd-d2,d2,a,i+d2,mt,d2+d2+i);
    mul_karatsuba_add(base,mt,d2+d2+i,p,pd+i+d2-ad);
  }
#ifdef MULTIPLIER_TRACE
 printf("%*sreturning [%d]",ktsu_dd*2,"",pd);
 for (i=0;i<pd;i++) printdigit(p[i],base);
 printf("\n");
#endif
 free(mt);
#ifdef MULTIPLIER_TRACE
 ktsu_dd --;
#endif
}

static NUM *multiplier(NUM *n1, NUM *n2, int force_base)
{
 MANTTYPE *pbuf;
 NUM *rv;
 int d;
 int e;
 int p;

 if (n1->zero) return(num_ref(n1));
 if (n2->zero) return(num_ref(n2));
 if (n1->base != n2->base)
  { mixedbase();
    return(0);
  }
#ifdef MULTIPLIER_TRACE
 ktsu_dd = 0;
#endif
 if (n1->digs < n2->digs) { NUM *t; t = n1; n1 = n2; n2 = t; }
 d = n1->digs + n2->digs;
 e = n1->exp + n2->exp;
 pbuf = mmalloc(d);
 if (force_base)
  { mul_baseline(n1->base,n1->mant,n1->digs,n2->mant,n2->digs,pbuf,d);
  }
 else
  { mul_karatsuba_top(n1->base,n1->mant,n1->digs,n2->mant,n2->digs,pbuf,d);
  }
 if (pbuf[0] == 0)
  { if (pbuf[1] == 0) abort();
    d --;
    e --;
    mcopy(pbuf+1,pbuf,d);
  }
 while (pbuf[d-1] == 0)
  { if (d == 1) abort();
    d --;
  }
 if (d > MAXPREC)
  { pwarn(WARN_LOSSMULT,"significance loss in multiplication");
    if (roundnum(pbuf+MAXPREC,n1->base,MAXPREC,d-MAXPREC))
     { e ++;
       d = 1;
       pbuf[0] = 1;
     }
    else
     { d = MAXPREC;
     }
  }
 p = n1->prec;
 if (p < n2->prec) p = n2->prec;
 if (p < d) p = d;
 rv = num_alloc();
 rv->neg = (n1->neg != n2->neg);
 rv->base = n1->base;
 rv->prec = p;
 rv->digs = d;
 rv->exp = e;
 rv->mant = pbuf;
 return(rv);
}

static NUM *num_multiply(NUM *n1, NUM *n2)
{
 return(multiplier(n1,n2,0));
}

static NUM *C_baseline(int nargs, NUM **args)
{
 if (nargs != 2)
  { printf("usage: baseline(base,power)\n");
    return(0);
  }
 return(multiplier(args[0],args[1],1));
}

static NUM *num_divide(NUM *num, NUM *den)
{
 NUM *rv;
 int e;
 unsigned int b;
 unsigned int nr;
 unsigned int qd;
 unsigned int qx;
 MANTTYPE *q;
 MANTTYPE *r;
 MANTTYPE *d;
 int i;
 int a;
 unsigned int qdig;
 double drem;
 double dden;

 if (num->zero) return(num_ref(num));
 if (den->zero)
  { printf("division by zero");
    return(0);
  }
 b = num->base;
 if (b != den->base)
  { mixedbase();
    return(0);
  }
 e = 1 + num->exp - den->exp;
 qd = num->digs;
 if (qd < defprec) qd = defprec;
 nr = num->digs;
 if (nr <= qd+den->digs) nr = qd + den->digs + 1;
 q = mmalloc(qd+1);
 r = mmalloc(nr+1) + 1;
 d = mmalloc(den->digs+1) + 1;
 mcopy(num->mant,r,num->digs);
 if (nr > num->digs) mzero(r+num->digs,nr-num->digs);
 r[-1] = 0;
 mcopy(den->mant,d,den->digs);
 d[-1] = 0;
 qx = 0;
 dden = 0;
 for (i=den->digs-1;i>=0;i--) dden = (dden + den->mant[i]) / b;
 while (qx < qd)
  { drem = 0;
    for (i=den->digs-1;i>=0;i--) drem = (drem + r[i]) / b;
    drem += r[-1];
    qdig = drem / dden;
    if (qdig >= b) qdig = b-1;
    while (1)
     { int qdinc;
       a = 0;
       for (i=den->digs-1;i>=-1;i--)
	{ a += r[i] - (qdig * d[i]);
	  if (a < 0)
	   { int c;
	     c = - ((b-1-a) / b);
	     r[i] = a - (c * b);
	     a = c;
	   }
	  else
	   { r[i] = a % b;
	     a /= b;
	   }
	}
       if (a < 0)
	{ qdinc = -1;
	}
       else
	{ qdinc = 0;
	  for (i=-1;i<(int)den->digs;i++)
	   { if (r[i] > d[i])
	      { qdinc = 1;
		break;
	      }
	     else if (r[i] < d[i])
	      { break;
	      }
	   }
	  if (i >= (int)den->digs) qdinc = 1;
	}
       if (qdinc)
	{ a = 0;
	  for (i=den->digs-1;i>=-1;i--)
	   { a += r[i] + (qdig * d[i]);
	     r[i] = a % b;
	     a /= b;
	   }
	  qdig += qdinc;
	}
       else
	{ break;
	}
     }
    if (qdig || qx) q[qx++] = qdig; else e --;
    mcopy(r,r-1,nr);
    r[nr-1] = 0;
  }
 r --;
 a = 0;
 for (i=nr;i>=0;i--)
  { a += 2 * r[i];
    r[i] = a % b;
    a /= b;
  }
 if (a)
  { q[qx] = b - 1;
  }
 else
  { for (i=0;i<den->digs;i++)
     { if (r[i] < d[i])
	{ q[qx] = 0;
	  break;
	}
       if (d[i] < r[i])
	{ q[qx] = b - 1;
	  break;
	}
     }
    if (i >= den->digs) q[qx] = b - 1;
  }
 if (roundnum(q+qx,b,qd,1))
  { e ++;
    q[0] = 1;
    mzero(q+1,qd-1);
  }
 for (i=qd-1;(i>=0)&&(q[i]==0);i--) ;
 rv = num_alloc();
 rv->neg = (num->neg != den->neg);
 rv->base = b;
 rv->prec = qd;
 if (i < 0)
  { rv->zero = 1;
    free(q);
  }
 else
  { rv->digs = i + 1;
    if (e < MINEXP)
     { printf("exponent underflow in division, returning 0\n");
       rv->zero = 1;
     }
    else if (e > MAXEXP)
     { printf("exponent overflow in division, clipping to @%d\n",MAXEXP);
       e = MAXEXP;
     }
    rv->exp = e;
    rv->mant = q;
  }
 free(d-1);
 free(r);
 return(rv);
}

static VAR *var_by_name(const char *name, int create)
{
 VAR *v;

 for (v=vars;v&&strcmp(v->name,name);v=v->link) ;
 if (!v && create)
  { v = malloc(sizeof(VAR));
    v->name = strdup(name);
    v->link = vars;
    vars = v;
    v->value = 0;
  }
 return(v);
}

static NUM *num_add(NUM *n1, NUM *n2)
{
 NUM *rv;
 unsigned int d;
 MANTTYPE *tmp;
 unsigned int b;
 unsigned int p;
 unsigned int ediff;

 if (n1->zero) return(num_ref(n2));
 if (n2->zero) return(num_ref(n1));
 b = n1->base;
 if (b != n2->base)
  { mixedbase();
    return(0);
  }
 if (n1->exp < n2->exp)
  { NUM *t;
    t = n1;
    n1 = n2;
    n2 = t;
  }
 ediff = n1->exp - n2->exp;
 d = n2->digs + ediff;
 if (d < n1->digs) d = n1->digs;
 p = n2->prec;
 if (p < n1->prec) p = n1->prec;
 tmp = mmalloc(d+2) + 1;
 mzero(tmp,d);
 rv = num_alloc();
 rv->base = b;
 if (n1->neg == n2->neg)
  { unsigned int a;
    int i;
    rv->neg = n1->neg;
    rv->exp = n1->exp;
    mcopy(n2->mant,tmp+ediff,n2->digs);
    a = 0;
    for (i=n1->digs-1;i>=0;i--)
     { a += tmp[i] + n1->mant[i];
       tmp[i] = a % b;
       a /= b;
     }
    if (a)
     { tmp[-1] = a;
       d ++;
       mcopy(tmp-1,tmp,d);
       if (rv->exp == MAXEXP)
	{ printf("exponent overflow when adding, chopping to @%d\n",MAXEXP);
	}
       else
	{ rv->exp ++;
	}
     }
  }
 else
  { int a;
    int i;
    int z;
    rv->neg = n1->neg;
    for (i=n2->digs-1;i>=0;i--)
     { tmp[i+ediff] = b - 1 - n2->mant[i];
     }
    for (i=ediff-1;i>=0;i--) tmp[i] = b - 1;
    for (i=n2->digs+ediff-1;i>=0;i--)
     { if (tmp[i] == b-1)
	{ tmp[i] = 0;
	}
       else
	{ tmp[i] ++;
	  break;
	}
     }
    a = 0;
    z = 1;
    for (i=n2->digs+ediff-1;i>=n1->digs;i--)
     { if (tmp[i])
	{ z = 0;
	  break;
	}
     }
    for (i=n1->digs-1;i>=0;i--)
     { int v;
       a += tmp[i] + n1->mant[i];
       v = a % b;
       if (v) z = 0;
       tmp[i] = v;
       a /= b;
     }
    rv->zero = z;
    if (! a)
     { rv->neg = ! rv->neg;
       a = 0;
       for (i=d-1;(i>=0)&&(tmp[i]==0);i--) ;
       if (i >= 0)
	{ tmp[i] = b - tmp[i];
	  i --;
	  for (;i>=0;i--) tmp[i] = b - 1 - tmp[i];
	}
       else
	{ rv->zero = 1;
	}
     }
    if (! rv->zero)
     { for (i=0;tmp[i]==0;i++) ;
       if (i > 0)
	{ if (i > d) abort();
	  d -= i;
	  mcopy(tmp+i,tmp,d);
	}
       i = n1->exp - i;
       if (i < MINEXP)
	{ printf("exponent underflow when adding, setting result to 0\n");
	  rv->zero = 1;
	}
       else
	{ rv->exp = i;
	}
     }
  }
 if (! rv->zero)
  { int i;
    for (i=d-1;(i>=0)&&(tmp[i]==0);i--) ;
    if (d > MAXPREC)
     { pwarn(WARN_LOSSADD,"significance loss in addition");
       d = MAXPREC;
     }
    rv->digs = d;
    if (p < d) p = d;
    if (p < defprec) p = defprec;
    rv->prec = p;
    rv->mant = mmalloc(d);
    mcopy(tmp,rv->mant,d);
  }
 free(tmp-1);
 return(rv);
}

static int function_lookup(const char *name)
{
 int i;

 for (i=0;fxns[i].name;i++) if (!strcmp(fxns[i].name,name)) return(i);
 return(-1);
}

static NUM *evalnode(NODE *n)
{
 switch (n->type)
  { case NT_CONST:
       return(num_ref(n->u.c));
       break;
    case NT_VAR:
	{ VAR *v;
	  for (v=vars;v&&strcmp(v->name,n->u.var);v=v->link) ;
	  if (v && v->value) return(num_ref(v->value));
	  printf("no variable `%s' defined\n",n->u.var);
	  return(0);
	}
       break;
    case NT_HIST:
	{ NUMLIST *h;
	  for (h=history;h&&(h->n!=n->u.hist);h=h->link) ;
	  if (h) return(num_ref(h->value));
	  printf("no history element #%d exists\n",n->u.hist);
	  return(0);
	}
       break;
    case NT_UNARY:
	{ NUM *a;
	  a = evalnode(n->u.u.arg);
	  if (a == 0) return(0);
	  switch (n->u.u.op)
	   { case U_PLUS:
		return(a);
		break;
	     case U_MINUS:
		a = num_modify(a,1);
		a->neg = ! a->neg;
		return(a);
		break;
	     case U_RECIP:
		 { NUM *rv;
		   static NUM *one = 0;
		   if (!one || (one->base != a->base) || (one->prec != a->prec))
		    { num_deref(one);
		      one = num_alloc();
		      one->base = a->base;
		      one->prec = a->prec;
		      one->digs = 1;
		      one->exp = 1;
		      one->mant = mmalloc(1);
		      one->mant[0] = 1;
		    }
		   rv = num_divide(one,a);
		   num_deref(a);
		   return(rv);
		 }
		break;
	   }
	}
       break;
    case NT_BINARY:
       switch (n->u.b.op)
	{ case B_ASSIGN:
	      { VAR *v;
		NUM *val;
		if (n->u.b.lhs->type != NT_VAR)
		 { printf("invalid lhs of =, somehow, in evalnode");
		   return(0);
		 }
		val = evalnode(n->u.b.rhs);
		if (! val) return(0);
		v = var_by_name(n->u.b.lhs->u.var,1);
		SETREF(v->value,val);
		return(val);
	      }
	     break;
	  default:
	      { NUM *lhs;
		NUM *rhs;
		NUM *rv;
		lhs = evalnode(n->u.b.lhs);
		rhs = evalnode(n->u.b.rhs);
		if (!lhs || !rhs) return(0);
		switch (n->u.b.op)
		 { case B_PLUS:
		      rv = num_add(lhs,rhs);
		      break;
		   case B_MINUS:
		      rhs = num_modify(rhs,1);
		      rhs->neg = ! rhs->neg;
		      rv = num_add(lhs,rhs);
		      break;
		   case B_TIMES:
		      rv = num_multiply(lhs,rhs);
		      break;
		   case B_DIVIDE:
		      rv = num_divide(lhs,rhs);
		      break;
		   case B_ASSIGN: /* shut up -Wswitch */
		      break;
		 }
		num_deref(lhs);
		num_deref(rhs);
		return(rv);
	      }
	     break;
	}
       break;
    case NT_FUNCTION:
	{ int fx;
	  NUM **args;
	  int i;
	  NUM *rv;
	  fx = n->u.f.inx;
	  if (fx < 0)
	   { fx = function_lookup(n->u.f.name);
	     if (fx < 0)
	      { printf("unknown function name `%s'\n",n->u.f.name);
		return(0);
	      }
	     n->u.f.inx = fx;
	   }
	  if (fxns[fx].flags & FF_NOEVAL)
	   { rv = (*fxns[fx].u.nodefn)(n->u.f.nargs,n->u.f.args);
	   }
	  else
	   { args = malloc(n->u.f.nargs*sizeof(NUM *));
	     for (i=0;i<n->u.f.nargs;i++)
	      { args[i] = evalnode(n->u.f.args[i]);
		if (! args[i])
		 { for (;i>0;i--) num_deref(args[i]);
		   return(0);
		 }
	      }
	     rv = (*fxns[fx].u.numfn)(n->u.f.nargs,args);
	     for (i=0;i<n->u.f.nargs;i++) num_deref(args[i]);
	     free(args);
	   }
	  return(rv);
	}
       break;
  }
 fprintf(stderr,"invalid node type %d in evalnode\n",(int)n->type);
 abort();
 return(0);
}

static NUM *C_pow(int nargs, NUM **args)
{
 NUM *rv;
 NUM *base;
 NUM *t;
 unsigned int power;
 unsigned int pbit;
 int i;
 int neg;

 if (nargs != 2)
  { printf("usage: pow(base,power)\n");
    return(0);
  }
 base = args[0];
 i = num_intval(args[1]);
 if (i < 0)
  { neg = 1;
    i = - i;
  }
 else
  { neg = 0;
  }
 if (i == 0)
  { rv = num_alloc();
    rv->base = base->base;
    rv->digs = 1;
    rv->exp = 1;
    rv->mant = mmalloc(1);
    setmant1(rv);
    return(rv);
  }
 power = i;
 rv = num_ref(base);
 pbit = power;
 i = 1;
 while (pbit & (pbit+1))
  { pbit |= pbit >> i;
    i <<= 1;
  }
 pbit = (pbit & ~(pbit>>1)) >> 1;
 while (pbit)
  { t = num_multiply(rv,rv);
    num_deref(rv);
    if (power & pbit)
     { rv = num_multiply(t,base);
       num_deref(t);
     }
    else
     { rv = t;
     }
    pbit >>= 1;
  }
 if (neg)
  { NUM *q;
    t = num_alloc();
    t->base = base->base;
    t->digs = 1;
    t->exp = 1;
    t->mant = mmalloc(1);
    setmant1(t);
    q = num_divide(t,rv);
    num_deref(t);
    num_deref(rv);
    rv = q;
  }
 return(rv);
}

static NUM *C_abs(int nargs, NUM **args)
{
 NUM *n;

 if (nargs != 1)
  { printf("usage: abs(num)\n");
    return(0);
  }
 n = num_ref(args[0]);
 if (n->neg)
  { n = num_modify(n,2);
    n->neg = 0;
  }
 return(n);
}

#define INTNUM_CACHE 16

static NUM *int_num(int value, int base)
{
 static NUM *n[INTNUM_CACHE] = { 0 };
 static int bases[INTNUM_CACHE];
 static int values[INTNUM_CACHE];
 static int lu[INTNUM_CACHE];
 static int luv;
 int i;
 int f;

 if (! n[0])
  { for (i=0;i<INTNUM_CACHE;i++)
     { n[i] = 0;
       bases[i] = -1;
       values[i] = 0;
       lu[i] = 0;
     }
    luv = 1;
  }
 f = -1;
 for (i=0;i<INTNUM_CACHE;i++)
  { if ((bases[i] == base) && (values[i] == value))
     { lu[i] = luv++;
       return(num_ref(n[i]));
     }
    if ((f < 0) || (lu[i] < lu[f])) f = i;
  }
 num_deref(n[f]);
 bases[f] = base;
 values[f] = value;
 n[f] = num_for_int_detail(value,base,defprec);
 lu[f] = luv++;
 return(num_ref(n[f]));
}

static NUM *num_sqrt(NUM *n)
{
 NUM *two;
 NUM *t;
 NUM *t2;
 NUM *t3;
 NUM *d;
 NUM *a;
 NUM *b;

 if (n->zero) return(num_ref(n));
 two = int_num(2,n->base);
 t = num_alloc();
 t->base = n->base;
 t->prec = n->prec;
 t->digs = 1;
 if (n->exp < 0) t->exp = -((-n->exp)/2); else t->exp = (n->exp+1)/2;
 t->mant = mmalloc(1);
 t->mant[0] = 1;
 t2 = 0;
 t3 = 0;
 d = 0;
 while (1)
  { num_deref(t3);
    t3 = t2;
    t2 = t;
    a = num_divide(n,t);
    b = num_add(a,t);
    num_deref(a);
    a = num_divide(b,two);
    num_deref(b);
    t = num_chop(a,n->prec);
    num_deref(a);
    if (! t3) continue;
    num_deref(d);
    t3 = num_modify(t3,1);
    t3->neg = ! t3->neg;
    d = num_add(t3,t);
    if (d->zero) break;
  }
 num_deref(t2);
 num_deref(t3);
 num_deref(d);
 num_deref(two);
 return(t);
}

static NUM *num_intpart(NUM *n, int minrefs, int noisy)
{
 if (n->zero) return(n);
 if (n->digs < 1)
  { n = num_modify(n,minrefs);
    n->zero = 1;
    return(n);
  }
 if (n->digs > n->exp)
  { if (noisy) pwarn(WARN_FRACDROP,"discarding fractional part");
    n = num_modify(n,minrefs);
    if (n->exp <= 0)
     { n->zero = 1;
     }
    else
     { n->digs = n->exp;
     }
  }
 return(n);
}

static NUM *C_sqrt(int nargs, NUM **args)
{
 if (nargs != 1)
  { printf("usage: sqrt(num)\n");
    return(0);
  }
 if (args[0]->neg)
  { printf("sqrt of a negative number\n");
    return(0);
  }
 return(num_sqrt(args[0]));
}

static NUM *C_int(int nargs, NUM **args)
{
 NUM *n;

 if (nargs != 1)
  { printf("usage: int(num)\n");
    return(0);
  }
 n = num_modify(num_ref(args[0]),2);
 if (n->exp < 1)
  { n->zero = 1;
  }
 else if (n->digs > n->exp)
  { n->digs = n->exp;
  }
 return(n);
}

static NUM *C_gcd(int nargs, NUM **args)
{
 NUM *n;
 NUM *m;
 NUM *t;
 int i;

 if (nargs < 1)
  { printf("usage: gcd(num[,num,...])\n");
    return(0);
  }
 n = args[0];
 args[0] = 0;
 n = num_intpart(n,1,1);
 if (n->zero)
  { printf("gcd: zero argument\n");
    num_deref(n);
    return(0);
  }
 if (n->neg)
  { n = num_modify(n,1);
    n->neg = 0;
  }
 for (i=1;i<nargs;i++)
  { m = args[i];
    args[i] = 0;
    m = num_intpart(m,1,1);
    if (m->zero)
     { printf("gcd: zero argument\n");
       num_deref(n);
       num_deref(m);
       return(0);
     }
    if (m->neg)
     { m = num_modify(m,1);
       m->neg = 0;
     }
    while (! m->zero)
     { t = num_divide(n,m);
       t = num_intpart(t,1,0);
       t = num_multiply(t,m);
       t = num_modify(t,1);
       if (t->neg) abort();
       t->neg = 1;
       t = num_add(n,t);
       num_deref(n);
       n = m;
       m = t;
     }
    num_deref(m);
  }
 return(n);
}

static void dumpnode(NODE *n)
{
 switch (n->type)
  { case NT_CONST:
       printf("<CONST:");
       printnum(n->u.c);
       printf(">");
       break;
    case NT_VAR:
       printf("<VAR:%s>",n->u.var);
       break;
    case NT_HIST:
       printf("<HIST:%d>",n->u.hist);
       break;
    case NT_UNARY:
       printf("<UNARY:");
       switch (n->u.u.op)
	{ case U_PLUS:  printf("PLUS:");  break;
	  case U_MINUS: printf("MINUS:"); break;
	  case U_RECIP: printf("RECIP:"); break;
	}
       dumpnode(n->u.u.arg);
       printf(">");
       break;
    case NT_BINARY:
       printf("<BINARY:");
       switch (n->u.b.op)
	{ case B_PLUS:   printf("PLUS:");   break;
	  case B_MINUS:  printf("MINUS:");  break;
	  case B_TIMES:  printf("TIMES:");  break;
	  case B_DIVIDE: printf("DIVIDE:"); break;
	  case B_ASSIGN: printf("ASSIGN:"); break;
	}
       dumpnode(n->u.b.lhs);
       printf(":");
       dumpnode(n->u.b.rhs);
       printf(">");
       break;
    case NT_FUNCTION:
	{ int i;
	  printf("<FUNCTION:%s[%d]:%d",n->u.f.name,n->u.f.inx,n->u.f.nargs);
	  for (i=0;i<n->u.f.nargs;i++)
	   { printf(":");
	     dumpnode(n->u.f.args[i]);
	   }
	  printf(">");
	}
       break;
  }
}

static NUM *C_BASE(int nargs, NUM **args)
{
 if (nargs != 1)
  { printf("usage: BASE(number)\n");
    return(0);
  }
 return(num_for_int(args[0]->base));
}

static NUM *C_PREC(int nargs, NUM **args)
{
 if (nargs != 1)
  { printf("usage: PREC(number)\n");
    return(0);
  }
 return(num_for_int(args[0]->prec));
}

static NUM *C_EXP(int nargs, NUM **args)
{
 if (nargs != 1)
  { printf("usage: EXP(number)\n");
    return(0);
  }
 return(num_for_int(args[0]->exp));
}

static NUM *C_MANT(int nargs, NUM **args)
{
 NUM *n;

 if (nargs != 1)
  { printf("usage: MANT(number)\n");
    return(0);
  }
 n = num_modify(num_ref(args[0]),2);
 n->exp = 0;
 return(n);
}

static NUM *C_ULP(int nargs, NUM **args)
{
 NUM *n;
 NUM *rv;

 if (nargs != 1)
  { printf("usage: ULP(number)\n");
    return(0);
  }
 n = args[0];
 if (n->zero) return(num_ref(n));
 rv = num_alloc();
 rv->base = n->base;
 rv->digs = 1;
 rv->exp = 1 + n->exp - n->prec;
 rv->mant = mmalloc(1);
 setmant1(rv);
 return(rv);
}

/*
 * Implement log().  We do this by, essentially, binary search: we do a
 *  preliminary loop to find integers Ll and Lh such that base^Ll and
 *  base^Lh bracket N, the number we want the logarithm of.  Then we
 *  loop, doing binary search on the logarithm.  In pseudocode:
 *
 *	set Nl = 1, Nh = base, Ll = 0, Lh = 1
 *	loop until Nl and Nh bracket N:
 *		Nl = Nh
 *		Nh *= base
 *		Ll = Lh
 *		Lh ++
 *	loop
 *		Nt = sqrt(Nl*Nh)
 *		Lt = (Ll+Lh)/2
 *		replace either Nl/Ll or Nh/Lh with Nt/Lt, depending on
 *			whether N is bracketed by Nt/Nh or Nl/Nt
 *
 * In practice, this seems to have typical errors in the tens of ULPs.
 *  I'm not enough of a numerical analyst to work out exactly how the
 *  error is affected by the way we chop Nt.  Perhaps we should keep a
 *  few guard digits?
 */
static NUM *C_log(int nargs, NUM **args)
{
 NUM *num;
 NUM *base;
 int lt1;
 NUM *one;
 NUM *two;
 NUM *t;
 NUM *t2;
 NUM *t3;
 NUM *n_l;
 NUM *n_h;
 NUM *l_l;
 NUM *l_h;

 if (nargs != 2)
  { printf("usage: log(num,base)\n");
    return(0);
  }
 num = args[0];
 base = args[1];
 if (num->base != base->base)
  { mixedbase();
    return(0);
  }
 if (num->neg || num->zero)
  { printf("log: num must be strictly positive\n");
    return(0);
  }
 if (base->neg || base->zero || (base->exp < 1) || ((base->digs == 1) && (base->mant[0] == 1) && (base->exp == 1)))
  { printf("log: base must be > 1\n");
    return(0);
  }
 one = int_num(1,num->base);
 two = int_num(2,num->base);
 if (num->exp < 1)
  { lt1 = 1;
    t = num_divide(one,num);
    num = t;
  }
 else
  { lt1 = 0;
    num_ref(num);
  }
 n_l = num_ref(one);
 n_h = num_ref(base);
 l_l = int_num(0,num->base);
 l_h = num_ref(one);
 while (num_cmp(n_h,num) < 0)
  {
#ifdef LOGARITHM_TRACE
    printf("initial loop:\n");
    printf("  n_l = ");
    printnum(n_l);
    printf("\n  n_h = ");
    printnum(n_h);
    printf("\n  l_l = ");
    printnum(l_l);
    printf("\n  l_h = ");
    printnum(l_h);
    printf("\n");
#endif
    num_deref(n_l);
    n_l = n_h;
    n_h = num_multiply(n_l,base);
    num_deref(l_l);
    l_l = l_h;
    l_h = num_add(l_l,one);
  }
#ifdef LOGARITHM_TRACE
 printf("now have:\n");
 printf("  n_l = ");
 printnum(n_l);
 printf("\n  n_h = ");
 printnum(n_h);
 printf("\n  l_l = ");
 printnum(l_l);
 printf("\n  l_h = ");
 printnum(l_h);
 printf("\n");
#endif
 do <"ret">
  { if (num_cmp(n_h,num) == 0)
     { num_deref(n_l);
       num_deref(n_h);
       num_deref(l_l);
       t = l_h;
#ifdef LOGARITHM_TRACE
       printf("exact power\n");
#endif
       break <"ret">;
     }
    while (1)
     {
#ifdef LOGARITHM_TRACE
       printf("main loop:\n");
       printf("  n_l = ");
       printnum(n_l);
       printf("\n  n_h = ");
       printnum(n_h);
       printf("\n  l_l = ");
       printnum(l_l);
       printf("\n  l_h = ");
       printnum(l_h);
       printf("\n");
#endif
       t2 = num_multiply(n_l,n_h);
       t3 = num_sqrt(t2);
       num_deref(t2);
       t = num_chop(t3,num->prec);
       num_deref(t3);
       t3 = num_add(l_l,l_h);
       t2 = num_divide(t3,two);
       num_deref(t3);
#ifdef LOGARITHM_TRACE
       printf("  t = ");
       printnum(t);
       printf("\n  t2 = ");
       printnum(t2);
       printf("\n");
#endif
       if ( (num_cmp(t,n_l) ? 0 : ((t3=num_ref(l_l)),1)) ||
	    (num_cmp(t,n_h) ? 0 : ((t3=num_ref(l_h)),1)) )
	{ num_deref(n_l);
	  num_deref(n_h);
	  num_deref(l_l);
	  num_deref(l_h);
	  t = t3;
#ifdef LOGARITHM_TRACE
	  printf("sqrt match\n");
#endif
	  break <"ret">;
	}
       switch (num_cmp(t,num))
	{ case -1:
#ifdef LOGARITHM_TRACE
	     printf("cmp -1\n");
#endif
	     num_deref(n_l);
	     n_l = t;
	     num_deref(l_l);
	     l_l = t2;
	     break;
	  case 0:
	     num_deref(n_l);
	     num_deref(n_h);
	     num_deref(l_l);
	     num_deref(l_h);
	     t = t2;
#ifdef LOGARITHM_TRACE
	     printf("cmp 0\n");
#endif
	     break <"ret">;
	  case 1:
#ifdef LOGARITHM_TRACE
	     printf("cmp 1\n");
#endif
	     num_deref(n_h);
	     n_h = t;
	     num_deref(l_h);
	     l_h = t2;
	     break;
	  default:
	     abort();
	     break;
	}
     }
  } while (0);
 num_deref(one);
 num_deref(two);
 num_deref(num);
 if (lt1)
  { t = num_modify(t,1);
    t->neg = ! t->neg;
  }
 t2 = num_chop(t,num->prec);
 num_deref(t);
#ifdef LOGARITHM_TRACE
 printf("returning ");
 printnum(t2);
 printf("\n");
#endif
 return(t2);
}

static NODE *copynode_tweak(NODE *n, NODE *(*fn)(NODE *))
{
 NODE *n2;

 n2 = malloc(sizeof(NODE));
 n2->type = n->type;
 switch (n->type)
  { default: abort(); break;
    case NT_CONST:
       n2->u.c = num_ref(n->u.c);
       break;
    case NT_VAR:
       n2->u.var = strdup(n->u.var);
       break;
    case NT_HIST:
       n2->u.hist = n->u.hist;
       break;
    case NT_UNARY:
       n2->u.u.op = n->u.u.op;
       n2->u.u.arg = copynode_tweak(n->u.u.arg,fn);
       break;
    case NT_BINARY:
       n2->u.b.lhs = copynode_tweak(n->u.b.lhs,fn);
       n2->u.b.op = n->u.b.op;
       n2->u.b.rhs = copynode_tweak(n->u.b.rhs,fn);
       break;
    case NT_FUNCTION:
	{ int i;
	  n2->u.f.name = strdup(n->u.f.name);
	  n2->u.f.inx = -1;
	  n2->u.f.nargs = n->u.f.nargs;
	  n2->u.f.args = malloc(n2->u.f.nargs*sizeof(NODE *));
	  for (i=0;i<n2->u.f.nargs;i++) n2->u.f.args[i] = copynode_tweak(n->u.f.args[i],fn);
	}
       break;
  }
 return((*fn)(n2));
}

static NODE *copynode_lookup(NODE *n)
{
 NODE *foo(NODE *n)
  { if (n->type == NT_FUNCTION)
     { n->u.f.inx = function_lookup(n->u.f.name);
     }
    return(n);
  }

 return(copynode_tweak(n,foo));
}

static NUM *C_solve(int nargs, NODE **args)
{
 NUM *two;
 NUM *rv;
 NUM *l;
 NUM *m;
 NUM *h;
 NUM *f;
 NUM *t;
 NUM *fuzz;
 VAR *v;
 NUM *oldval;
 NODE *fn;
 NODE *avgnode;
 NODE *subnode;
 int preclimit;

 switch (nargs)
  { case 4: case 5: break;
    default:
usage:;
       printf("usage: solve(expression,var,low,high[,fuzz])\n");
       return(0);
       break;
  }
 if (args[1]->type != NT_VAR) goto usage;
 fn = copynode_lookup(args[0]);
 avgnode = 0;
 subnode = 0;
 m = 0;
 f = 0;
 v = 0;
 h = 0;
 l = 0;
 t = 0;
 fuzz = 0;
 two = 0;
 l = evalnode(args[2]);
 if (l == 0)
  {
fail:;
    rv = 0;
done:;
    num_deref(m);
    num_deref(f);
    num_deref(l);
    num_deref(h);
    num_deref(t);
    num_deref(fuzz);
    num_deref(two);
    if (avgnode) node_free(avgnode);
    if (subnode) node_free(subnode);
    if (v)
     { num_deref(v->value);
       v->value = oldval;
     }
    node_free(fn);
    return(rv);
  }
 h = evalnode(args[3]);
 if (h == 0) goto fail;
 if (nargs >= 5)
  { fuzz = evalnode(args[4]);
    if (fuzz == 0) goto fail;
  }
 else
  { fuzz = 0;
  }
 if ((l->base != h->base) || (fuzz && (l->base != fuzz->base)))
  { mixedbase();
    goto fail;
  }
 preclimit = l->prec;
 if (h->prec > preclimit) preclimit = h->prec;
 if (fuzz && (fuzz->prec > preclimit)) preclimit = fuzz->prec;
 two = int_num(2,l->base);
 v = var_by_name(args[1]->u.var,1);
 oldval = v->value;
 v->value = num_ref(l);
 f = evalnode(fn);
 if (f == 0) goto fail;
 if (f->zero)
  { rv = num_ref(l);
    goto done;
  }
 SETREF(v->value,h);
 if (f->neg)
  { num_deref(f);
    f = evalnode(fn);
    if (f == 0) goto fail;
    if (f->zero)
     { rv = num_ref(h);
       goto done;
     }
    if (f->neg)
     { printf("both function values negative\n");
       goto fail;
     }
  }
 else
  { num_deref(f);
    f = evalnode(fn);
    if (f == 0) goto fail;
    if (f->zero)
     { rv = num_ref(h);
       goto done;
     }
    if (! f->neg)
     { printf("both function values positive\n");
       goto fail;
     }
    m = l;
    l = h;
    h = m;
    m = 0;
  }
 avgnode = node_make( NT_BINARY,
		      node_make( NT_BINARY,
				 node_make(NT_CONST,num_ref(two)),
				 B_PLUS,
				 node_make(NT_CONST,num_ref(two)) ),
		      B_DIVIDE,
		      node_make(NT_CONST,num_ref(two)) );
 subnode = node_make( NT_BINARY,
		      node_make(NT_CONST,num_ref(two)),
		      B_MINUS,
		      node_make(NT_CONST,num_ref(two)) );
 while (1)
  { if (fuzz)
     { SETREF(subnode->u.b.lhs->u.c,l);
       SETREF(subnode->u.b.rhs->u.c,h);
       num_deref(f);
       f = evalnode(subnode);
       if (f == 0) goto fail;
       if (f->neg)
	{ f = num_modify(f,1);
	  f->neg = 0;
	}
       if (f->zero)
	{ rv = num_ref(l);
	  goto done;
	}
       SETREF(subnode->u.b.rhs->u.c,f);
       SETREF(subnode->u.b.lhs->u.c,fuzz);
       num_deref(f);
       f = evalnode(subnode);
       if (f == 0) goto fail;
       if (! f->neg)
	{ rv = num_ref(l);
	  goto done;
	}
     }
    SETREF(avgnode->u.b.lhs->u.b.lhs->u.c,l);
    SETREF(avgnode->u.b.lhs->u.b.rhs->u.c,h);
    num_deref(m);
    m = evalnode(avgnode);
    if (m == 0) goto fail;
    if (m->prec > preclimit)
     { if (m->prec > preclimit) m->prec = preclimit;
       if (m->digs > preclimit)
	{ int i;
	  m = num_modify(m,1);
	  for (i=preclimit-1;(i>=0)&&(m->mant[i]==0);i--) ;
	  if (i < 0) m->zero = 1; else m->digs = i + 1;
	}
     }
    SETREF(v->value,m);
    num_deref(f);
    f = evalnode(fn);
    if (f == 0) goto fail;
    if (f->zero)
     { rv = num_ref(m);
       goto done;
     }
    num_deref(t);
    num_ref(m);
    if (f->neg)
     { t = l;
       l = m;
     }
    else
     { t = h;
       h = m;
     }
    if (! fuzz)
     { SETREF(subnode->u.b.lhs->u.c,t);
       SETREF(subnode->u.b.rhs->u.c,m);
       num_deref(f);
       f = evalnode(subnode);
       if (f == 0) goto fail;
       if (f->zero)
	{ rv = num_ref(m);
	  goto done;
	}
     }
  }
}

static NUM *C_const_e(void)
{
 NUM *e;
 NUM *one;
 NUM *n;
 NUM *f;
 NUM *t;
 NUM *s;

 e = int_num(2,defbase);
 n = int_num(2,defbase);
 f = int_num(2,defbase);
 one = int_num(1,defbase);
 while (1)
  { t = num_divide(one,f);
    if (t->exp < -defprec-2) break;
    s = num_add(e,t);
    num_deref(t);
    num_deref(e);
    e = s;
    t = num_add(n,one);
    num_deref(n);
    n = t;
    t = num_multiply(f,n);
    num_deref(f);
    f = t;
  }
 num_deref(t);
 num_deref(one);
 num_deref(n);
 num_deref(f);
 return(e);
}

static NUM *C_const_pi(void)
{
 NUM *a;
 NUM *b;
 NUM *t;
 NUM *x;
 NUM *two;
 NUM *tmp;
 NUM *tmp2;
 NUM *y;
 NUM *d;
 int p;

 p = defprec;
 a = int_num(1,defbase);
 x = num_ref(a);
 two = int_num(2,defbase);
 tmp = num_divide(a,two);
 b = num_sqrt(tmp);
 num_deref(tmp);
 tmp = int_num(4,defbase);
 t = num_divide(a,tmp);
 num_deref(tmp);
 d = 0;
 while (1)
  { /* (y goes live) */
    /* y = a */
    y = a;
    /* a = (a+b)/2 */
    tmp = num_add(a,b);
    tmp2 = num_divide(tmp,two);
    num_deref(tmp);
    a = num_chop(tmp2,p*2);
    num_deref(tmp2);
printf("a=");
printnum(a);
printf("\n");
    /* b = sqrt(b*y) */
    tmp = num_multiply(b,y);
    num_deref(b);
    tmp2 = num_sqrt(tmp);
    num_deref(tmp);
    b = num_chop(tmp2,p*2);
    num_deref(tmp2);
printf("b=");
printnum(b);
printf("\n");
    /* t = t - x*(a-y)^2 */
    y = num_modify(y,1);
    y->neg = ! y->neg;
    tmp2 = num_add(a,y);
    num_deref(y);
    tmp = num_multiply(tmp2,tmp2);
    num_deref(tmp2);
    tmp2 = num_multiply(x,tmp);
    num_deref(tmp);
    tmp2 = num_modify(tmp2,1);
    tmp2->neg = ! tmp2->neg;
    tmp = num_add(t,tmp2);
    num_deref(tmp2);
    num_deref(t);
    t = tmp;
    /* (y goes dead) */
    /* x = 2*x */
    tmp = num_add(x,x);
    num_deref(x);
    x = tmp;
    /* Check a-b */
    tmp = copynum(b);
    tmp->neg = ! tmp->neg;
    num_deref(d);
    d = num_add(a,tmp);
    num_deref(tmp);
printf("d=");
printnum(d);
printf("\n");
    if (d->zero || (d->exp <= (1-defprec)*2)) break;
  }
 tmp = num_add(a,b);
 num_deref(a);
 num_deref(b);
 tmp2 = num_divide(tmp,two);
 num_deref(tmp);
 tmp = num_multiply(tmp2,tmp2);
 num_deref(tmp2);
 tmp2 = num_divide(tmp,t);
 num_deref(tmp);
 tmp = num_chop(tmp2,defprec);
 num_deref(tmp2);
 num_deref(t);
 num_deref(x);
 num_deref(two);
 num_deref(d);
 return(tmp);
}

static NUM *C_const(int nargs, NODE **args)
{
 static struct {
	  const char *name;
	  NUM *(*fn)(void);
	  } consts[] = { { "e", C_const_e },
			 { "pi", C_const_pi },
			 { 0 } };
 int i;

 switch (nargs)
  { case 1: break;
    default:
usage:;
       printf("usage: const(constant-name)\n");
       return(0);
       break;
  }
 if (args[0]->type != NT_VAR) goto usage;
 for (i=0;consts[i].name;i++)
  { if (!strcmp(args[0]->u.var,consts[i].name)) return((*consts[i].fn)());
  }
 printf("unknown constant name `%s'\n",args[0]->u.var);
 return(0);
}

static NUM *C_let(int nargs, NODE **args)
{
 __label__ ret;

 int nv;
 VAR **varv;
 NUM **valv;
 NUM *valtmp;
 NUM *rv;
 int i;

 void fail(void)
  { goto ret;
  }

 void usage(void)
  { printf("usage: let(var,val,[var,val,...],expr)\n");
    fail();
  }

 rv = 0;
 nv = 0;
 if ((nargs < 3) || ! (nargs & 1)) usage();
 nv = (nargs - 1) / 2;
 varv = malloc(nv*sizeof(VAR *));
 valv = malloc(nv*sizeof(NUM *));
 for (i=0;i<nv;i++)
  { varv[i] = 0;
    valv[i] = 0;
  }
 for (i=0;i<nv;i++)
  { if (args[i*2]->type != NT_VAR) usage();
    varv[i] = var_by_name(args[i*2]->u.var,1);
    valv[i] = evalnode(args[(i*2)+1]);
    if (! valv[i]) fail();
  }
 for (i=0;i<nv;i++)
  { valtmp = varv[i]->value;
    varv[i]->value = valv[i];
    valv[i] = valtmp;
  }
 rv = evalnode(args[nv*2]);
 for (i=0;i<nv;i++)
  { valtmp = varv[i]->value;
    varv[i]->value = valv[i];
    valv[i] = valtmp;
  }
ret:;
 for (i=nv-1;i>=0;i--)
  { if (valv[i]) num_deref(valv[i]);
  }
 free(varv);
 free(valv);
 return(rv);
}

static void settokc(int off, char val)
{
 if (off >= tokhave)
  { tokhave = off + 1;
    tokbuf = realloc(tokbuf,tokhave);
  }
 tokbuf[off] = val;
}

static int token_number(int c)
{
 int i;
 int j;
 int base;
 int prec;
 int exp;
 int nd;
 int dotat;
 MANTTYPE *mant;

 toktype = TOK_INUMBER;
 tokinum = 0;
 base = defbase;
 prec = defprec;
 i = 0;
 nd = 0;
 while (1)
  { if (c == '#')
     { settokc(i++,c);
       c = get();
       switch (c)
	{ case 'b': case 'B':
	     base = 2;
	     break;
	  case 'o': case 'O': case 'q': case 'Q':
	     base = 8;
	     break;
	  case 'd': case 'D': case 't': case 'T':
	     base = 10;
	     break;
	  case 'x': case 'X': case 'h': case 'H':
	     base = 16;
	     break;
	  default:
	     if (isdigit(c))
	      { base = c - '0';
		while (1)
		 { settokc(i++,c);
		   c = get();
		   if (! isdigit(c)) break;
		   base = (10 * base) + (c - '0');
		 }
		if (c == '#')
		 { settokc(i++,c);
		   if ((base < MINBASE) || (base > MAXBASE))
		    { printf("invalid base\n");
		      return(0);
		    }
		   break;
		 }
	      }
	     printf("malformed base\n");
	     return(0);
	     break;
	}
       settokc(i++,c);
       c = get();
     }
    else if (c == '_')
     { settokc(i++,c);
       c = get();
       if (isdigit(c))
	{ prec = c - '0';
	  while (1)
	   { settokc(i++,c);
	     c = get();
	     if (! isdigit(c)) break;
	     prec = (10 * prec) + (c - '0');
	   }
	  if (c == '_')
	   { settokc(i++,c);
	     c = get();
	     if ((prec < MINPREC) || (prec > MAXPREC))
	      { printf("invalid precision\n");
		return(0);
	      }
	     continue;
	   }
	}
       printf("malformed precision\n");
       return(0);
       break;
     }
    else
     { break;
     }
  }
 dotat = -1;
 mant = 0;
 while (1)
  { unsigned int v;
    if (c == '.')
     { if (dotat < 0)
	{ dotat = nd;
	  settokc(i++,c);
	  c = get();
	  toktype = TOK_NUMBER;
	  continue;
	}
       else
	{ break;
	}
     }
    else if (c == ':')
     { v = 0;
       while (1)
	{ settokc(i++,c);
	  c = get();
	  if (! isdigit(c)) break;
	  v = (10 * v) + (c - '0');
	}
       if (v >= base)
	{ printf("out-of-range explicit digit value\n");
	  free(mant);
	  return(0);
	}
     }
    else
     { static const char *dv = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
       const char *vp;
       vp = index(dv,c);
       if (! vp) break;
       v = vp - dv;
       if (v >= 36) v -= 26;
       if (v >= base) break;
       settokc(i++,c);
       c = get();
     }
    nd ++;
    mant = mrealloc(mant,nd);
    mant[nd-1] = v;
    tokinum = (tokinum * base) + v;
  }
 if (dotat < 0) dotat = nd;
 exp = 0;
 if (c == '@')
  { int expneg;
    toktype = TOK_NUMBER;
    expneg = 0;
    settokc(i++,c);
    c = get();
    if ((c == '-') || (c == '+'))
     { expneg = (c == '-');
       settokc(i++,c);
       c = get();
     }
    while (1)
     { if (! isdigit(c)) break;
       exp = (10 * exp) + (c - '0');
       settokc(i++,c);
       c = get();
     }
    if (expneg) exp = - exp;
  }
 unget(c);
 if (nd == 0)
  { printf("no digits in number\n");
    free(mant);
    return(0);
  }
 if (toknum) num_deref(toknum);
 toknum = num_alloc();
 toknum->base = base;
 toknum->prec = prec;
 for (j=0;(j<nd)&&(mant[j]==0);j++) ;
 if (j >= nd)
  { toknum->zero = 1;
    return(1);
  }
 exp += dotat - j;
 if (j > 0)
  { nd -= j;
    mcopy(mant+j,mant,nd);
  }
 for (j=nd-1;(j>=0)&&(mant[j]==0);j--) ;
 nd = j + 1;
 toknum->digs = nd;
 toknum->mant = mmalloc(nd);
 if ((nd > prec) && roundnum(mant+prec,base,prec,nd-prec))
  { exp ++;
    setmant1(toknum);
  }
 else
  { mcopy(mant,toknum->mant,nd);
  }
 free(mant);
 if (exp < MINEXP)
  { printf("exponent too small (%d), number taken to be 0\n",exp);
    toknum->zero = 1;
    return(1);
  }
 if (exp > MAXEXP)
  { printf("exponent too large (%d), clipping to @%d\n",exp,MAXEXP);
    exp = MAXEXP;
  }
 toknum->exp = exp;
 return(1);
}

static int token_var(int c)
{
 int i;

 settokc(0,c);
 i = 1;
 while (1)
  { c = get();
    if (! isalnum(c)) break;
    settokc(i++,c);
  }
 unget(c);
 settokc(i,'\0');
 toktype = TOK_VAR;
 return(1);
}

static int token_char(int c)
{
 int i;

 if ((c == ':') || (c == '#') || (c == '_') || (c == '.'))
  { return(token_number(c));
  }
 if (c == '\\')
  { tokinum = 0;
    i = 0;
    while (1)
     { c = get();
       switch (c)
	{ case 'e': tokinum |= TOKF_ECHO;   break;
	  case 'h': tokinum |= TOKF_NOHIST; break;
	  case 'n': tokinum |= TOKF_NONL;   break;
	  case 'q': tokinum |= TOKF_QUIET;  break;
	  default:
	     if (isalnum(c))
	      { fprintf(stderr,"%s: unrecognized flag \\%c ignored\n",__progname,c);
	      }
	     else
	      { unget(c);
		toktype = TOK_FLAGS;
		return(1);
	      }
	     break;
	}
     }
  }
 toklen = 1;
 settokc(0,c);
 settokc(1,'\0');
 toktype = TOK_CHAR;
 return(1);
}

static void token(int geteol)
{
 int c;

 if (untokened)
  { untokened = 0;
    if (geteol || (toktype != TOK_EOL)) return;
  }
 do
  { c = get();
    if (c == EOF)
     { toktype = TOK_EOF;
       return;
     }
    if ((c == '\n') && geteol)
     { toktype = TOK_EOL;
       return;
     }
  } while (isspace(c));
 if (! ( isdigit(c)
	 ? token_number(c)
	 : isalpha(c)
	 ? token_var(c)
	 : token_char(c) ))
  { toktype = TOK_ERR;
  }
}

static void untoken(void)
{
 untokened = 1;
}

static void syscmd_setvar(int *varp, int min, int max, const char *what)
{
 int val;
 int c;

 val = 0;
 while (1)
  { c = get();
    if ((c == '\n') || (c == EOF))
     { unget(c);
       break;
     }
    if (!isdigit(c)) continue;
    val = (val * 10) + (c - '0');
  }
 if ((val < min) || (val > max))
  { printf("%s value out of range [%d..%d]\n",what,min,max);
  }
 else
  { *varp = val;
    printf("%s set to %d.\n",what,val);
  }
}

static void syscmd_help(void)
{
 printf("\
Operators: * (multiplication), / (divison), + (addition), - (subtraction)\n\
           = (assignment), or function calls.\n\
Functions: chop(value,ndig) - chops value to ndig significant digits\n\
           round(value,ndig) - rounds value to ndig significant digits\n\
           cvt(value,base[,prec]) - expresses value in base, to prec\n\
                significant digits; if prec omitted, ?p value used.\n\
");
}

static int syscmd(void)
{
 int c;

 c = get();
 if (c == '?')
  { c = get();
    switch (c)
     { case EOF: unget(EOF); return(1); break;
       case '?': case '\n':
	  printf("?p  %3d (default precision, %d..%d)\n",defprec,MINPREC,MAXPREC);
	  printf("?b  %3d (default base, %d..%d)\n",defbase,MINBASE,MAXBASE);
	  printf("?e- %3d (min -ve printed exponent, %d..0)\n",pminexp,MINEXP);
	  printf("?e+ %3d (min +ve printed exponent, 0..%d)\n",pmaxexp,MAXEXP);
	  printf("?d  %3d (debugging, 0 or 1)\n",debugging);
	  printf("?h (get help)\n");
	  break;
       case 'p':
	  syscmd_setvar(&defprec,MINPREC,MAXPREC,"default precision");
	  break;
       case 'b':
	  syscmd_setvar(&defbase,MINBASE,MAXBASE,"default base");
	  break;
       case 'd':
	  syscmd_setvar(&debugging,0,1,"debugging");
	  break;
       case 'e':
	  c = get();
	  switch (c)
	   { case '-': syscmd_setvar(&pminexp,MINEXP,0,"min -ve printed exp"); break;
	     case '+': syscmd_setvar(&pmaxexp,0,MAXEXP,"min +ve printed exp"); break;
	     default: printf("unknown e subcommand `%c'\n",c); break;
	   }
	  break;
       case 'h':
	  syscmd_help();
	  break;
       default:
	  printf("unknown syscmd `%c'\n",c);
	  break;
     }
    do
     { c = get();
     } while ((c != '\n') && (c != EOF));
    unget('\n');
    return(1);
  }
 unget(c);
 return(0);
}

static NODE *parse_unary(void)
{
 NODE *new;

 token(0);
 switch (toktype)
  { case TOK_EOF:
    case TOK_EOL: /* shut up -Wswitch */
    case TOK_ERR:
    case TOK_FLAGS:
       return(0);
       break;
    case TOK_NUMBER:
    case TOK_INUMBER:
       new = node_make(NT_CONST,toknum);
       toknum = 0;
       break;
    case TOK_VAR:
	{ char *vstr;
	  vstr = strdup(tokbuf);
	  token(1);
	  if ((toktype == TOK_CHAR) && (tokbuf[0] == '('))
	   { new = node_make(NT_FUNCTION,vstr,0);
	     while (1)
	      { NODE *arg;
		arg = parse_top();
		if (! arg)
		 { node_free(new);
		   return(0);
		 }
		new->u.f.args = realloc(new->u.f.args,(new->u.f.nargs+1)*sizeof(NODE *));
		new->u.f.args[new->u.f.nargs++] = arg;
		token(0);
		if ((toktype == TOK_CHAR) && (tokbuf[0] == ')')) break;
		if ((toktype == TOK_CHAR) && (tokbuf[0] == ',')) continue;
		printf("invalid argument list delimiter\n");
		node_free(new);
		return(0);
	      }
	   }
	  else
	   { untoken();
	     new = node_make(NT_VAR,vstr);
	   }
	}
       break;
    case TOK_CHAR:
       if (tokbuf[0] == '$')
	{ int h;
	  int b;
	  b = defbase;
	  defbase = 10;
	  token(0);
	  defbase = b;
	  if ((toktype == TOK_CHAR) && (tokbuf[0] == '$'))
	   { if (exprno > 1)
	      { h = exprno - 1;
	      }
	     else
	      { printf("no previous value available for $$\n");
		return(0);
	      }
	   }
	  else if (toktype == TOK_INUMBER)
	   { h = tokinum;
	     if ((h < 1) || (h >= exprno))
	      { printf("no history value $%d available\n",tokinum);
		return(0);
	      }
	   }
	  else if ((toktype == TOK_CHAR) && (tokbuf[0] == '-'))
	   { token(0);
	     if (toktype != TOK_INUMBER)
	      { untoken();
		tokinum = 1;
	      }
	     h = exprno - tokinum;
	     if ((h < 1) || (h >= exprno))
	      { printf("no history value $%d available\n",-tokinum);
		return(0);
	      }
	   }
	  else
	   { printf("invalid/missing number after $\n");
	     return(0);
	   }
	  new = node_make(NT_HIST,h);
	}
       else if (tokbuf[0] == '(')
	{ new = parse_top();
	  if (! new) return(0);
	  token(0);
	  if ((toktype != TOK_CHAR) || (tokbuf[0] != ')'))
	   { printf("unclosed (\n");
	     node_free(new);
	     return(0);
	   }
	}
       else
	{ UNARYOP op;
	  NODE *rhs;
	  switch (tokbuf[0])
	   { case '+': op = U_PLUS;  break;
	     case '-': op = U_MINUS; break;
	     case '/': op = U_RECIP; break;
	     default:
		printf("unknown operator `%c'\n",tokbuf[0]);
		return(0);
		break;
	   }
	  rhs = parse_unary();
	  if (! rhs) return(0);
	  new = node_make(NT_UNARY,op,rhs);
	}
       break;
  }
 return(new);
}

static NODE *parse_muldiv(void)
{
 NODE *lhs;
 NODE *rhs;
 BINARYOP op;
 NODE *new;

 lhs = parse_unary();
 if (! lhs) return(0);
 while (1)
  { token(1);
    if ((toktype == TOK_CHAR) && (tokbuf[0] == '*'))
     { op = B_TIMES;
     }
    else if ((toktype == TOK_CHAR) && (tokbuf[0] == '/'))
     { op = B_DIVIDE;
     }
    else
     { untoken();
       return(lhs);
     }
    rhs = parse_unary();
    if (! rhs)
     { node_free(lhs);
       return(0);
     }
    new = node_make(NT_BINARY,lhs,op,rhs);
    lhs = new;
  }
}

static NODE *parse_addsub(void)
{
 NODE *lhs;
 NODE *rhs;
 BINARYOP op;
 NODE *new;

 lhs = parse_muldiv();
 if (! lhs) return(0);
 while (1)
  { token(1);
    if ((toktype == TOK_CHAR) && (tokbuf[0] == '+'))
     { op = B_PLUS;
     }
    else if ((toktype == TOK_CHAR) && (tokbuf[0] == '-'))
     { op = B_MINUS;
     }
    else
     { untoken();
       return(lhs);
     }
    rhs = parse_muldiv();
    if (! rhs)
     { node_free(lhs);
       return(0);
     }
    new = node_make(NT_BINARY,lhs,op,rhs);
    lhs = new;
  }
}

static NODE *parse_assign(void)
{
 NODE *lhs;
 NODE *rhs;
 NODE *new;

 lhs = parse_addsub();
 if (! lhs) return(0);
 token(1);
 if ((toktype != TOK_CHAR) || (tokbuf[0] != '='))
  { untoken();
    return(lhs);
  }
 if (lhs->type != NT_VAR)
  { printf("invalid lhs of =\n");
    node_free(lhs);
    return(0);
  }
 rhs = parse_assign();
 if (! rhs)
  { node_free(lhs);
    return(0);
  }
 new = node_make(NT_BINARY,lhs,B_ASSIGN,rhs);
 return(new);
}

static NODE *parse_top(void)
{
 return(parse_assign());
}

static int echoline(void)
{
 int c;

 token(1);
 if ((toktype != TOK_FLAGS) || !(tokinum & TOKF_ECHO))
  { untoken();
    return(0);
  }
 c = get();
 if ((c != '\n') && (c != EOF)) c = get();
 while (1)
  { if ((c == '\n') || (c == EOF)) break;
    putchar(c);
    c = get();
  }
 if (c == '\n') unget('\n');
 if (! (tokinum & TOKF_NONL)) putchar('\n');
 return(1);
}

static NODE *parse(void)
{
 int c;
 NODE *n;

 do
  { c = get();
  } while (isspace(c) && (c != '\n') && (c != EOF));
 if (c == EOF)
  { if (mflag) leakcheck();
    exit(0);
  }
 unget(c);
 if (c == '\n') return(0);
 if (syscmd()) return(0);
 if (echoline()) return(0);
 n = parse_top();
 if (n == 0) return(0);
 token(1);
 if (toktype == TOK_EOL) return(n);
 if ((toktype == TOK_CHAR) && (tokbuf[0] == ';'))
  { lflags |= TOKF_QUIET;
    return(n);
  }
 if (toktype == TOK_FLAGS)
  { lflags |= tokinum;
    return(n);
  }
 untoken();
 return(n);
}

static void execute(NODE *n)
{
 NUM *rv;
 NUMLIST *nl;

 rv = evalnode(n);
 if (! rv) return;
 if (! (lflags & TOKF_QUIET))
  { if (! qflag) printf("$%d = ",exprno);
    printnum(rv);
    if (! (lflags & TOKF_NONL)) printf("\n");
  }
 if (hflag || (lflags & TOKF_NOHIST))
  { num_deref(rv);
  }
 else
  { nl = malloc(sizeof(NUMLIST));
    nl->n = exprno;
    nl->value = rv;
    nl->link = history;
    history = nl;
    exprno ++;
  }
}

static void handleargs(int ac, char **av)
{
 int skip;
 int errs;

 skip = 0;
 errs = 0;
 for (ac--,av++;ac;ac--,av++)
  { if (skip > 0)
     { skip --;
       continue;
     }
    if (**av != '-')
     { fprintf(stderr,"%s: unrecognized argument `%s'\n",__progname,*av);
       errs ++;
       continue;
     }
#if 0
    if (0)
     {
needarg:;
       fprintf(stderr,"%s: %s needs a following argument\n",__progname,*av);
       errs ++;
       continue;
     }
#endif
#define WANTARG() do { if (++skip >= ac) goto needarg; } while (0)
    if (!strcmp(*av,"-q"))
     { qflag = 1;
       continue;
     }
    if (!strcmp(*av,"-Q"))
     { qflag = 0;
       continue;
     }
    if (!strcmp(*av,"-h"))
     { hflag = 1;
       continue;
     }
    if (!strcmp(*av,"-H"))
     { hflag = 0;
       continue;
     }
    if (!strcmp(*av,"-m"))
     { mflag = 1;
       continue;
     }
#undef WANTARG
    fprintf(stderr,"%s: unrecognized option `%s'\n",__progname,*av);
    errs ++;
  }
 if (errs) exit(1);
}

int main(int, char **);
int main(int ac, char **av)
{
 handleargs(ac,av);
 init();
 if (qflag) warned = ~0U;
 while (1)
  { NODE *n;
    if (!qflag && !(lflags & TOKF_QUIET))
     { if (hflag) printf("> "); else printf("%d> ",exprno);
     }
    lflags = 0;
    n = parse();
    if (! (lflags & TOKF_QUIET)) warned = 0;
    if (n)
     { if (debugging)
	{ dumpnode(n);
	  printf("\n");
	}
       execute(n);
       node_free(n);
     }
    else
     { flushnl();
     }
    if (mflag && hflag) leakcheck();
  }
}
