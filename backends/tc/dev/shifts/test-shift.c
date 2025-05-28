/*
 * Shift code generation algorithm test program.
 *
 * This is designed to test the shift implementation generation.  There
 *  are six kinds of shifts of interest:
 *
 *	shlc	Left shift by a constant
 *	shrlc	Logical right shift by a constant
 *	shrac	Arithmetic right shift by a constant
 *	shlx	Left shift by a run-time value
 *	shrlx	Logical right shift by a run-time value
 *	shrax	Arithmetic right shift by a run-time value
 *
 *  Each of these can be characterized by two values.  One is the
 *  bitwidth of the value to be shifted.  For the "by a constant"
 *  forms, the other is the number of bits to shift by; for the "by a
 *  run-time value" forms, this is the number of bits in that run-time
 *  value.
 *
 * In each case, this program runs the shift-code generation code.  It
 *  also generates a test program surrounding it.  The test program
 *  runs a bunch of random test cases (the count of them comes from the
 *  TESTCASES #define, below).  For each test case, randomly-generated
 *  values are fed into the algorithm.  The output is checked against a
 *  slow version which generates its output by handling each bit
 *  separately; this is much slower but I'm far more confident it's
 *  correct.  If there is any discrepancy, the tester program prints
 *  details and exits.  (If the test program fails to compile, the
 *  tester also exits.)
 *
 * As mentioned above, shift algorithms are characterized by two
 *  values: the value bitwidth and another value.  These are called,
 *  here, L and R, respectively.  For some (L,R) pairs, a given kind of
 *  shift may not make sense, or may not use this algorithm; for
 *  example, shifts by a constant where R >= L get short-circuited
 *  before the shift code generation algorithms matter.  Similarly,
 *  run-time values with 0 bits don't exist, so the run-time value
 *  shift tests don't run for R=0.  And if L and R are both <=64, none
 *  of the tests are needed, since such shifts are inlined.  The tester
 *  program enumerates all pairs (L,R) for L in [1..128] and R in
 *  [0..127].  Then, for each test, if the L and R values are suitable
 *  for that test, it generates, compiles, and runs a test program.
 *
 * All the test program generation, compilation, and running is done in
 *  a subdirectory "foo" of the current directory.  This directory must
 *  already exist.  (This is done so that, for example, it is easy to
 *  do all the disk-I/O-intensive work on a ramdisk, by making foo a
 *  ramdisk mount point.)
 *
 * The shifter code generation is slightly odd; it uses a type,
 *  BUILDER, which looks unnecessary.  And it _is_ unnecessary, from
 *  one point of view.  This is done so that the shift generation code
 *  can be textually identical to the similar code in the P4
 *  transpiler; the less needs to be done to the generation code when
 *  moving it between the tester and the P4 transpiler, the more
 *  confidence I have that the result will bep correct.  We use a few
 *  macros so that they can be identical, and, indeed, the P4C TC
 *  backend and this program #include the same file to get the
 *  generation code.  This is also why, for example, this program
 *  writes bld->newline() instead of the (*bld->newline)() I would
 *  normally use in C: it is trying to look like the C++ code in the P4
 *  transpiler.  Similarly to the P4 transpiler is also why the
 *  WIDTH_REC type exists here.
 *
 * The test programs also bracket the value bytes with guard bytes, to
 *  make it easier to catch implementations that incorrectly access
 *  bytes before or after the value bytes.  Again, macros are used to
 *  make the implementations textually identical to the P4
 *  transpiler's, which don't use guard bytes.
 *
 * This program can be run with no arguments, in which case it behaves
 *  as described above.  It can also take three arguments, which are a
 *  shift kind (shlc, shrlc, shrac, shlx, shrlx, shrax) and the L and R
 *  values in the above terminology, in which case it runs just that
 *  test.
 *
 * The tester program, which is written to foo/foo.c, is also designed
 *  for debuggability.  If run with no arguments, it runs test cases as
 *  the tester expects.  But, on failure, it prints the random seed
 *  used, the number of good test cases run before failure, and the
 *  test case that failed (as value v, shift count, slow result s, and
 *  fast result f).  If run with one argument, it is taken as the
 *  random seed; with two arguments, they are the random seed and a
 *  number of test cases to run before calling an internal bkpt()
 *  routine, which does nothing but is suitable for setting a debugger
 *  breakpoint on.
 *
 * For example, here is a (hypothetical) failing run:
 *
...

87a982db6575c9915aa4
shrax 65 9
seed = 1743553691, loops = 7
v = 0x14f9e998c43e140745d74f7d1d57184d >>a 0x003:
s = 0x1e9f3d331887c280e8bae9efa3aae309
f = 0x1e1f3d331887c280e8bae9efa3aae309
 *
 *  This indicates that the arithmetic right shift by a run-time value
 *  (shrax) of the 65-bit value 0x14f9e998c43e140745d74f7d1d57184d by a
 *  9-bit value which happens to have value 3 gave
 *  0x1e9f3d331887c280e8bae9efa3aae309 from the slow algorithm and
 *  0x1e1f3d331887c280e8bae9efa3aae309 from the fast algorithm, after
 *  running 7 other test cases successfuly.  You could then do
 *  something like
 *
% gdb --args foo/foo 1743553691 7
...
(gdb) b bkpt
(gdb) run
 *
 *  to do the same thing but with the debugger getting control just
 *  before the failing test case is started.  If you skip the loop
 *  count (7 in the above example), bkpt() is not called at all.  This
 *  is suitable if loops is printed as 0, indicating no successful test
 *  cases before the failing test case, or if you want to poke at other
 *  iterations of the main loop with the debugger.
 *
 * When generating test cases for sh*x shifts, the shift counts are
 *  randomly chosen 1/67 of the time to be fully random R-bit values,
 *  which for most values of R will almost always be large enough to
 *  shift all bits of v away, and the other 66/67 of the time will be
 *  randomly-chosen values in the range [0..L), padded with 0s to R
 *  bits.
 *
 * Randomness for the test programs comes from random(), seeded with
 *  time(0)^getpid().  Not great, but good enough for these purposes,
 *  and much faster than things like reading /dev/urandom.
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <strings.h>
#include <sys/wait.h>
#include <sys/socket.h>

#define TESTCASES 100000

typedef enum {
	  WR_SHL_X = 1,
	  WR_SHL_C,
	  WR_SHRA_X,
	  WR_SHRA_C,
	  WR_SHRL_X,
	  WR_SHRL_C,
	  } WRTYPE;

typedef struct builder BUILDER;
typedef struct width_rec WIDTH_REC;

struct builder {
  void (*append)(const char *);
  void (*appendFormat)(const char *, ...);
  void (*newline)(void);
  } ;

struct width_rec {
  WRTYPE type;
  union {
    struct {
      int lw;
      int rw;
      } shift_x;
    struct {
      int lw;
      int sv;
      } shift_c;
    } ;
  } ;

static FILE *bf;

#define assert(test) do { if (! (test)) assert_failed(__LINE__,#test); } while (0)

static void assert_failed(int lno, const char *txt)
{
 fprintf(stderr,"assert() failed, line %d: %s\n",lno,txt);
 exit(1);
}

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

static void append_type_for_width(BUILDER *bld, int w)
{
 if (w <= 64) bld->append("u64"); else bld->appendFormat("struct internal_bit_%d",w);
}

#include "../../gen-code/shifts.c"

static void gen_slow_init(int w)
{
 fprintf(bf," if (s == 0) return(v);\n");
 fprintf(bf," for (i=GUARDSIZE-1;i>=0;i--)\n");
 fprintf(bf,"  { BITS(r)[i-GUARDSIZE] = BITS(v)[i-GUARDSIZE];\n"/*}*/);
 fprintf(bf,"    BITS(r)[i+%d] = BITS(v)[i+%d];\n",(w+7)>>3,(w+7)>>3);
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," bzero(&BITS(r)[0],%d);\n",(w+7)>>3);
}

