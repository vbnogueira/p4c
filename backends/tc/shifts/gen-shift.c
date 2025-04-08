/*
 * Shift-code generation algorithm test program.
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
 * In case case, this program runs the shift-code generation code.  It
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
 *  is textually very similar to the similar code in the P4 transpiler;
 *  the less needs to be done to the generation code when moving it
 *  between the tester and the P4 transpiler, the more confidence I
 *  have that the result will bep correct.  This is also why, for
 *  example, this program writes bld->newline() instead of the
 *  (*bld->newline)() I would normally use in C: it is trying to look
 *  like the C++ code in the P4 transpiler.  Similarly to the P4
 *  transpiler is also why the WIDTH_REC type exists here.  With these
 *  done, moving the shift generation code into the P4 transpiler needs
 *  only changing BUILDER to EBPF::CodeBuilder.  (This could even be
 *  done with a #define, in which case _no_ textual changes are
 *  needed.)
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

static void gen_shl_c(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int lw;
 unsigned int sv;
 int o;
 int i;
 const char *pref;
 const char *suff;
 int nb;

 assert(wr->type == WR_SHL_C);
 lw = wr->shift_c.lw;
 sv = wr->shift_c.sv;
 assert((lw > 64) && (sv < lw));
 bld->newline();
 bld->appendFormat("struct internal_bit_%u shl_%u_c_%u(struct internal_bit_%u v)\n",lw,lw,sv,lw);
 bld->append("{\n"/*}*/);
 if (sv == 0)
  { /*
     * Can this even happen?  Shifting by a zero constant is the kind
     *	of thing I'd expect to get optimized away in an MI way.  But
     *	it's easy to DTRT here....
     */
    bld->append(" return(v);\n");
  }
 else
  { bld->appendFormat(" struct internal_bit_%u rv = randinit();\n",lw);
    o = sv >> 3;
    sv &= 7;
    if (sv) bld->append(" u16 a;\n");
    bld->newline();
    if (o) bld->appendFormat(" __builtin_memset(&rv.bits[0],0,%u);\n",o);
    if (sv)
     { pref = "";
       suff = "";
       // This can be optimized more in some cases.
       // "First make it work..."
       i = 0;
       nb = lw >> 3;
       for (;o<nb;o++,i++)
	{ bld->appendFormat(" a = %sv.bits[%u] << %u%s;\n",pref,i,sv,suff);
	  pref = "(a >> 8) | (";
	  suff = ")";
	  bld->appendFormat(" rv.bits[%u] = a & 255;\n",o);
	}
       if (lw & 7)
	{ bld->appendFormat(" a = %sv.bits[%u] << %u%s;\n",pref,i,sv,suff);
	  bld->appendFormat(" rv.bits[%u] = a & %u;\n",o,(1U<<(lw&7))-1U);
	}
     }
    else
     { bld->appendFormat(" __builtin_memcpy(&rv.bits[%u],&v.bits[0],%u);\n",o,(lw>>3)-o);
       if (lw & 7) bld->appendFormat(" rv.bits[%u] = v.bits[%u] & %u;\n",lw>>3,(lw>>3)-o,(1U<<(lw&7))-1U);
     }
    bld->append(" return(rv);\n");
  }
 bld->append(/*{*/"}\n");
}

