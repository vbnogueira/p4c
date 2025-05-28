/*
 * Arithmetic code generation algorithm test program.
 *
 * This is designed to test the arithmetic implementation generation.
 *  There are seven kinds of arithmetic we handle:
 *
 *	add	Basic addition
 *	sub	Basic subtraction
 *	mul	Basic multiplication
 *	bxsmul	Big x small multiplication
 *	neg	Negation
 *	addsat	Saturating addition
 *	subsat	Saturating subtraction
 *
 *  Each of these can be characterized by a bit-width (for bxsmul, this
 *  describes the large value).  Some of these, perhaps most notably
 *  multiplication, could probably benefit from recognizing when
 *  operands are diffrent widths cast to the same width to satisfy P4's
 *  restrictions.  For example,
 *
 *	bit<94> a;
 *	bit<78> b;
 *	bit<128> c;
 *	...
 *	c = (bit<128>)a * (bit<128>)b;
 *
 *  could benefit from recognizing that the top 34 bits of one operand
 *  and the top 50 bits of the other will always be zero and using an
 *  implementation customized for 78 x 94 -> 128 multiplication.  That
 *  is for future work.
 *
 * In contrast to the shift tester, which does exhaustive testing
 *  against a "slow" version which I have much more confidence in, this
 *  does not do exhaustive testing.  This is because the generated code
 *  is simple enough that the compare-against code would be
 *  approximately the same as the code to be tested, rendering such
 *  testing pointless.  Instead, this takes an operation name and a
 *  bitwidth on the command line and generates/runs a test program for
 *  that operation on numbers with that bitwidth.  You can then
 *  hand-check the results or compare them against a large-number
 *  arithmetic program's output or whatever is appropriate in your
 *  case.
 *
 * Randomness for the test programs comes from random(), seeded with
 *  time(0)^getpid().  Not great, but good enough for these purposes,
 *  and much faster than things like reading /dev/urandom.
 *
 * Usage: $0 opname widthargs [progargs]
 *
 * opname is one of the operation names, as listed above: add, sub,
 *  mul, bxsmul, neg, addsat, or subsat.  widthargs is one or two args,
 *  depending on opname: for add, sub, mul, and neg, widthargs is one
 *  number, the bit-width of the operand(s).  For bxsmul, widthargs is
 *  two args, first the bit-width of the large operand and second the
 *  small constant (which must fit into 52 bits).  For addsat and
 *  subsat, widthargs is two args, first the bit-width of the operands
 *  and second either "u" or "s" according as the large numbers are
 *  unsigned or signed.
 *
 * If no progargs are given, ie, if the arglist ends immediately after
 *  the last of the widthargs, the test program is generated and
 *  nothing more happens.  If any progargs are given, the test program
 *  is run with those args.  (It is up to the test program to check
 *  that the args make sense for it; if they are nonsense, the error
 *  will come from the test program, not the testing driver.)  The test
 *  program can, of course, be run manually in any case.
 *
 * Test program arguments, by operation, where W is the bit-width
 *  specified (which must be > 64).
 *
 *	add	Two numbers of bit-width W.
 *	sub	Two numbers of bit-width W.
 *	mul	Two numbers of bit-width W.
 *	bxsmul	One number of bit-width W.
 *	neg	One number of bit-width W.
 *	addsat	Two numbers of bit-width W.
 *	subsat	Two numbers of bit-width W.
 *
 *  Numbers "of bit-width W" are padded with 0 bits on the
 *  most-significant end, if necessary.  It is an error for an argument
 *  number to have more bits than listed above.  The operation result
 *  is printed on stdout.  (For bxsmul, the small multiplier is given
 *  at test specification time, not at test-program run time.)
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
//#include <signal.h>
#include <strings.h>
#include <sys/wait.h>
#include <sys/socket.h>

extern const char *__progname;

typedef enum {
	  WR_ADD = 1,
	  WR_SUB,
	  WR_MUL,
	  WR_BXSMUL,
	  WR_NEG,
	  WR_ADDSAT,
	  WR_SUBSAT,
	  } WRTYPE;

typedef struct builder BUILDER;
typedef struct width_rec WIDTH_REC;
typedef unsigned char bool;

struct builder {
  void (*append)(const char *);
  void (*appendFormat)(const char *, ...);
  void (*newline)(void);
  } ;

struct width_rec {
  WRTYPE type;
  const char *text;
  union {
    struct {
      int w;
      } arith;
    struct {
      int bw;
      long long int sv;
      } bxsmul;
    struct {
      int w;
      bool issigned;
      } sarith;
    } ;
  } ;

static FILE *bf;
static char **progargs;
static int prognargs;

#define assert(test) do { if (! (test)) assert_failed(__LINE__,#test); } while (0)

static void assert_failed(int lno, const char *txt)
{
 fprintf(stderr,"assert() failed, line %d: %s\n",lno,txt);
 exit(1);
}

static void append_type_for_width(BUILDER *bld, int w)
{
 if (w <= 64) bld->append("u64"); else bld->appendFormat("struct internal_bit_%d",w);
}

#include "../../gen-code/arithmetic.c"

static void builder_append(const char *s)
{
 fputs(s,bf);
}

static void builder_appendFormat(const char *fmt, ...)
{
 va_list ap;

 va_start(ap,fmt);
 vfprintf(bf,fmt,ap);
 va_end(ap);
}

static void builder_newline(void)
{
 putc('\n',bf);
}

static BUILDER builder = { &builder_append,
			   &builder_appendFormat,
			   &builder_newline, };


static void usage(void)
{
 fprintf(stderr,"Usage: %s opname widthargs [args]\n",__progname);
 exit(1);
}

static void args_arith(int *acp, char ***avp, WIDTH_REC *wr, WRTYPE t)
{
 if (acp[0] < 2)
  { fprintf(stderr,"%s: too few args for %s\n",__progname,avp[0][0]);
    exit(1);
  }
 wr->type = t;
 wr->arith.w = atoi(avp[0][1]);
 if (wr->arith.w <= 64)
  { fprintf(stderr,"%s: width (%d) must be > 64\n",__progname,wr->arith.w);
    exit(1);
  }
 acp[0] -= 2;
 avp[0] += 2;
}

static void args_bxsmul(int *acp, char ***avp, WIDTH_REC *wr)
{
 if (acp[0] < 3)
  { fprintf(stderr,"%s: too few args for %s\n",__progname,avp[0][0]);
    exit(1);
  }
 wr->type = WR_BXSMUL;
 wr->bxsmul.bw = atoi(avp[0][1]);
 wr->bxsmul.sv = strtoll(avp[0][2],0,0);
 if (wr->bxsmul.bw <= 64)
  { fprintf(stderr,"%s: width (%d) must be > 64\n",__progname,wr->bxsmul.bw);
    exit(1);
  }
 if ((wr->bxsmul.sv < -(1LL<<51)) || (wr->bxsmul.sv >= (1LL<<51)))
  { fprintf(stderr,"%s: multiplier (%lld) must fit into 52 bits\n",__progname,wr->bxsmul.sv);
    exit(1);
  }
 acp[0] -= 3;
 avp[0] += 3;
}

static void args_sat(int *acp, char ***avp, WIDTH_REC *wr, WRTYPE t)
{
 if (acp[0] < 3)
  { fprintf(stderr,"%s: too few args for %s\n",__progname,avp[0][0]);
    exit(1);
  }
 wr->type = t;
 wr->sarith.w = atoi(avp[0][1]);
 if (!strcmp(avp[0][2],"u"))
  { wr->sarith.issigned = 0;
  }
 else if (!strcmp(avp[0][2],"s"))
  { wr->sarith.issigned = 1;
  }
 else
  { fprintf(stderr,"%s: second arg (%s) must be u or s\n",__progname,avp[0][2]);
    exit(1);
  }
 if (wr->sarith.w <= 64)
  { fprintf(stderr,"%s: width (%d) must be > 64\n",__progname,wr->sarith.w);
    exit(1);
  }
 acp[0] -= 3;
 avp[0] += 3;
}

static void setup(void)
{
 if (chdir("foo") < 0)
  { fprintf(stderr,"chdir foo: %s\n",strerror(errno));
    exit(1);
  }
}

static void run_cmd(const char *exe, ...)
{
 va_list ap;
 int xp[2];
 pid_t kid;
 int na;
 const char *arg;
 const char **av;
 int i;
 int e;

 va_start(ap,exe);
 for (na=1;va_arg(ap,const char *);na++) ;
 va_end(ap);
 av = malloc(na*sizeof(const char *));
 i = 0;
 va_start(ap,exe);
 while (1)
  { arg = va_arg(ap,const char *);
    if (i >= na) abort();
    av[i++] = arg;
    if (! arg) break;
  }
 va_end(ap);
 if (i != na) abort();
 if (socketpair(AF_LOCAL,SOCK_STREAM,0,&xp[0]) < 0)
  { fprintf(stderr,"socketpair: %s\n",strerror(errno));
    exit(1);
  }
 fflush(0);
 kid = fork();
 if (kid == 0)
  { close(xp[0]);
    fcntl(xp[1],F_SETFD,1);
    execv(exe,(const void *)av);
    e = errno;
    write(xp[1],&e,sizeof(int));
    _exit(0);
  }
 close(xp[1]);
 i = recv(xp[0],&e,sizeof(int),MSG_WAITALL);
 if (i == sizeof(int))
  { fprintf(stderr,"can't exec %s: %s\n",exe,strerror(e));
    exit(1);
  }
 if (i != 0)
  { fprintf(stderr,"exec protocol error: wanted %d or 0, got %d\n",(int)sizeof(int),i);
    exit(1);
  }
 close(xp[0]);
 while (1)
  { if (waitpid(kid,&e,WUNTRACED) < 0)
     { if (errno == EINTR) continue;
       fprintf(stderr,"wait for %d for %s: %s\n",(int)kid,exe,strerror(errno));
       exit(1);
     }
    if (WIFSTOPPED(e))
     { kill(kid,SIGCONT);
       continue;
     }
    if (WIFSIGNALED(e))
     { fprintf(stderr,"%d (%s) died on signal %d\n",(int)kid,exe,WTERMSIG(e));
       exit(1);
     }
    if (! WIFEXITED(e))
     { fprintf(stderr,"incomprehensible process status %d\n",e);
       exit(1);
     }
    if (WEXITSTATUS(e))
     { fprintf(stderr,"%d (%s) exited with code %d\n",(int)kid,exe,WEXITSTATUS(e));
       exit(1);
     }
    break;
  }
 free(av);
}

static void gen_arg_data(int bits)
{
 int bytes;

 bytes = (bits + 7) >> 3;
 fprintf(bf,"\n");
 fprintf(bf,"static void arg_data(struct internal_bit_%d *v, const char *s0)\n",bits);
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," const char *s;\n");
 fprintf(bf," unsigned int a;\n");
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," bzero(&BITS(*v)[0],%d);\n",bytes);
 // Can't just use strto* - numbers (usually) are too big
 fprintf(bf," s = s0;\n");
 fprintf(bf," while (*s)\n");
 fprintf(bf,"  { switch (*s)\n"/*}*/);
 fprintf(bf,"     { case '0': a = 0 << 8; break;\n"/*}*/);
 fprintf(bf,"       case '1': a = 1 << 8; break;\n");
 fprintf(bf,"       case '2': a = 2 << 8; break;\n");
 fprintf(bf,"       case '3': a = 3 << 8; break;\n");
 fprintf(bf,"       case '4': a = 4 << 8; break;\n");
 fprintf(bf,"       case '5': a = 5 << 8; break;\n");
 fprintf(bf,"       case '6': a = 6 << 8; break;\n");
 fprintf(bf,"       case '7': a = 7 << 8; break;\n");
 fprintf(bf,"       case '8': a = 8 << 8; break;\n");
 fprintf(bf,"       case '9': a = 9 << 8; break;\n");
 fprintf(bf,"       case 'a': case 'A': a = 10 << 8; break;\n");
 fprintf(bf,"       case 'b': case 'B': a = 11 << 8; break;\n");
 fprintf(bf,"       case 'c': case 'C': a = 12 << 8; break;\n");
 fprintf(bf,"       case 'd': case 'D': a = 13 << 8; break;\n");
 fprintf(bf,"       case 'e': case 'E': a = 14 << 8; break;\n");
 fprintf(bf,"       case 'f': case 'F': a = 15 << 8; break;\n");
 fprintf(bf,"       default:\n");
 fprintf(bf,"\t  fprintf(stderr,\"bad digit %%c in argument\\n\",*s);\n");
 fprintf(bf,"\t  exit(1);\n");
 fprintf(bf,"\t  break;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    for (i=%d;i>=0;i--)\n",bytes-1);
 fprintf(bf,"     { a = (a >> 8) | BITS(*v)[i] << 4;\n"/*}*/);
 fprintf(bf,"       BITS(*v)[i] = a & 255;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    s ++;\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," if ((a > 255)");
 if (bits & 7) fprintf(bf," || (BITS(*v)[0] & %d)",(255<<(bits&7))&255);
 fprintf(bf,")\n");
 fprintf(bf,"  { fprintf(stderr,\"%%s: argument `%%s' overflows %d bits\\n\",__progname,s0);\n"/*}*/,bits);
 fprintf(bf,"    exit(1);\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf,/*{*/"}\n");
}

static void gen_twobig_tester(const char *optext, int bits)
{
 int bytes;

 assert(bits>64);
 bytes = (bits + 7) >> 3;
 fprintf(bf,"\n");
 fprintf(bf,"static const char *arg1 = 0;\n");
 fprintf(bf,"static const char *arg2 = 0;\n");
 fprintf(bf,"unsigned int rseed = 0;\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void handleargs(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," int skip;\n");
 fprintf(bf," int errs;\n");
 fprintf(bf,"\n");
 fprintf(bf," skip = 0;\n");
 fprintf(bf," errs = 0;\n");
 fprintf(bf," for (ac--,av++;ac>0;ac--,av++)\n");
 fprintf(bf,"  { if (skip > 0)\n"/*}*/);
 fprintf(bf,"     { skip --;\n"/*}*/);
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (**av != '-')\n");
 fprintf(bf,"     { if (! arg1)\n"/*}*/);
 fprintf(bf,"	{ arg1 = *av;\n"/*}*/);
 fprintf(bf,/*{*/"	}\n");
 fprintf(bf,"       else if (! arg2)\n");
 fprintf(bf,"	{ arg2 = *av;\n"/*}*/);
 fprintf(bf,/*{*/"	}\n");
 fprintf(bf,"       else\n");
 fprintf(bf,"	{ fprintf(stderr,\"%%s: stray argument `%%s'\\n\",__progname,*av);\n"/*}*/);
 fprintf(bf,"	  errs = 1;\n");
 fprintf(bf,/*{*/"	}\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (0)\n");
 fprintf(bf,"     {\n"/*}*/);
 fprintf(bf,"needarg:;\n");
 fprintf(bf,"       fprintf(stderr,\"%%s: %%s needs a following argument\\n\",__progname,*av);\n");
 fprintf(bf,"       errs = 1;\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"#define WANTARG() do { if (++skip >= ac) goto needarg; } while (0)\n");
 fprintf(bf,"    if (!strcmp(*av,\"-seed\"))\n");
 fprintf(bf,"     { WANTARG();\n"/*}*/);
 fprintf(bf,"       rseed = strtol(av[skip],0,0);\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (!strcmp(*av,\"-arg1\"))\n");
 fprintf(bf,"     { WANTARG();\n"/*}*/);
 fprintf(bf,"       arg1 = av[skip];\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (!strcmp(*av,\"-arg2\"))\n");
 fprintf(bf,"     { WANTARG();\n"/*}*/);
 fprintf(bf,"       arg2 = av[skip];\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"#undef WANTARG\n");
 fprintf(bf,"    fprintf(stderr,\"%%s: unrecognized option `%%s'\\n\",__progname,*av);\n");
 fprintf(bf,"    errs = 1;\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," if (errs) exit(1);\n");
 fprintf(bf,/*{*/"}\n");
 gen_arg_data(bits);
 fprintf(bf,"\n");
 fprintf(bf,"int main(int, char **);\n");
 fprintf(bf,"int main(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," ");
 append_type_for_width(&builder,bits);
 fprintf(bf," lhs;\n");
 fprintf(bf," ");
 append_type_for_width(&builder,bits);
 fprintf(bf," rhs;\n");
 fprintf(bf," ");
 append_type_for_width(&builder,bits);
 fprintf(bf," res;\n");
 fprintf(bf," unsigned char guard_res_b[GUARDSIZE];\n");
 fprintf(bf," unsigned char guard_res_a[GUARDSIZE];\n");
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," handleargs(ac,av);\n");
 fprintf(bf," if (!arg1 || !arg2)\n");
 fprintf(bf,"  { fprintf(stderr,\"%%s: need two numbers as arguments\\n\",__progname);\n"/*}*/);
 fprintf(bf,"    exit(1);\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," if (! rseed) rseed = time(0) ^ getpid();\n");
 fprintf(bf," srandom(rseed);\n");
 fprintf(bf," printf(\"-seed %%u\\n\",rseed);\n");
#ifndef __linux__
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)1);\n");
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)3);\n");
#endif
 fprintf(bf," random_data(&BITS(lhs)[-GUARDSIZE],GUARDSIZE);\n");
 fprintf(bf," random_data(&BITS(lhs)[%u],GUARDSIZE);\n",bytes);
 fprintf(bf," random_data(&BITS(rhs)[-GUARDSIZE],GUARDSIZE);\n");
 fprintf(bf," random_data(&BITS(rhs)[%d],GUARDSIZE);\n",bytes);
 fprintf(bf," arg_data(&lhs,arg1);\n");
 fprintf(bf," arg_data(&rhs,arg2);\n");
 fprintf(bf," res = %s_%d(lhs,rhs,&guard_res_b[0],&guard_res_a[0]);\n",optext,bits);
 fprintf(bf," if (bcmp(&BITS(res)[-GUARDSIZE],&guard_res_b[0],GUARDSIZE)) guardfail(\"before\",res,&guard_res_b[0]);\n");
 fprintf(bf," if (bcmp(&BITS(res)[%d],&guard_res_a[0],GUARDSIZE)) guardfail(\"after\",res,&guard_res_a[0]);\n",bytes);
 fprintf(bf," dump_bytes(stdout,&BITS(res)[0],%d);\n",bytes);
 fprintf(bf," printf(\"\\n\");\n");
 fprintf(bf,/*{*/"}\n");
}

static void gen_onebig_tester(int bits, void (*gen_call)(void *), void *arg)
{
 int bytes;

 assert(bits>64);
 bytes = (bits + 7) >> 3;
 fprintf(bf,"\n");
 fprintf(bf,"static const char *num = 0;\n");
 fprintf(bf,"unsigned int rseed = 0;\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void handleargs(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," int skip;\n");
 fprintf(bf," int errs;\n");
 fprintf(bf,"\n");
 fprintf(bf," skip = 0;\n");
 fprintf(bf," errs = 0;\n");
 fprintf(bf," for (ac--,av++;ac>0;ac--,av++)\n");
 fprintf(bf,"  { if (skip > 0)\n"/*}*/);
 fprintf(bf,"     { skip --;\n"/*}*/);
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (**av != '-')\n");
 fprintf(bf,"     { if (! num)\n"/*}*/);
 fprintf(bf,"	{ num = *av;\n"/*}*/);
 fprintf(bf,/*{*/"	}\n");
 fprintf(bf,"       else\n");
 fprintf(bf,"	{ fprintf(stderr,\"%%s: stray argument `%%s'\\n\",__progname,*av);\n"/*}*/);
 fprintf(bf,"	  errs = 1;\n");
 fprintf(bf,/*{*/"	}\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (0)\n");
 fprintf(bf,"     {\n"/*}*/);
 fprintf(bf,"needarg:;\n");
 fprintf(bf,"       fprintf(stderr,\"%%s: %%s needs a following argument\\n\",__progname,*av);\n");
 fprintf(bf,"       errs = 1;\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"#define WANTARG() do { if (++skip >= ac) goto needarg; } while (0)\n");
 fprintf(bf,"    if (!strcmp(*av,\"-seed\"))\n");
 fprintf(bf,"     { WANTARG();\n"/*}*/);
 fprintf(bf,"       rseed = strtol(av[skip],0,0);\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"    if (!strcmp(*av,\"-arg\"))\n");
 fprintf(bf,"     { WANTARG();\n"/*}*/);
 fprintf(bf,"       num = av[skip];\n");
 fprintf(bf,"       continue;\n");
 fprintf(bf,/*{*/"     }\n");
 fprintf(bf,"#undef WANTARG\n");
 fprintf(bf,"    fprintf(stderr,\"%%s: unrecognized option `%%s'\\n\",__progname,*av);\n");
 fprintf(bf,"    errs = 1;\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," if (errs) exit(1);\n");
 fprintf(bf,/*{*/"}\n");
 gen_arg_data(bits);
 fprintf(bf,"\n");
 fprintf(bf,"int main(int, char **);\n");
 fprintf(bf,"int main(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," ");
 append_type_for_width(&builder,bits);
 fprintf(bf," arg;\n");
 fprintf(bf," ");
 append_type_for_width(&builder,bits);
 fprintf(bf," res;\n");
 fprintf(bf," unsigned char guard_res_b[GUARDSIZE];\n");
 fprintf(bf," unsigned char guard_res_a[GUARDSIZE];\n");
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," handleargs(ac,av);\n");
 fprintf(bf," if (! num)\n");
 fprintf(bf,"  { fprintf(stderr,\"%%s: need an argument number\\n\",__progname);\n"/*}*/);
 fprintf(bf,"    exit(1);\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," if (! rseed) rseed = time(0) ^ getpid();\n");
 fprintf(bf," srandom(rseed);\n");
 fprintf(bf," printf(\"-seed %%u\\n\",rseed);\n");
#ifndef __linux__
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)1);\n");
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)3);\n");
#endif
 fprintf(bf," random_data(&BITS(arg)[-GUARDSIZE],GUARDSIZE);\n");
 fprintf(bf," random_data(&BITS(arg)[%u],GUARDSIZE);\n",bytes);
 fprintf(bf," arg_data(&arg,num);\n");
 fprintf(bf," res = ");
 (*gen_call)(arg);
 fprintf(bf,"(arg,&guard_res_b[0],&guard_res_a[0]);\n");
 fprintf(bf," if (bcmp(&BITS(res)[-GUARDSIZE],&guard_res_b[0],GUARDSIZE)) guardfail(\"before\",res,&guard_res_b[0]);\n");
 fprintf(bf," if (bcmp(&BITS(res)[%d],&guard_res_a[0],GUARDSIZE)) guardfail(\"after\",res,&guard_res_a[0]);\n",bytes);
 fprintf(bf," dump_bytes(stdout,&BITS(res)[0],%d);\n",bytes);
 fprintf(bf," printf(\"\\n\");\n");
 fprintf(bf,/*{*/"}\n");
}

static void gen_bxsmul_call(void *wrv)
{
 WIDTH_REC *wr;

 wr = wrv;
 fprintf(bf,"bxsmul_%d_%lld",wr->bxsmul.bw,wr->bxsmul.sv);
}

static void gen_onebig_call(void *wrv)
{
 WIDTH_REC *wr;

 wr = wrv;
 fprintf(bf,"%s_%d",wr->text,wr->arith.w);
}

static void gen_tester(WIDTH_REC *wr)
{
 int bits;
 int bytes;

 bf = fopen("foo.c","w");
 if (! bf)
  { fprintf(stderr,"open foo.c: %s\n",strerror(errno));
    exit(1);
  }
 switch (wr->type)
  { case WR_ADD:
    case WR_SUB:
    case WR_MUL:
    case WR_NEG:
       bits = wr->arith.w;
       break;
    case WR_BXSMUL:
       bits = wr->bxsmul.bw;
       break;
    case WR_ADDSAT:
    case WR_SUBSAT:
       bits = wr->sarith.w;
       break;
    default:
       abort();
       break;
  }
 bytes = (bits + 7) >> 3;
 fprintf(bf,"#include <time.h>\n");
 fprintf(bf,"#include <stdio.h>\n");
 fprintf(bf,"#include <unistd.h>\n");
 fprintf(bf,"#include <stdlib.h>\n");
 fprintf(bf,"#include <string.h>\n");
 fprintf(bf,"#include <strings.h>\n");
 fprintf(bf,"\n");
 fprintf(bf,"extern const char *__progname;\n");
 fprintf(bf,"\n");
 fprintf(bf,"#include \"types.h\"\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void copyguards(struct internal_bit_%d *to, struct internal_bit_%d from)\n",bits,bits);
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," bcopy(&BITS(from)[-GUARDSIZE],&BITS(*to)[-GUARDSIZE],GUARDSIZE);\n");
 fprintf(bf," bcopy(&BITS(from)[%u],&BITS(*to)[%u],GUARDSIZE);\n",bytes,bytes);
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
 fprintf(bf,"#define COPYGUARDS(t,f) copyguards(&(t),(f))\n");
 fprintf(bf,"#define GUARDARGS , u8 *guard_arg_b, u8 *guard_arg_a\n");
 fprintf(bf,"#define SETGUARDS(x) do { \\\n"/*}*/);
 fprintf(bf,"\trandom_data(guard_arg_b,GUARDSIZE);\\\n");
 fprintf(bf,"\tbcopy(guard_arg_b,&BITS(x)[-GUARDSIZE],GUARDSIZE);\\\n");
 fprintf(bf,"\trandom_data(guard_arg_a,GUARDSIZE);\\\n");
 fprintf(bf,"\tbcopy(guard_arg_a,&BITS(x)[%d],GUARDSIZE);\\\n",bytes);
 fprintf(bf,/*{*/"\t} while (0)\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void random_data(u8 *p, int n)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," for (;n>0;n--) *p++ = (random() >> 22) & 255;\n");
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void dump_bytes(FILE *to, const u8 *p, int n)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," for (;n>0;n--) fprintf(to,\"%%02x\",*p++);\n");
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void guardfail(const char *which, struct internal_bit_%d val, const u8 *expected)\n",bits);
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," printf(\"%%s guard fail\\n\",which);\n");
 fprintf(bf," printf(\"value = <\");\n");
 fprintf(bf," dump_bytes(stdout,&BITS(val)[-GUARDSIZE],GUARDSIZE);\n");
 fprintf(bf," printf(\">\");\n");
 fprintf(bf," dump_bytes(stdout,&BITS(val)[0],%d);\n",bytes);
 fprintf(bf," printf(\"<\");\n");
 fprintf(bf," dump_bytes(stdout,&BITS(val)[%d],GUARDSIZE);\n",bytes);
 fprintf(bf," printf(\">\\n\");\n");
 fprintf(bf," printf(\"expected = \");\n");
 fprintf(bf," dump_bytes(stdout,expected,GUARDSIZE);\n");
 fprintf(bf," printf(\"\\n\");\n");
 fprintf(bf," exit(1);\n");
 fprintf(bf,/*{*/"}\n");
 switch (wr->type)
  { case WR_ADD:
       gen_add(&builder,wr);
       gen_twobig_tester(wr->text,wr->arith.w);
       break;
    case WR_SUB:
       gen_sub(&builder,wr);
       gen_twobig_tester(wr->text,wr->arith.w);
       break;
    case WR_MUL:
       gen_mul(&builder,wr);
       gen_twobig_tester(wr->text,wr->arith.w);
       break;
    case WR_BXSMUL:
       gen_bxsmul(&builder,wr);
       gen_onebig_tester(wr->bxsmul.bw,&gen_bxsmul_call,wr);
       break;
    case WR_NEG:
       gen_neg(&builder,wr);
       gen_onebig_tester(wr->arith.w,&gen_onebig_call,wr);
       break;
    case WR_ADDSAT:
       gen_addsat(&builder,wr);
       gen_twobig_tester(wr->text,wr->sarith.w);
       break;
    case WR_SUBSAT:
       gen_subsat(&builder,wr);
       gen_twobig_tester(wr->text,wr->sarith.w);
       break;
    default:
       abort();
       break;
  }
 fclose(bf);
}

static void compile_tester(void)
{
#ifdef __linux__
 run_cmd("/bin/cc","/bin/cc","-g","-o","foo","foo.c","-I../..",(const char *)0);
#else
 run_cmd("/usr/bin/cc","cc","-g","-o","foo","foo.c","-I../..",(const char *)0);
#endif
}

static void run_tester(void)
{
 (void)progargs;
 // run_cmd("./foo","foo",(const char *)0);
}

int main(int, char **);
int main(int ac, char **av)
{
 WIDTH_REC wr;

 if (ac < 3) usage();
 ac --;
 av ++;
 wr.text = av[0];
 if (! strcmp(av[0],"add"))
  { args_arith(&ac,&av,&wr,WR_ADD);
  }
 else if (! strcmp(av[0],"sub"))
  { args_arith(&ac,&av,&wr,WR_SUB);
  }
 else if (! strcmp(av[0],"mul"))
  { args_arith(&ac,&av,&wr,WR_MUL);
  }
 else if (! strcmp(av[0],"neg"))
  { args_arith(&ac,&av,&wr,WR_NEG);
  }
 else if (! strcmp(av[0],"bxsmul"))
  { args_bxsmul(&ac,&av,&wr);
  }
 else if (! strcmp(av[0],"addsat"))
  { args_sat(&ac,&av,&wr,WR_ADDSAT);
  }
 else if (! strcmp(av[0],"subsat"))
  { args_sat(&ac,&av,&wr,WR_SUBSAT);
  }
 else
  { fprintf(stderr,"%s: unrecognized opname `%s'\n",__progname,av[0]);
    usage();
  }
 setup();
 gen_tester(&wr);
 compile_tester();
 if (prognargs > 0) run_tester();
 return(0);
}