static void gen_slow_shl(int w)
{
 fprintf(bf,"\n");
 if (w <= 64)
  { fprintf(bf,"static u64 slow(u64 v, u64 s)\n");
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," return((s >= %d) ? 0 : ((v << s) & %lluULL));\n",w,(w<64)?(1ULL<<w)-1ULL:0xffffffffffffffffULL);
    fprintf(bf,/*{*/"}\n");
  }
 else
  { fprintf(bf,"static struct internal_bit_%d slow(struct internal_bit_%d v, u64 s)\n",w,w);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," struct internal_bit_%d r;\n",w);
    fprintf(bf," int i;\n");
    fprintf(bf," int j;\n");
    fprintf(bf,"\n");
    gen_slow_init(w);
    fprintf(bf," if (s < %d)\n",w);
    fprintf(bf,"  { for (i=0,j=s;j<%d;i++,j++) if ((BITS(v)[%d-(i>>3)]>>(i&7)) & 1) BITS(r)[%d-(j>>3)] |= 1 << (j & 7);\n"/*}*/,w,(w-1)>>3,(w-1)>>3);
    fprintf(bf,/*{*/"  }\n");
    fprintf(bf," return(r);\n");
    fprintf(bf,/*{*/"}\n");
  }
}

static void gen_slow_shrl(int w)
{
 fprintf(bf,"\n");
 if (w <= 64)
  { fprintf(bf,"static u64 slow(u64 v, u64 s)\n");
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," return((s >= %d) ? 0 : (v >> s));\n",w);
    fprintf(bf,/*{*/"}\n");
  }
 else
  { fprintf(bf,"static struct internal_bit_%d slow(struct internal_bit_%d v, u64 s)\n",w,w);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," struct internal_bit_%d r;\n",w);
    fprintf(bf," int i;\n");
    fprintf(bf," int j;\n");
    fprintf(bf,"\n");
    gen_slow_init(w);
    fprintf(bf," if (s < %d)\n",w);
    fprintf(bf,"  { for (i=0,j=s;j<%d;i++,j++) if ((BITS(v)[%d-(j>>3)]>>(j&7)) & 1) BITS(r)[%d-(i>>3)] |= 1 << (i & 7);\n"/*}*/,w,(w-1)>>3,(w-1)>>3);
    fprintf(bf,/*{*/"  }\n");
    fprintf(bf," return(r);\n");
    fprintf(bf,/*{*/"}\n");
  }
}