static void gen_shrl_c(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int lw;
 unsigned int sv;
 int i;
 int j;
 int s;

 assert(wr->type == WR_SHRL_C);
 lw = wr->shift_c.lw;
 sv = wr->shift_c.sv;
 assert(lw > 64);
 bld->newline();
 append_type_for_width(bld,lw);
 bld->appendFormat(" shrl_%u_c_%u(",lw,sv);
 append_type_for_width(bld,lw);
 bld->append(" v)\n");
 bld->append("{\n"/*}*/);
 if (sv == 0)
  { /*
     * Can this even happen?  Shifting by a zero constant is the kind
     *	of thing I'd expect to get optimized away in an MI way.  But
     *	it's easy to DTRT here....
     */
    bld->append(" return(v);\n");
  }
 else
  { bld->appendFormat(" struct internal_bit_%u rv = randinit();\n",lw);
    if (sv >= (lw & ~7U))
     { // No bits survive from anything below the top byte of v
       bld->newline();
       bld->appendFormat(" __builtin_memset(&rv.bits[1],0,%u);\n",(lw-1)>>3);
       if (sv == (lw & ~7U))
	{ bld->appendFormat(" rv.bits[0] = v.bits[%u]",(lw-1)>>3);
	  if (lw & 7) bld->appendFormat(" & %u",(1U<<(lw&7))-1);
	  bld->append(";\n");
	}
       else if (lw & 7)
	{ bld->appendFormat(" rv.bits[0] = (v.bits[%u] & %u) >> %u;\n",(lw-1)>>3,(1U<<(lw&7))-1U,sv-(lw&~7U));
	}
       else
	{ bld->appendFormat(" rv.bits[0] = v.bits[%u] >> %u;\n",(lw-1)>>3,sv-(lw&~7U));
	}
     }
    else if (! (sv & 7))
     { if (lw & 7)
	{ bld->appendFormat(" __builtin_memcpy(&rv.bits[0],&v.bits[%u],%u);\n",sv>>3,(lw-sv)>>3);
	  bld->appendFormat(" rv.bits[%u] = v.bits[%u] & %u;\n",(lw-sv)>>3,lw>>3,(1U<<(lw&7))-1U);
	  bld->appendFormat(" __builtin_memset(&rv.bits[%u],0,%u);\n",((lw-sv)>>3)+1,sv>>3);
	}
       else
	{ bld->appendFormat(" __builtin_memcpy(&rv.bits[0],&v.bits[%u],%u);\n",sv>>3,(lw-sv)>>3);
	  bld->appendFormat(" __builtin_memset(&rv.bits[%u],0,%u);\n",(lw-sv)>>3,sv>>3);
	}
     }
    else
     { bld->append(" u32 a;\n");
       bld->newline();
       bld->append(" a = ");
       if (lw & 7) bld->appendFormat("v.bits[%u] & %u",lw>>3,(1U<<(lw&7))-1); else bld->appendFormat("v.bits[%u]",(lw>>3)-1);
       bld->append(";\n");
       s = 8 + ((lw - 1) & 7) - ((lw - 1 - sv) & 7);
       j = (lw - 9) >> 3;
       i = (lw - 1 - sv) >> 3;
       if (i < ((lw-1)>>3)) bld->appendFormat(" __builtin_memset(&rv.bits[%u],0,%u);\n",i+1,((lw+7)>>3)-(i+1));
       for (i=(lw-1-sv)>>3;i>0;i--,j--)
	{ bld->appendFormat(" a = (a << 8) | v.bits[%u];\n",j);
	  bld->appendFormat(" rv.bits[%u] = (a >> %u) & 255;\n",i,s);
	}
       if (s == 8)
	{ bld->appendFormat(" rv.bits[%u] = a & 255;\n",i);
	}
       else if (s > 8)
	{ bld->appendFormat(" rv.bits[%u] = (a >> %u) & 255;\n",i,s-8);
	}
       else
	{ bld->appendFormat(" a = (a << 8) | v.bits[%u];\n",j);
	  bld->appendFormat(" rv.bits[%u] = (a >> %u) & 255;\n",i,s);
	}
     }
    bld->append(" return(rv);\n");
  }
 bld->append(/*{*/"}\n");
}