static void gen_slow_shra(int w)
{
 fprintf(bf,"\n");
 if (w <= 64)
  { fprintf(bf,"static u64 slow(u64 v, u64 s)\n");
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," if (s >= %d) s = %d;\n",w,w-1);
    fprintf(bf," return((v >> s) | ((v & %lluULL) ? (((1ULL << s) - 1ULL) << (%d-s)) : 0));\n",1ULL<<(w-1),w);
    fprintf(bf,/*{*/"}\n");
  }
 else
  { fprintf(bf,"static struct internal_bit_%d slow(struct internal_bit_%d v, u64 s)\n",w,w);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," struct internal_bit_%d r;\n",w);
    fprintf(bf," int i;\n");
    fprintf(bf," int j;\n");
    fprintf(bf," int sb;\n");
    fprintf(bf,"\n");
    gen_slow_init(w);
    if ((w & 7) == 1)
     { fprintf(bf," sb = BITS(v)[0] & 1;\n");
     }
    else
     { fprintf(bf," sb = (BITS(v)[0] >> %d) & 1;\n",(w+7)&7);
     }
    fprintf(bf,"  { for (i=%d;i>=0;i--)\n"/*}*/,w-1);
    fprintf(bf,"    if ((i >= %d-s) ? sb : ((BITS(v)[%d-((i+s)>>3)] >> ((i+s)&7)) & 1)) BITS(r)[%d-(i>>3)] |= 1 << (i & 7);\n",w-1,(w-1)>>3,(w-1)>>3);
    fprintf(bf,/*{*/"  }\n");
    fprintf(bf," return(r);\n");
    fprintf(bf,/*{*/"}\n");
  }
}

static void gen_overhead(int lw)
{
 fprintf(bf,"static unsigned int rseed;\n");
 fprintf(bf,"static unsigned int goodloops;\n");
 fprintf(bf,"static unsigned int skiploops;\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void args(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," skiploops = -1;\n");
 fprintf(bf," switch (ac)\n");
 fprintf(bf,"  { case 1:\n"/*}*/);
 fprintf(bf,"       rseed = time(0) ^ getpid();\n");
 fprintf(bf,"       break;\n");
 fprintf(bf,"    case 2:\n");
 fprintf(bf,"       rseed = strtoul(av[1],0,0);\n");
 fprintf(bf,"       break;\n");
 fprintf(bf,"    case 3:\n");
 fprintf(bf,"       rseed = strtoul(av[1],0,0);\n");
 fprintf(bf,"       skiploops = atoi(av[2]);\n");
 fprintf(bf,"       break;\n");
 fprintf(bf,"    default:\n");
 fprintf(bf,"       fprintf(stderr,\"Usage: %%s [seed [loopno]]\\n\",av[0]);\n");
 fprintf(bf,"       exit(1);\n");
 fprintf(bf,"       break;\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," srandom(rseed);\n");
 fprintf(bf," goodloops = 0;\n");
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
 fprintf(bf,"static void bkpt(void)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
 if (lw <= 30)
  { fprintf(bf,"#define INIT = random()\n");
  }
 else if (lw <= 60)
  { fprintf(bf,"#define INIT = ((random() << 30) ^ ((unsigned long long int)random() << 60))\n");
  }
 else if (lw <= 64)
  { fprintf(bf,"#define INIT = ((random() << 20) ^ ((unsigned long long int)random() << 40) ^ ((unsigned long long int)random() << 60))\n");
  }
 else
  { fprintf(bf,"static void random_data(u8 *p, int n)\n");
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," for (;n>0;n--) *p++ = (random() >> 22) & 255;\n");
    fprintf(bf,/*{*/"}\n");
    fprintf(bf,"\n");
    fprintf(bf,"static struct internal_bit_%d init(struct internal_bit_%d v)\n",lw,lw);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," int i;\n");
    fprintf(bf," struct internal_bit_%d r;\n",lw);
    fprintf(bf,"\n");
    fprintf(bf," for (i=GUARDSIZE-1;i>=0;i--)\n");
    fprintf(bf,"  { BITS(r)[i-GUARDSIZE] = BITS(v)[i-GUARDSIZE];\n"/*}*/);
    fprintf(bf,"    BITS(r)[i+%d] = BITS(v)[i+%d];\n",(lw+7)>>3,(lw+7)>>3);
    fprintf(bf,/*{*/"  }\n");
    fprintf(bf," random_data(&BITS(r)[0],%u);\n",(lw+7)>>3);
    fprintf(bf," return(r);\n");
    fprintf(bf,/*{*/"}\n");
    fprintf(bf,"#define INIT = init(v)\n");
    fprintf(bf,"\n");
    fprintf(bf,"static struct internal_bit_%d constguards(struct internal_bit_%d to, struct internal_bit_%d from)\n",lw,lw,lw);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," bcopy(&BITS(to)[-GUARDSIZE],&BITS(to)[0],%d);\n",(lw+7)>>3);
    fprintf(bf," bcopy(&BITS(from)[-GUARDSIZE],&BITS(to)[-GUARDSIZE],GUARDSIZE);\n");
    fprintf(bf," bcopy(&BITS(from)[%u],&BITS(to)[%u],GUARDSIZE);\n",(lw+7)>>3,(lw+7)>>3);
    fprintf(bf," return(to);\n");
    fprintf(bf,/*{*/"}\n");
    fprintf(bf,"\n");
    fprintf(bf,"static struct internal_bit_%d copyguards(struct internal_bit_%d to, struct internal_bit_%d from)\n",lw,lw,lw);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," bcopy(&BITS(from)[-GUARDSIZE],&BITS(to)[-GUARDSIZE],GUARDSIZE);\n");
    fprintf(bf," bcopy(&BITS(from)[%u],&BITS(to)[%u],GUARDSIZE);\n",(lw+7)>>3,(lw+7)>>3);
    fprintf(bf," return(to);\n");
    fprintf(bf,/*{*/"}\n");
    fprintf(bf,"\n");
    fprintf(bf,"static void gen_guards(u8 *bits, u8 *gb, u8 *ga)\n");
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," random_data(bits-GUARDSIZE,GUARDSIZE);\n");
    fprintf(bf," random_data(bits+%u,GUARDSIZE);\n",(lw+7)>>3);
    fprintf(bf," if (gb) bcopy(bits-GUARDSIZE,gb,GUARDSIZE);\n");
    fprintf(bf," if (ga) bcopy(bits+%u,ga,GUARDSIZE);\n",(lw+7)>>3);
    fprintf(bf,/*{*/"}\n");
    fprintf(bf,"\n");
    fprintf(bf,"#define CONSTGUARDS(t,f) constguards((t),(f))\n");
    fprintf(bf,"#define COPYGUARDS(t,f) copyguards((t),(f))\n");
    fprintf(bf,"#define GUARDARGS , u8 *guard_arg_b, u8 *guard_arg_a\n");
    fprintf(bf,"#define SETGUARDS(x) do { \\\n"/*}*/);
    fprintf(bf,"\trandom_data(guard_arg_b,GUARDSIZE);\\\n");
    fprintf(bf,"\tbcopy(guard_arg_b,&BITS(x)[-GUARDSIZE],GUARDSIZE);\\\n");
    fprintf(bf,"\trandom_data(guard_arg_a,GUARDSIZE);\\\n");
    fprintf(bf,"\tbcopy(guard_arg_a,&BITS(x)[%d],GUARDSIZE);\\\n",(lw+7)>>3);
    fprintf(bf,/*{*/"\t} while (0)\n");
  }
 if (lw <= 64)
  { fprintf(bf,"#define GUARDARGS /*nothing*/\n");
    fprintf(bf,"#define SETGUARDS(x) do ; while (0)\n");
  }
}

static void gen_print_value(int w)
{
 fprintf(bf,"static void print_value_%d(struct internal_bit_%d v)\n",w,w);
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," printf(\"<\");\n");
 fprintf(bf," for (i=-GUARDSIZE;i<0;i++) printf(\"%%02x\",BITS(v)[i]);\n");
 fprintf(bf," printf(\">\");\n");
 fprintf(bf," for (i=0;i<%d;i++) printf(\"%%02x\",BITS(v)[i]);\n",(w+7)>>3);
 fprintf(bf," printf(\"<\");\n");
 fprintf(bf," for (;i<%d+GUARDSIZE;i++) printf(\"%%02x\",BITS(v)[i]);\n",(w+7)>>3);
 fprintf(bf," printf(\">\");\n");
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
}