static void gen_shra_c(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int lw;
 unsigned int sv;
 int i;
 int j;
 int s;
 unsigned int m;

 assert(wr->type == WR_SHRA_C);
 lw = wr->shift_c.lw;
 sv = wr->shift_c.sv;
 assert(lw > 64);
 bld->newline();
 append_type_for_width(bld,lw);
 bld->appendFormat(" shra_%u_c_%u(",lw,sv);
 append_type_for_width(bld,lw);
 bld->append(" v)\n");
 bld->append("{\n"/*}*/);
 if (sv == 0)
  { /*
     * Can this even happen?  Shifting by a zero constant is the kind
     *	of thing I'd expect to get optimized away in an MI way.  But
     *	it's easy to DTRT here....
     */
    bld->append(" return(v);\n");
  }
 else
  { bld->appendFormat(" struct internal_bit_%u rv = randinit();\n",lw);
    if (sv >= (lw & ~7U))
     { // No bits survive from anything below the top byte of v
       bld->newline();
       if (sv == lw-1)
	{ // All bits copies of sign bit
	  if (lw & 7)
	   { bld->appendFormat(" __builtin_memset(&rv.bits[0],(v.bits[%u]&%u)?255:0,%u);\n",lw>>3,1U<<((lw-1)&7),lw>>3);
	     bld->appendFormat(" rv.bits[%u] = (v.bits[%u] & %u) ? %u : 0;\n",lw>>3,lw>>3,1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	   }
	  else
	   { bld->appendFormat(" __builtin_memset(&rv.bits[0],(v.bits[%u]&%u)?255:0,%u);\n",(lw>>3)-1,1U<<((lw-1)&7),lw>>3);
	   }
	}
       else
	{ if (lw & 7)
	   { bld->appendFormat(" if (v.bits[%u] & %u)\n",(lw-1)>>3,1U<<((lw-1)&7));
	     bld->appendFormat("  { __builtin_memset(&rv.bits[1],255,%u);\n"/*}*/,(lw>>3)-1);
	     bld->appendFormat("    rv.bits[%u] = %u;\n",(lw-1)>>3,(1U<<(lw&7))-1U);
	     bld->append(/*{*/"  }\n");
	     bld->append(" else\n");
	     bld->appendFormat("  { __builtin_memset(&rv.bits[1],0,%u);\n"/*}*/,lw>>3);
	     bld->append(/*{*/"  }\n");
	   }
	  else
	   { bld->appendFormat(" __builtin_memset(&rv.bits[1],(v.bits[%u]&128)?255:0,%u);\n",(lw-1)>>3,(lw-9)>>3);
	   }
	  bld->append(" rv.bits[0] = (");
	  if (sv == (lw & ~7U))
	   { bld->appendFormat("v.bits[%u]",(lw-1)>>3);
	     if (lw & 7) bld->appendFormat(" & %u",(1U<<(lw&7))-1);
	   }
	  else if (lw & 7)
	   { bld->appendFormat("(v.bits[%u] & %u) >> %u",(lw-1)>>3,(1U<<(lw&7))-1U,sv-(lw&~7U));
	   }
	  else
	   { bld->appendFormat("v.bits[%u] >> %u",(lw-1)>>3,sv-(lw&~7U));
	   }
	  bld->appendFormat(") | ((v.bits[%u] & %u) ? %u : 0);\n",(lw-1)>>3,1U<<((lw-1)&7),(255U<<((lw-(sv&7))&7))&255U);
	}
     }
    else if (! (sv & 7))
     { bld->newline();
       if (lw & 7)
	{ bld->appendFormat(" __builtin_memcpy(&rv.bits[0],&v.bits[%u],%u);\n",sv>>3,(lw+8-sv)>>3);
	  bld->appendFormat(" rv.bits[%u] |= (v.bits[%u] & %u) ? %u : 0;\n",(lw-sv)>>3,lw>>3,1U<<((lw-1)&7),(255U<<(lw&7))&255U);
	  if ((sv >> 3) > 1) bld->appendFormat(" __builtin_memset(&rv.bits[%u],(v.bits[%u]&%u)?255:0,%u);\n",((lw-sv)>>3)+1,lw>>3,1U<<((lw-1)&7),(sv>>3)-1);
	  bld->appendFormat(" rv.bits[%u] = (v.bits[%u] & %u) ? %u : 0;\n",lw>>3,lw>>3,1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	}
       else
	{ bld->appendFormat(" __builtin_memcpy(&rv.bits[0],&v.bits[%u],%u);\n",sv>>3,(lw-sv)>>3);
	  bld->appendFormat(" __builtin_memset(&rv.bits[%u],(v.bits[%u]&128)?255:0,%u);\n",(lw-sv)>>3,(lw-1)>>3,sv>>3);
	}
     }
    else
     { bld->append(" u32 a;\n");
       bld->newline();
       switch (lw & 7)
	{ case 0:
	     bld->appendFormat(" a = v.bits[%u] | ((v.bits[%u] & 128) ? ~(u32)255 : 0);\n",(lw-1)>>3,(lw-1)>>3);
	     break;
	  case 1:
	     bld->appendFormat(" a = (v.bits[%u] & 1) ? ~(u32)0 : 0;\n",lw>>3);
	     break;
	  default:
	     bld->appendFormat(" a = (v.bits[%u] & %u) | ((v.bits[%u] & %u) ? ~(u32)%u : 0);\n",lw>>3,(1U<<(lw&7))-1,lw>>3,1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	     break;
	}
       s = 8 + ((lw - 1) & 7) - ((lw - 1 - sv) & 7);
       j = (lw - 9) >> 3;
       i = (lw - 1 - sv) >> 3;
       m = (lw & 7) ? (1U << (lw & 7)) - 1U : 255;
       bld->append("// a\n");
       if (i < ((lw-1) >> 3))
	{ if (lw & 7)
	   { bld->append("// b\n");
	     if (((lw-1) >> 3) - i > 1) bld->appendFormat(" __builtin_memset(&rv.bits[%u],(v.bits[%u]&%u)?255:0,%u);\n",i+1,(lw-1)>>3,1U<<((lw-1)&7),((lw-1)>>3)-i-1);
	     bld->appendFormat(" rv.bits[%u] = (v.bits[%u] & %u) ? %u : 0;\n",(lw-1)>>3,(lw-1)>>3,1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	     m = 255;
	   }
	  else
	   { bld->append("// c\n");
	     bld->appendFormat(" __builtin_memset(&rv.bits[%u],(v.bits[%u]&%u)?255:0,%u);\n",i+1,(lw-1)>>3,1U<<((lw-1)&7),((lw-1)>>3)-i);
	   }
	}
       bld->append("// d\n");
       for (;i>0;i--,j--)
	{ bld->appendFormat(" a = (a << 8) | v.bits[%u];\n",j);
	  bld->appendFormat(" rv.bits[%u] = (a >> %u) & %u;\n",i,s,m);
	  m = 255;
	}
       if (s == 8)
	{ bld->append("// e\n");
	  bld->appendFormat(" rv.bits[%u] = a & %u;\n",i,m);
	}
       else if (s > 8)
	{ bld->append("// f\n");
	  bld->appendFormat(" rv.bits[%u] = (a >> %u) & %u;\n",i,s-8,m);
	}
       else
	{ bld->append("// g\n");
	  bld->appendFormat(" a = (a << 8) | v.bits[%u];\n",j);
	  bld->appendFormat(" rv.bits[%u] = (a >> %u) & %u;\n",i,s,m);
	}
     }
    bld->append(" return(rv);\n");
  }
 bld->append(/*{*/"}\n");
}

static void gen_sh_x_shvar(
	BUILDER *bld,
	unsigned int lw, unsigned int rw,
	const char *sharg, const char *shint,
	const char **shvarr )
{
 const char *indent;
 int s_set;
 int i;

 bld->append(" do\n");
 indent = "  { "/*}*/;
 if (rw <= 64)
  { // We assume at least one of the next two if blocks will run;
    //  we don't care about the case where lw >= 2^64.
    if (rw < 64)
     { bld->appendFormat("%s%s &= %lluULL;\n",indent,sharg,(1ULL<<rw)-1ULL);
       indent = "    ";
     }
    if ((rw > 63) || (lw < (1ULL<<rw)-1ULL))
     { bld->appendFormat("%sif (%s >= %u) break;\n",indent,sharg,lw);
       // indent = "    "; don't bother; it isn't used further
     }
    *shvarr = sharg;
  }
 else
  { if (rw & 7)
     { bld->appendFormat("%s%s = %s.bits[%d] & %d;\n",indent,shint,sharg,rw>>3,(1<<(rw&7))-1);
       indent = "    ";
       if ((1U<<(rw&7))-1U >= lw) bld->appendFormat("    if (%s >= %u) break;\n",shint,lw);
       s_set = 1;
     }
    else
     { s_set = 0;
     }
    // rw > 64 here, so this loop runs at least once
    for (i=(rw-1)>>3;i>=0;i--)
     { bld->appendFormat("%s%s = ",indent,shint);
       indent = "    ";
       if (s_set) bld->appendFormat("(%s << 8) | (",shint);
       bld->appendFormat("%s.bits[%d]%s;\n",sharg,i,s_set?")":"");
       bld->appendFormat("    if (%s >= %u) break;\n",shint,lw);
       s_set = 1;
     }
    *shvarr = shint;
  }
}

static void gen_shl_x(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int lw;
 unsigned int rw;
 const char *shvar;

 assert(wr->type == WR_SHL_X);
 lw = wr->shift_x.lw;
 rw = wr->shift_x.rw;
 assert((lw > 64) || (rw > 64));
 bld->newline();
 append_type_for_width(bld,lw);
 bld->appendFormat(" shl_%u_x_%u(",lw,rw);
 append_type_for_width(bld,lw);
 bld->append(" v, ");
 append_type_for_width(bld,rw);
 bld->append(" sh)\n");
 bld->append("{\n"/*}*/);
 if (rw > 64) bld->append(" unsigned int s;\n");
 if (lw > 64) bld->appendFormat(" struct internal_bit_%u rv = randinit();\n",lw);
 bld->append(" unsigned int i;\n");
 bld->append(" u16 a;\n");
 bld->append(" unsigned int left;\n");
 bld->append(" unsigned int o;\n");
 bld->newline();
 gen_sh_x_shvar(bld,lw,rw,"sh","s",&shvar);
 // the value in the variable named by shvar is < lw, now
 // (but we can't assert() that; it's a packet-processing-time thing)
 if (lw < 64)
  { bld->appendFormat("    return((v<<%s)&%llu);\n",shvar,(1ULL<<lw)-1ULL);
  }
 else if (lw == 64)
  { bld->appendFormat("    return(v<<%s);\n",shvar);
  }
 else
  { if (rw > 3)
     { bld->appendFormat("    for (o=0;%s>=8;o++,%s-=8) rv.bits[o] = 0;\n",shvar,shvar);
       bld->appendFormat("    a = 0;\n");
       bld->appendFormat("    left = %u - (8 * o);\n",lw);
       bld->append      ("    for (i=0;left>=8;left-=8)\n");
     }
    else
     { bld->appendFormat("    a = 0;\n");
       bld->appendFormat("    for (i=0,o=0,left=%u;left>=8;left-=8)\n",lw);
     }
    bld->appendFormat("     { a = (a >> 8) | (v.bits[i++] << %s);\n"/*}*/,shvar);
    bld->append      ("       rv.bits[o++] = a & 255;\n");
    bld->append (/*{*/"     }\n");
    bld->append      ("    if (left)\n");
    bld->appendFormat("     { if (left > %s) a = (a >> 8) | (v.bits[i++] << %s); else a >>= 8;\n"/*}*/,shvar,shvar);
    bld->append      ("       rv.bits[o++] = a & ((1U << left) - 1);\n");
    bld->append (/*{*/"     }\n");
    bld->append      ("    return(rv);\n");
  }
 bld->append(/*{*/"  } while (0);\n");
 bld->append(" return(");
 if (lw <= 64)
  { bld->append("0");
  }
 else
  { bld->appendFormat("(struct internal_bit_%u){{0}}",lw);
  }
 bld->append(");\n");
 bld->append(/*{*/"}\n");
}

static void gen_shrl_x(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int lw;
 unsigned int rw;
 const char *shvar;

 assert(wr->type == WR_SHRL_X);
 lw = wr->shift_x.lw;
 rw = wr->shift_x.rw;
 assert((lw > 64) || (rw > 64));
 bld->newline();
 append_type_for_width(bld,lw);
 bld->appendFormat(" shrl_%u_x_%u(",lw,rw);
 append_type_for_width(bld,lw);
 bld->append(" v, ");
 append_type_for_width(bld,rw);
 bld->append(" sh)\n");
 bld->append("{\n"/*}*/);
 if (rw > 64) bld->append(" unsigned int s;\n");
 if (lw > 64) bld->appendFormat(" struct internal_bit_%u rv = randinit();\n",lw);
 bld->append(" u16 a;\n");
 bld->append(" unsigned int i;\n");
 bld->append(" unsigned int o;\n");
 bld->newline();
 gen_sh_x_shvar(bld,lw,rw,"sh","s",&shvar);
 // the value in the variable named by shvar is < lw, now
 // (but we can't assert() that; it's a packet-processing-time thing)
 if (lw <= 64)
  { bld->appendFormat("    return(v>>%s);\n",shvar);
  }
 else
  { bld->appendFormat("    i = %s >> 3;\n",shvar);
    bld->appendFormat("    %s = 8 - (%s & 7ULL);\n",shvar,shvar);
    bld->appendFormat("    a = v.bits[i++] << %s;\n",shvar);
    bld->append      ("    o = 0;\n");
    bld->appendFormat("    for (;i<%u;i++)\n",(lw+7)>>3);
    bld->appendFormat("     { a = (a >> 8) | (v.bits[i] << %s);\n"/*}*/,shvar);
    bld->appendFormat("       rv.bits[o++] = a & 255;\n");
    bld->append      (/*{*/"     }\n");
    if (lw & 7) bld->appendFormat("    a &= %u << %s;\n",(1U<<(lw&7))-1U,shvar);
    bld->append      ("    rv.bits[o++] = a >> 8;\n");
    bld->appendFormat("    for (;o<%u;o++) rv.bits[o] = 0;\n",(lw+7)>>3);
    bld->append      ("    return(rv);\n");
  }
 bld->append(/*{*/"  } while (0);\n");
 bld->append(" return(");
 if (lw <= 64)
  { bld->append("0");
  }
 else
  { bld->appendFormat("(struct internal_bit_%u){{0}}",lw);
  }
 bld->append(");\n");
 bld->append(/*{*/"}\n");
}

static void gen_shra_x(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int lw;
 unsigned int rw;
 const char *shvar;
 int i;

 assert(wr->type == WR_SHRA_X);
 lw = wr->shift_x.lw;
 rw = wr->shift_x.rw;
 assert((lw > 64) || (rw > 64));
 bld->newline();
 append_type_for_width(bld,lw);
 bld->appendFormat(" shra_%u_x_%u(",lw,rw);
 append_type_for_width(bld,lw);
 bld->append(" v, ");
 append_type_for_width(bld,rw);
 bld->append(" sh)\n");
 bld->append("{\n"/*}*/);
 if (rw > 64) bld->append(" unsigned int s;\n");
 if (lw > 64) bld->appendFormat(" struct internal_bit_%u rv = randinit();\n",lw);
 bld->append(" u16 a;\n");
 bld->append(" unsigned int i;\n");
 bld->append(" unsigned int o;\n");
 bld->append(" u8 sign;\n");
 bld->newline();
 gen_sh_x_shvar(bld,lw,rw,"sh","s",&shvar);
 // the value in the variable named by shvar is < lw, now
 // (but we can't assert() that; it's a packet-processing-time thing)
 if (lw <= 64)
  { bld->appendFormat("    return((v>>%s)|((v&%lluULL)?(((1ULL<<%s)-1ULL)<<(%u-%s)):0));\n",shvar,1ULL<<(lw-1),shvar,lw,shvar);
  }
 else
  { bld->appendFormat("    if (! %s) return(v);\n",shvar);
    bld->appendFormat("    sign = (v.bits[%u] & %u) ? 255 : 0;\n",(lw-1)>>3,1U<<((lw-1)&7));
    bld->appendFormat("    i = %s >> 3;\n",shvar);
    bld->appendFormat("    %s = 8 - (%s & 7ULL);\n",shvar,shvar);
    if (lw & 7)
     { bld->appendFormat("    a = ((i == %u) ? (v.bits[%u] & %u) | ((sign << %u) & 255) : v.bits[i]) << %s;\n",
		(lw-1)>>3, (lw-1)>>3, (1U<<(lw&7))-1U, lw&7, shvar);
       bld->append      ("    i ++;\n");
       bld->append      ("    o = 0;\n");
       bld->appendFormat("    for (;i<%u;i++)\n",(lw-1)>>3);
       bld->appendFormat("     { a = (a >> 8) | (v.bits[i] << %s);\n"/*}*/,shvar);
       bld->appendFormat("       rv.bits[o++] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->appendFormat("    if (i == %u)\n",(lw-1)>>3);
       bld->appendFormat("     { a = (a >> 8) | ((v.bits[%u] & %u) << %s) | (sign << (%s + %u));\n"/*}*/,
		(lw-1)>>3, (1U<<(lw&7))-1U, shvar, shvar, lw&7);
       bld->append      ("       rv.bits[o] = a & 255;\n");
       bld->append      ("       o ++;\n");
       bld->append      (/*{*/"     }\n");
       bld->appendFormat("    a |= sign << (8 + %s);\n",shvar);
       bld->appendFormat("    if (o == %u)\n",(lw-1)>>3);
       bld->appendFormat("     { rv.bits[o++] = (a >> 8) & %u;\n"/*}*/,(1U<<(lw&7))-1U);
       bld->append      (/*{*/"     }\n");
       bld->append      ("    else\n");
       bld->append      ("     { rv.bits[o++] = (a >> 8) & 255;\n"/*}*/);
       bld->appendFormat("       for (;o<%u;o++) rv.bits[o] = sign;\n",(lw-1)>>3);
       bld->appendFormat("       rv.bits[o] = sign & %u;\n",(1U<<(lw&7))-1U);
       bld->append      (/*{*/"     }\n");
     }
    else
     { bld->appendFormat("    a = v.bits[i++] << %s;\n",shvar);
       bld->append      ("    o = 0;\n");
       bld->appendFormat("    for (;i<%u;i++)\n",(lw+7)>>3);
       bld->appendFormat("     { a = (a >> 8) | (v.bits[i] << %s);\n"/*}*/,shvar);
       bld->appendFormat("       rv.bits[o++] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->appendFormat("    if (o < %u)\n",(lw+7)>>3);
       bld->appendFormat("     { a = (a >> 8) | (sign << %s);\n"/*}*/,shvar);
       bld->appendFormat("       rv.bits[o++] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->appendFormat("    for (;o<%u;o++) rv.bits[o] = sign;\n",(lw+7)>>3);
     }
    bld->append      ("    return(rv);\n");
  }
 bld->append(/*{*/"  } while (0);\n");
 bld->append(" return(");
 if (lw <= 64)
  { bld->appendFormat("(v&%lluULL)?%lluULL:0",1ULL<<(lw-1),((1ULL<<(lw-1))<<1)|1ULL);
  }
 else
  { bld->appendFormat("(v.bits[%u]&%u)?(struct internal_bit_%u){{"/*}}*/,(lw-1)>>3,1U<<((lw-1)&7),lw);
    for (i=(lw-1)>>3;i>0;i--) bld->append("255,");
    bld->appendFormat("%u",(1U<<(((lw-1)&7)+1))-1);
    bld->appendFormat(/*{{*/"}}:(struct internal_bit_%u){{0}}",lw);
  }
 bld->append(");\n");
 bld->append(/*{*/"}\n");
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
    fprintf(bf," if (s == 0) return(v);\n");
    fprintf(bf," r = (struct internal_bit_%d){{0}};\n",w);
    fprintf(bf," if (s < %d)\n",w);
    fprintf(bf,"  { for (i=%d,j=%d-s;j>=0;i--,j--) if ((v.bits[j>>3]>>(j&7)) & 1) r.bits[i>>3] |= 1 << (i & 7);\n"/*}*/,w-1,w-1);
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
    fprintf(bf," if (s == 0) return(v);\n");
    fprintf(bf," r = (struct internal_bit_%d){{0}};\n",w);
    fprintf(bf," if (s < %d)\n",w);
    fprintf(bf,"  { for (i=%d,j=%d-s;j>=0;i--,j--)\n"/*}*/,w-1,w-1);
    fprintf(bf,"    if ((v.bits[i>>3]>>(i&7)) & 1) r.bits[j>>3] |= 1 << (j & 7);\n");
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
    fprintf(bf," if (s == 0) return(v);\n");
    fprintf(bf," r = (struct internal_bit_%d){{0}};\n",w);
    if ((w & 7) == 1)
     { fprintf(bf," sb = v.bits[%d] & 1;\n",w>>3);
     }
    else
     { fprintf(bf," sb = (v.bits[%d] >> %d) & 1;\n",(w-1)>>3,(w+7)&7);
     }
    fprintf(bf,"  { for (i=%d;i>=0;i--)\n"/*}*/,w-1);
    fprintf(bf,"    if ((i >= %d-s) ? sb : ((v.bits[(i+s)>>3] >> ((i+s)&7)) & 1)) r.bits[i>>3] |= 1 << (i & 7);\n",w-1);
    fprintf(bf,/*{*/"  }\n");
    fprintf(bf," return(r);\n");
    fprintf(bf,/*{*/"}\n");
  }
}

static void gen_randinit(int lw)
{
 int i;

 if (lw <= 64) return;
 fprintf(bf,"static struct internal_bit_%d randinit(void)\n",lw);
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," return((struct internal_bit_%d){\n"/*}*/,lw);
 for (i=(lw+7)>>3;i>0;i--) fprintf(bf,"(random() >> 22) & 255,\n");
 fprintf(bf,/*{*/"});\n");
 fprintf(bf,/*{*/"}\n");
}

static void gen_init(void)
{
 fprintf(bf,"static unsigned int rseed;\n");
 fprintf(bf,"static unsigned int goodloops;\n");
 fprintf(bf,"static unsigned int skiploops;\n");
 fprintf(bf,"static void init(int ac, char **av)\n");
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
}

static void gen_c_tester(const char *kind, int lw, int sv, const char *optext)
{
 if (lw <= 64) abort();
 fprintf(bf,"\n");
 fprintf(bf,"static void fail(struct internal_bit_%d v, struct internal_bit_%d s, struct internal_bit_%d f, int sh)\n",lw,lw,lw);
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," printf(\"seed = %%u, loops = %%d\\n\",rseed,goodloops);\n");
 fprintf(bf," printf(\"v = \");");
 fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",v.bits[i]);\n",(lw-1)>>3);
 fprintf(bf," printf(\" %s %%d:\\n\",sh);",optext);
 fprintf(bf," printf(\"s = \");");
 fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",s.bits[i]);\n",(lw-1)>>3);
 fprintf(bf," printf(\"\\n\");");
 fprintf(bf," printf(\"f = \");");
 fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",f.bits[i]);\n",(lw-1)>>3);
 fprintf(bf," printf(\"\\n\");\n");
 fprintf(bf," exit(1);\n");
 fprintf(bf,/*{*/"}\n");
 fprintf(bf,"\n");
 fprintf(bf,"int main(void);\n");
 fprintf(bf,"int main(void)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," struct internal_bit_%d v;\n",lw);
 fprintf(bf," struct internal_bit_%d s;\n",lw);
 fprintf(bf," struct internal_bit_%d f;\n",lw);
 fprintf(bf," int i;\n");
 fprintf(bf," int j;\n");
 fprintf(bf,"\n");
#ifndef __linux__
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)1);\n");
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)3);\n");
#endif
 fprintf(bf," srandom(time(0)^getpid());\n");
 fprintf(bf," for (i=%d;i>0;i--)\n",TESTCASES);
 fprintf(bf,"  { for (j=%d;j>=0;j--) v.bits[j] = (random() >> 22) & 255;\n"/*}*/,(lw-1)>>3);
 if (lw & 7) fprintf(bf,"    v.bits[%d] &= %d;\n",(lw-1)>>3,(1<<(lw&7))-1);
 fprintf(bf,"    if (skiploops-- == 0) bkpt();\n");
 fprintf(bf,"    s = slow(v,%d);\n",sv);
 fprintf(bf,"    f = %s_%d_c_%d(v);\n",kind,lw,sv);
 fprintf(bf,"    if (bcmp(&s.bits[0],&f.bits[0],%d)) fail(v,s,f,%d);\n",(lw+7)>>3,sv);
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
 fprintf(bf,"static void fail(");
 append_type_for_width(&builder,lw);
 fprintf(bf," v, ");
 append_type_for_width(&builder,lw);
 fprintf(bf," s, ");
 append_type_for_width(&builder,lw);
 fprintf(bf," f, ");
 append_type_for_width(&builder,rw);
 fprintf(bf, " sh)\n");
 fprintf(bf,"{\n"/*}*/);
 fprintf(bf," int i;\n");
 fprintf(bf,"\n");
 fprintf(bf," printf(\"seed = %%u, loops = %%d\\n\",rseed,goodloops);\n");
 if (lw <= 64)
  { fprintf(bf," printf(\"v = 0x%%0%ullx\",v);\n",(lw+3)>>2);
  }
 else
  { fprintf(bf," printf(\"v = \");\n");
    fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",v.bits[i]);\n",(lw-1)>>3);
  }
 if (rw <= 64)
  { fprintf(bf," printf(\" %s 0x%%0%ullx:\\n\",(unsigned long long int)sh);",optext,(rw+3)>>2);
  }
 else
  { fprintf(bf," printf(\" %s 0x\");\n",optext);
    fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",sh.bits[i]);\n",(rw-1)>>3);
    fprintf(bf," printf(\":\\n\");\n");
  }
 if (lw <= 64)
  { fprintf(bf," printf(\"s = 0x%%0%ullx\\n\",s);\n",(lw+3)>>2);
    fprintf(bf," printf(\"f = 0x%%0%ullx\\n\",f);\n",(lw+3)>>2);
  }
 else
  { fprintf(bf," printf(\"s = \");\n");
    fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",s.bits[i]);\n",(lw-1)>>3);
    fprintf(bf," printf(\"\\n\");\n");
    fprintf(bf," printf(\"f = \");\n");
    fprintf(bf," for (i=%d;i>=0;i--) printf(\"%%02x\",f.bits[i]);\n",(lw-1)>>3);
    fprintf(bf," printf(\"\\n\");\n");
  }
 fprintf(bf," exit(1);\n");
 fprintf(bf,/*{*/"}\n");
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
 fprintf(bf,"\n");