static void gen_fails(int lw, int rw, const char *optext)
{
 fprintf(bf,"\n");
 if (lw > 64) gen_print_value(lw);
 if ((rw > 64) && (rw != lw)) gen_print_value(rw);
fprintf(bf,"// lw=%d rw=%d optext=%s\n",lw,rw,optext);
 fprintf(bf,"static void valuefail(const char *how, ");
 append_type_for_width(&builder,lw);
 fprintf(bf," v, ");
 append_type_for_width(&builder,lw);
 fprintf(bf," s, ");
 append_type_for_width(&builder,lw);
 fprintf(bf," f, ");
 if (rw < 1)
  { fprintf(bf,"int");
  }
 else
  { append_type_for_width(&builder,rw);
  }
 fprintf(bf, " sh)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," printf(\"%%s%%sseed = %%u, loops = %%d\\n\",how,how?\"\\n\":\"\",rseed,goodloops);\n");
 if (lw <= 64)
  { fprintf(bf," printf(\"v = 0x%%0%ullx\",v);\n",(lw+3)>>2);
  }
 else
  { fprintf(bf," printf(\"v = \");\n");
    fprintf(bf," print_value_%d(v);\n",lw);
  }
 fprintf(bf," printf(\": %s \");\n",optext);
 if (rw < 1)
  { fprintf(bf," printf(\"%%d\",sh);\n");
  }
 else if (rw <= 64)
  { fprintf(bf," printf(\"0x%%0%ullx\",(unsigned long long int)sh);\n",(rw+3)>>2);
  }
 else
  { fprintf(bf," print_value_%d(sh);\n",rw);
  }
 fprintf(bf," printf(\"\\n\");\n");
 if (lw <= 64)
  { fprintf(bf," printf(\"s = 0x%%0%ullx\",s);\n",(lw+3)>>2);
  }
 else
  { fprintf(bf," printf(\"s = \");\n");
    fprintf(bf," print_value_%d(s);\n",lw);
  }
 fprintf(bf," printf(\"\\n\");");
 if (lw <= 64)
  { fprintf(bf," printf(\"f = 0x%%0%ullx\",f);\n",(lw+3)>>2);
  }
 else
  { fprintf(bf," printf(\"f = \");\n");
    fprintf(bf," print_value_%d(f);\n",lw);
  }
 fprintf(bf," printf(\"\\n\");");
 fprintf(bf," exit(1);\n");
 fprintf(bf,/*{*/"}\n");
 if (lw > 64)
  { fprintf(bf,"\n");
    fprintf(bf,"static void guardfail(const char *which, struct internal_bit_%u v, const u8 *expected)\n",lw);
    fprintf(bf,"{\n"/*}*/);
    fprintf(bf," int i;\n");
    fprintf(bf,"\n");
    fprintf(bf," printf(\"`%%s' guard corrupted\\n\",which);\n");
    fprintf(bf," printf(\"seed = %%u, loops = %%d\\n\",rseed,goodloops);\n");
    fprintf(bf," printf(\"guards: <\");\n");
    fprintf(bf," for (i=0;i<GUARDSIZE;i++) printf(\"%%02x\",BITS(v)[i-GUARDSIZE]);\n");
    fprintf(bf," printf(\"> and <\");\n");
    fprintf(bf," for (i=0;i<GUARDSIZE;i++) printf(\"%%02x\",BITS(v)[i+%d]);\n",(lw+7)>>3);
    fprintf(bf," printf(\">\\n\");\n");
    fprintf(bf," printf(\"expected: <\");\n");
    fprintf(bf," for (i=0;i<GUARDSIZE;i++) printf(\"%%02x\",expected[i]);\n");
    fprintf(bf," printf(\">\\n\");\n");
    fprintf(bf," exit(1);\n");
    fprintf(bf,/*{*/"}\n");
  }
}

static void gen_c_tester(const char *kind, int lw, int sv, const char *optext)
{
 if (lw <= 64) abort();
 gen_fails(lw,-1,optext);
 fprintf(bf,"\n");
 fprintf(bf,"\n");
 fprintf(bf,"int main(int, char **);\n");
 fprintf(bf,"int main(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," struct internal_bit_%d v;\n",lw);
 fprintf(bf," struct internal_bit_%d s;\n",lw);
 fprintf(bf," struct internal_bit_%d f;\n",lw);
 fprintf(bf," int i;\n");
 fprintf(bf," int j;\n");
 fprintf(bf," u8 rgb[GUARDSIZE];\n");
 fprintf(bf," u8 rga[GUARDSIZE];\n");
 fprintf(bf,"\n");
#ifndef __linux__
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)1);\n");
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)3);\n");
#endif
 fprintf(bf," args(ac,av);\n");
 fprintf(bf," for (i=%d;i>0;i--)\n",TESTCASES);
 fprintf(bf,"  { random_data(&BITS(v)[0],%d);\n"/*}*/,(lw-1)>>3);
 if (lw & 7) fprintf(bf,"    BITS(v)[0] &= %d;\n",(1<<(lw&7))-1);
 fprintf(bf,"    gen_guards(&BITS(v)[0],0,0);\n");
 fprintf(bf,"    if (skiploops-- == 0) bkpt();\n");
 fprintf(bf,"    s = slow(v,%d);\n",sv);
 fprintf(bf,"    f = %s_%d_c_%d(v,&rgb[0],&rga[0]);\n",kind,lw,sv);
 fprintf(bf,"    if (bcmp(&BITS(f)[-GUARDSIZE],&rgb[0],GUARDSIZE)) guardfail(\"before\",f,&rgb[0]);\n");
 fprintf(bf,"    if (bcmp(&BITS(f)[%d],&rga[0],GUARDSIZE)) guardfail(\"after\",f,&rga[0]);\n",(lw+7)>>3);
 fprintf(bf,"    goodloops ++;\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," return(0);\n");
 fprintf(bf,/*{*/"}\n");
}

static void gen_x_tester(const char *kind, int lw, int rw, const char *optext, int sra)
{
 int i;
 int j;

 if ((lw <= 64) && (rw <= 64)) abort();
 fprintf(bf,"\n");
 gen_fails(lw,rw,optext);
 fprintf(bf,"\n");
 fprintf(bf,"\n");
 fprintf(bf,"int main(int, char **);\n");
 fprintf(bf,"int main(int ac, char **av)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," ");
 append_type_for_width(&builder,lw);
 fprintf(bf," v;\n");
 fprintf(bf," ");
 append_type_for_width(&builder,lw);
 fprintf(bf," s;\n");
 fprintf(bf," ");
 append_type_for_width(&builder,lw);
 fprintf(bf," f;\n");
 fprintf(bf," ");
 append_type_for_width(&builder,rw);
 fprintf(bf," sh;\n");
 fprintf(bf," int sh_i;\n");
 fprintf(bf," int i;\n");
 fprintf(bf," int j;\n");
 if (rw > 64) fprintf(bf," int k;\n");
 if (sra) fprintf(bf," int sign;\n");
 fprintf(bf," u8 gb[GUARDSIZE];\n");
 fprintf(bf," u8 ga[GUARDSIZE];\n");
 fprintf(bf,"\n");
#ifndef __linux__
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)1);\n");
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)3);\n");
#endif
 fprintf(bf," args(ac,av);\n");
 fprintf(bf," for (i=%d;i>0;i--)\n",TESTCASES);
 fprintf(bf,"  { "/*}*/);
 if (lw <= 30)
  { fprintf(bf,"v = random() & %u;\n",(1U<<lw)-1);
  }
 else if (lw <= 60)
  { fprintf(bf,"v = (random() & 1073741823) | ((random() & %uULL) << 30);\n",(1U<<(lw-30))-1);
  }
 else if (lw <= 64)
  { fprintf(bf,"v = (random() & 1048575) | ((random() & 1048575ULL) << 20) | ((random() & %uULL) << 40);\n",(1U<<(lw-40))-1);
  }
 else
  { fprintf(bf,"for (j=%d;j>=0;j--) BITS(v)[j] = (random() >> 22) & 255;\n",(lw-1)>>3);
    if (lw & 7) fprintf(bf,"    BITS(v)[0] &= %d;\n",(1<<(lw&7))-1);
    fprintf(bf,"    random_data(&BITS(v)[-GUARDSIZE],GUARDSIZE);\n");
    fprintf(bf,"    random_data(&BITS(v)[%d],GUARDSIZE);\n",(lw+7)>>3);
  }
 if (sra)
  { if (lw <= 64)
     { fprintf(bf,"    sign = (v >> %u) & 1;\n",lw-1);
     }
    else
     { if ((lw - 1) & 7)
	{ fprintf(bf,"    sign = (BITS(v)[0] >> %u) & 1;\n",(lw-1)&7);
	}
       else
	{ fprintf(bf,"    sign = BITS(v)[0] & 1;\n");
	}
     }
  }
 if (rw <= 64)
  { fprintf(bf,"    if (random() %% 67)\n");
    fprintf(bf,"     { "/*}*/);
    if ((rw < 30) && (((1U<<rw)-1U) <= lw))
     { fprintf(bf,"sh = random() & %u;\n",(1U<<rw)-1U);
     }
    else
     { fprintf(bf,"sh = random() %% %u;\n",lw);
     }
    fprintf(bf,/*{*/"     }\n");
    fprintf(bf,"    else\n");
    fprintf(bf,"     { "/*}*/);
    if (rw <= 30)
     { fprintf(bf,"sh = random() & %u;\n",(1U<<rw)-1);
     }
    else if (rw <= 60)
     { fprintf(bf,"sh = (random() & 1073741823) | ((random() & %uULL) << 30);\n",(1U<<(rw-30))-1);
     }
    else
     { fprintf(bf,"sh = (random() & 1048575) | ((random() & 1048575ULL) << 20) | ((random() & %uULL) << 40);\n",(1U<<(rw-40))-1);
     }
    fprintf(bf,/*{*/"     }\n");
    fprintf(bf,"    sh_i = (sh < %u) ? sh : -1;\n",lw);
  }
 else
  { fprintf(bf,"    if (random() %% 67)\n");
    fprintf(bf,"     { j = random() %% %d;\n"/*}*/,lw);
    for (i=rw-1,j=(rw-1)>>3;i;i>>=8,j--)
     { if (i < rw-1) fprintf(bf,"       j >>= 8;\n");
       fprintf(bf,"       BITS(sh)[%d] = j & 255;\n",j);
     }
    for (;j>0;j--) fprintf(bf,"       BITS(sh)[%d] = 0;\n",j);
    fprintf(bf,/*{*/"     }\n");
    fprintf(bf,"    else\n");
    fprintf(bf,"     { for (k=%d;k>=0;k--) BITS(sh)[k] = (random() >> 22) & 255;\n"/*}*/,(rw-1)>>3);
    fprintf(bf,/*{*/"     }\n");
    fprintf(bf,"    sh_i = 0;\n");
    fprintf(bf,"    for (j=%u;j>=0;j--)\n",(rw-1)>>3);
    fprintf(bf,"     { sh_i = (sh_i << 8) | BITS(sh)[j];\n"/*}*/);
    fprintf(bf,"       if (sh_i >= %u)\n",lw);
    fprintf(bf,"	{ sh_i = -1;\n"/*}*/);
    fprintf(bf,"	  break;\n");
    fprintf(bf,/*{*/"	}\n");
    fprintf(bf,/*{*/"     }\n");
  }
 fprintf(bf,"    if (skiploops-- == 0) bkpt();\n");
 fprintf(bf,"    s = (sh_i < 0) ? ");
 if (lw <= 64)
  { if (sra) fprintf(bf,"sign ? 0x%llx : ",((1ULL<<(lw-1))<<1)|1ULL);
    fprintf(bf,"0");
  }
 else
  { fprintf(bf,"CONSTGUARDS((");
    if (sra)
     { fprintf(bf,"sign ? (");
       append_type_for_width(&builder,lw);
       fprintf(bf,"){"/*}*/);
       fprintf(bf,"%u",(1U<<(((lw-1)&7)+1))-1);
       for (i=(lw-1)>>3;i>0;i--) fprintf(bf,",255");
       fprintf(bf,/*{*/"} : ");
     }
    fprintf(bf,"(");
    append_type_for_width(&builder,lw);
    fprintf(bf,"){0}),v)");
  }
 fprintf(bf," : slow(v,sh_i);\n");
 fprintf(bf,"    f = %s_%d_x_%d(v,sh%s);\n",kind,lw,rw,(lw>64)?",&gb[0],&ga[0]":"");
 if (lw > 64)
  { fprintf(bf,"    if (bcmp(&BITS(f)[-GUARDSIZE],&gb[0],GUARDSIZE)) guardfail(\"before\",f,&gb[0]);\n");
    fprintf(bf,"    if (bcmp(&BITS(f)[%u],&ga[0],GUARDSIZE)) guardfail(\"after\",f,&gb[0]);\n",(lw+7)>>3);
  }
 fprintf(bf,"    if (");
 if (lw <= 64)
  { fprintf(bf,"s != f");
  }
 else
  { fprintf(bf,"bcmp(&BITS(s)[0],&BITS(f)[0],%d)",(lw+7)>>3);
  }
 fprintf(bf,") valuefail(\"data mismatch\",v,s,f,sh);\n");
 fprintf(bf,"    goodloops ++;\n");
 fprintf(bf,/*{*/"  }\n");
 fprintf(bf," return(0);\n");
 fprintf(bf,/*{*/"}\n");
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