#ifndef __linux__
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)1);\n");
 fprintf(bf," wait4(0x456d756c,(void *)0x4d616769,0x633a2d29,(void *)3);\n");
#endif
 fprintf(bf," init(ac,av);\n");
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
  { fprintf(bf,"for (j=%d;j>=0;j--) v.bits[j] = (random() >> 22) & 255;\n",(lw-1)>>3);
    if (lw & 7) fprintf(bf,"    v.bits[%d] &= %d;\n",(lw-1)>>3,(1<<(lw&7))-1);
  }
 if (sra)
  { if (lw <= 64)
     { fprintf(bf,"    sign = (v >> %u) & 1;\n",lw-1);
     }
    else
     { if ((lw - 1) & 7)
	{ fprintf(bf,"    sign = (v.bits[%u] >> %u) & 1;\n",(lw-1)>>3,(lw-1)&7);
	}
       else
	{ fprintf(bf,"    sign = v.bits[%u] & 1;\n",(lw-1)>>3);
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
    for (i=rw-1,j=0;i;i>>=8,j++)
     { if (j) fprintf(bf,"       j >>= 8;\n");
       fprintf(bf,"       sh.bits[%d] = j & 255;\n",j);
     }
    for (i=(rw+7)>>3;j<i;j++) fprintf(bf,"       sh.bits[%d] = 0;\n",j);
    fprintf(bf,/*{*/"     }\n");
    fprintf(bf,"    else\n");
    fprintf(bf,"     { for (k=0;k<%d;k++) sh.bits[k] = (random() >> 22) & 255;\n"/*}*/,(rw+7)>>3);
    fprintf(bf,/*{*/"     }\n");
    fprintf(bf,"    sh_i = 0;\n");
    fprintf(bf,"    for (j=%u;j>=0;j--)\n",(rw-1)>>3);
    fprintf(bf,"     { sh_i = (sh_i << 8) | sh.bits[j];\n"/*}*/);
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
  { if (sra)
     { fprintf(bf,"sign ? (");
       append_type_for_width(&builder,lw);
       fprintf(bf,"){"/*}*/);
       for (i=(lw-1)>>3;i>0;i--) fprintf(bf,"255,");
       fprintf(bf,"%u",(1U<<(((lw-1)&7)+1))-1);
       fprintf(bf,/*{*/"} : ");
     }
    fprintf(bf,"(");
    append_type_for_width(&builder,lw);
    fprintf(bf,"){0}");
  }
 fprintf(bf," : slow(v,sh_i);\n");
 fprintf(bf,"    f = %s_%d_x_%d(v,sh);\n",kind,lw,rw);
 fprintf(bf,"    if (");
 if (lw <= 64)
  { fprintf(bf,"s != f");
  }
 else
  { fprintf(bf,"bcmp(&s.bits[0],&f.bits[0],%d)",(lw+7)>>3);
  }
 fprintf(bf,") fail(v,s,f,sh);\n");
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
 gen_init();
 fprintf(bf,"\n");
 switch (wr->type)
  { case WR_SHL_C:
    case WR_SHRL_C:
    case WR_SHRA_C:
       gen_randinit(wr->shift_c.lw);
       break;
    case WR_SHL_X:
    case WR_SHRL_X:
    case WR_SHRA_X:
       gen_randinit(wr->shift_x.lw);
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
 run_cmd("/bin/cc","/bin/cc","-g","-o","foo","foo.c","-I..",(const char *)0);
#else
 run_cmd("/usr/bin/cc","cc","-g","-o","foo","foo.c","-I..",(const char *)0);
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