static void run_test(WIDTH_REC *wr)
{
 bf = fopen("foo.c","w");
 if (! bf)
  { fprintf(stderr,"can't open foo.c: %s\n",strerror(errno));
    exit(1);
  }
 fprintf(bf,"#include <time.h>\n");
 fprintf(bf,"#include <stdio.h>\n");
 fprintf(bf,"#include <unistd.h>\n");
 fprintf(bf,"#include <stdlib.h>\n");
 fprintf(bf,"#include <strings.h>\n");
 fprintf(bf,"\n");
 fprintf(bf,"#include \"types.h\"\n");
 fprintf(bf,"\n");
 switch (wr->type)
  { case WR_SHL_C:
    case WR_SHRL_C:
    case WR_SHRA_C:
       gen_overhead(wr->shift_c.lw);
       break;
    case WR_SHL_X:
    case WR_SHRL_X:
    case WR_SHRA_X:
       gen_overhead(wr->shift_x.lw);
       break;
    default:
       abort();
       break;
  }
 fprintf(bf,"\n");
 fprintf(bf,"static");
 switch (wr->type)
  { case WR_SHL_C:
       printf("shlc %d %d\n",wr->shift_c.lw,wr->shift_c.sv);
       gen_shl_c(&builder,wr);
       gen_slow_shl(wr->shift_c.lw);
       gen_c_tester("shl",wr->shift_c.lw,wr->shift_c.sv,"<<");
       break;
    case WR_SHRL_C:
       printf("shrlc %d %d\n",wr->shift_c.lw,wr->shift_c.sv);
       gen_shrl_c(&builder,wr);
       gen_slow_shrl(wr->shift_c.lw);
       gen_c_tester("shrl",wr->shift_c.lw,wr->shift_c.sv,">>l");
       break;
    case WR_SHRA_C:
       printf("shrac %d %d\n",wr->shift_c.lw,wr->shift_c.sv);
       gen_shra_c(&builder,wr);
       gen_slow_shra(wr->shift_c.lw);
       gen_c_tester("shra",wr->shift_c.lw,wr->shift_c.sv,">>a");
       break;
    case WR_SHL_X:
       printf("shlx %d %d\n",wr->shift_x.lw,wr->shift_x.rw);
       gen_shl_x(&builder,wr);
       gen_slow_shl(wr->shift_x.lw);
       gen_x_tester("shl",wr->shift_x.lw,wr->shift_x.rw,"<<",0);
       break;
    case WR_SHRL_X:
       printf("shrlx %d %d\n",wr->shift_x.lw,wr->shift_x.rw);
       gen_shrl_x(&builder,wr);
       gen_slow_shrl(wr->shift_x.lw);
       gen_x_tester("shrl",wr->shift_x.lw,wr->shift_x.rw,">>l",0);
       break;
    case WR_SHRA_X:
       printf("shrax %d %d\n",wr->shift_x.lw,wr->shift_x.rw);
       gen_shra_x(&builder,wr);
       gen_slow_shra(wr->shift_x.lw);
       gen_x_tester("shra",wr->shift_x.lw,wr->shift_x.rw,">>a",1);
       break;
    default:
       abort();
       break;
  }
 fclose(bf);
#ifdef __linux__
 run_cmd("/bin/cc","/bin/cc","-g","-o","foo","foo.c","-I../..",(const char *)0);
#else
 run_cmd("/usr/bin/cc","cc","-g","-o","foo","foo.c","-I../..",(const char *)0);
#endif
 run_cmd("./foo","foo",(const char *)0);
}

static void setup(void)
{
 if (chdir("foo") < 0)
  { fprintf(stderr,"chdir foo: %s\n",strerror(errno));
    exit(1);
  }
}

int main(int, char **);
int main(int ac, char **av)
{
 int i;
 int j;
 int lw;
 int r;
 WIDTH_REC wr;

 if (ac == 1)
  { setup();
    j = 0;
    for (i=128*129;i>0;i--)
     { r = j >> 7;
       lw = (j & 127) + 1;
       j += 907; // relatively prime to 128*129
       if (j >= 128*129) j -= 128*129;
       if ((lw <= 64) && (r <= 64)) continue;
       if ((lw > 64) && (r < lw))
	{ wr.shift_c.lw = lw;
	  wr.shift_c.sv = r;
	  wr.type = WR_SHL_C;
	  run_test(&wr);
	  wr.type = WR_SHRL_C;
	  run_test(&wr);
	  wr.type = WR_SHRA_C;
	  run_test(&wr);
	}
       if (r == 0) continue;
       if ((lw > 64) || (r > 64))
	{ wr.shift_x.lw = lw;
	  wr.shift_x.rw = r;
	  wr.type = WR_SHL_X;
	  run_test(&wr);
	  wr.type = WR_SHRL_X;
	  run_test(&wr);
	  wr.type = WR_SHRA_X;
	  run_test(&wr);
	}
     }
  }
 else if (ac == 4)
  { setup();
    if (!strcmp(av[1],"shlc"))
     { wr.type = WR_SHL_C;
       wr.shift_c.lw = atoi(av[2]);
       wr.shift_c.sv = atoi(av[3]);
     }
    else if (!strcmp(av[1],"shrlc"))
     { wr.type = WR_SHRL_C;
       wr.shift_c.lw = atoi(av[2]);
       wr.shift_c.sv = atoi(av[3]);
     }
    else if (!strcmp(av[1],"shrac"))
     { wr.type = WR_SHRA_C;
       wr.shift_c.lw = atoi(av[2]);
       wr.shift_c.sv = atoi(av[3]);
     }
    else if (!strcmp(av[1],"shlx"))
     { wr.type = WR_SHL_X;
       wr.shift_x.lw = atoi(av[2]);
       wr.shift_x.rw = atoi(av[3]);
     }
    else if (!strcmp(av[1],"shrlx"))
     { wr.type = WR_SHRL_X;
       wr.shift_x.lw = atoi(av[2]);
       wr.shift_x.rw = atoi(av[3]);
     }
    else if (!strcmp(av[1],"shrax"))
     { wr.type = WR_SHRA_X;
       wr.shift_x.lw = atoi(av[2]);
       wr.shift_x.rw = atoi(av[3]);
     }
    else
     { printf("Invalid first arg\n");
       exit(1);
     }
    run_test(&wr);
  }
 return(0);
}
