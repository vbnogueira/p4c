/*
 * This code is here in a separate file so it can be shared between p4c
 *  and test-arith.c (if-and-when we have a test-arith.c, that is).
 *
 * This code requires, from its surrounding code:
 *
 *	- BUILDER must (possibly after macro-expansion) be the
 *		code-builder type, with ->newline, ->append, and
 *		->appendFormat functions/methods.
 *
 * The generated code similarly requires:
 *
 *	- BITS(x) takes a struct internal_bit_* and returns a pointer
 *		to the first of its data bytes.
 *
 *	- COPYGUARDS(x,y) copies the guard bytes, if present, from y to
 *		x.  (If there are no guard bytes, this will typically
 *		be a macro which expands to an empty do-while.)
 *
 *	- GUARDARGS expands to declarations of any trailing arguments
 *		needed by SETGUARDS (below).
 *
 *	- SETGUARDS(x) generates code to set random guard bytes on x,
 *		copying the guard bytes also to the arguments declared
 *		by GUARDARGS.  If there are no guard bytes, this
 *		typically will be an empty do-while.
 */

static void gen_add(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 unsigned int b;
 int i;

 assert(wr->type == WR_ADD);
 w = wr->arith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u add_%u(struct internal_bit_%u lhs, struct internal_bit_%u rhs GUARDARGS)\n",w,w,w,w);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 // really need only u9, but can't count on that existing, ugh
 // (for that matter, can count on u16 existing only pragmatically)
 bld->append(" u16 a;\n");
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 for (i=b-1;i>=0;i--)
  { bld->appendFormat(" a = BITS(lhs)[%d] + BITS(rhs)[%d]%s;\n",i,i,i?" + (a >> 8)":"");
    bld->appendFormat(" BITS(ret)[%d] = a & ",i);
    if (i > 0) bld->append("255"); else bld->appendFormat("%u",255>>((b*8)-w));
    bld->append(";\n");
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

static void gen_sub(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 unsigned int b;
 int i;

 assert(wr->type == WR_SUB);
 w = wr->arith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u sub_%u(struct internal_bit_%u lhs, struct internal_bit_%u rhs GUARDARGS)\n",w,w,w,w);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 // really need only u9, but can't count on that existing, ugh
 // (for that matter, can count on u16 existing only pragmatically)
 bld->append(" u16 a;\n");
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 for (i=b-1;i>=0;i--)
  { bld->appendFormat(" a = BITS(lhs)[%u] - BITS(rhs)[%u]%s;\n",i,i,i?" - ((a >> 8) & 1)":"");
    bld->appendFormat(" BITS(ret)[%u] = a & ",i);
    if (i > 0) bld->append("255"); else bld->appendFormat("%u",255>>((b*8)-w));
    bld->append(";\n");
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

static void gen_mul(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 unsigned int b;
 unsigned int i;
 int j;

 assert(wr->type == WR_MUL);
 w = wr->arith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u mul_%u(struct internal_bit_%u lhs, struct internal_bit_%u rhs)\n",w,w,w,w);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 bld->append(" u32 a;\n");
 bld->append("\n");
 for (i=0;i<b;i++)
  { bld->appendFormat(" a =%s",i?" (a >> 8) +":"");
    for (j=i;j>=0;j--)
     { bld->appendFormat(" (BITS(lhs)[%d] * BITS(rhs)[%d])%s",j,(int)i-j,j?" +":"");
     }
    bld->append(";\n");
    bld->appendFormat(" BITS(ret)[%u] = a & ",i);
    if (i+1 < b) bld->append("255"); else bld->appendFormat("%u",255U>>((b*8)-w));
    bld->append(";\n");
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

static void gen_bxsmul(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int bw;
 unsigned int sv;
 unsigned int b;
 unsigned int i;

 assert(wr->type == WR_BXSMUL);
 bw = wr->bxsmul.bw;
 sv = wr->bxsmul.sv;
 b = (bw + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u bxsmul_%u_%u(struct internal_bit_%u arg)\n",bw,bw,sv,bw,bw);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",bw);
 bld->append(" u32 a;\n");
 bld->append("\n");
 for (i=0;i<b;i++)
  { bld->appendFormat(" a =%s (BITS(arg)[%u] * %u);\n",i?" (a >> 8) +":"",i,sv);
    bld->appendFormat(" BITS(ret)[%u] = a & ",i);
    if (i+1 < b) bld->append("255"); else bld->appendFormat("%u",255U>>((b*8)-bw));
    bld->append(";\n");
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

static void gen_neg(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 int i;
 int j;

 assert(wr->type == WR_NEG);
 w = wr->arith.w;
 assert(w > 64);
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u neg_%u(struct internal_bit_%u arg)\n",w,w,w);
 bld->append("{\n"/*}*/);
 bld->append(" u16 a;\n");
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 bld->append("\n");
 for (i=(w>>3)-1,j=0;i>=0;i--,j++)
  { bld->appendFormat(" a = %s + (255 ^ BITS(arg)[%d]);\n",j?"(a >> 8)":"1",j);
    bld->appendFormat(" BITS(ret)[%d] = a & 255;\n",j);
  }
 if (w & 7)
  { bld->appendFormat(" BITS(ret)[%d] = (%s + (255 ^ BITS(arg)[%d])) & %d;\n",j?"(a >> 8)":"1",j,j,(1<<(w&7))-1);
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

/*
 * Signed addition overflows exactly when the arguments have the same
 *  sign but the (overflow-ignored) result has a different sign,
 *  implemented here as ~(lhs^rhs) & (ret^lhs) & signbit.
 *
 * Unsigned addition overflows exactly when there is a carry out of the
 *  high bit; equivalently, if the (overflow-ignored) result is less
 *  than at least one of the input operands.
 */
static void gen_addsat(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 int b;
 int i;

 assert(wr->type == WR_ADDSAT);
 w = wr->sarith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->append("static __always_inline ");
 append_type_for_width(bld,w);
 bld->appendFormat(" addsat_%d(",w);
 append_type_for_width(bld,w);
 bld->append(" lhs, ");
 append_type_for_width(bld,w);
 bld->append(" rhs)\n");
 bld->append("{\n"/*}*/);
 bld->append(" ");
 append_type_for_width(bld,w);
 bld->append(" ret;\n");
 bld->append("\n");
 if (w <= 64)
  { unsigned long long int max;
    max = (w < 64) ? (1ULL << w) - 1ULL : 0xffffffffffffffffULL;
    // let the optimizer delete the &0x...ULL when appropriate
    bld->appendFormat(" ret = (lhs + rhs) & 0x%llxULL;\n",max);
    if (wr->sarith.issigned)
     { bld->appendFormat(" if (~(lhs ^ rhs) & (ret ^ lhs) & (1ULL << %u)) ret = (lhs & (1ULL << %u)) ? 0x%llxULL : 0x%llxULL;\n",
		w-1, w-1, max&~(max>>1), max);
     }
    else
     { bld->appendFormat(" if ((ret < lhs) || (ret < rhs)) ret = 0x%llxULL;\n",max);
     }
  }
 else
  { // really need only u9, but can't count on that existing, ugh
    // (for that matter, can count on u16 existing only pragmatically)
    bld->append(" u16 a;\n");
    bld->append("\n");
    for (i=0;i<b;i++)
     { bld->appendFormat(" a = BITS(lhs)[%d] + BITS(rhs)[%d]%s;\n",i,i,i?" + (a >> 8)":"");
       bld->appendFormat(" BITS(ret)[%d] = a & ",i);
       if (i+1 < b) bld->append("255"); else bld->appendFormat("%d",255>>((b*8)-w));
       bld->append(";\n");
     }
    if (wr->sarith.issigned)
     { unsigned int signbit;
       signbit = 128U >> ((b * 8) - w);
       bld->appendFormat(" if (~(BITS(lhs)[%u] ^ BITS(rhs)[%u]) & (a ^ BITS(lhs)[%u]) & %u)\n",b-1,b-1,b-1,signbit);
       bld->appendFormat("  { if (BITS(lhs)[%u] & %u)\n"/*}*/,b-1,signbit);
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[0],0,%u);\n"/*}*/,b-1);
       bld->appendFormat("       BITS(ret)[%u] = %u;\n",b-1,signbit);
       bld->append(/*{*/"     }\n");
       bld->append("    else\n");
       if (w % 8)
	{ bld->appendFormat("     { __builtin_memset(&BITS(ret)[0],255,%u);\n"/*}*/,b-1);
	  bld->appendFormat("       BITS(ret)[%u] = %u;\n",b-1,signbit-1);
	  bld->append(/*{*/"     }\n");
	}
       else
	{ bld->appendFormat("     { __builtin_memset(&BITS(ret)[0],255,%u);\n"/*}*/,b);
	  bld->append(/*{*/"     }\n");
	}
       bld->append(/*{*/"  }\n");
     }
    else
     { bld->appendFormat(" if (a > %d)",255>>((b*8)-w));
       // we know w > 64, and thus b > 1, at this point
       if (w % 8)
	{ bld->append("\n");
	  bld->appendFormat("  { __builtin_memset(&BITS(ret)[0],255,%u);\n"/*}*/,b-1);
	  bld->appendFormat("    BITS(ret)[%u] = %u;\n",b-1,255>>((b*8)-w));
	  bld->append(/*{*/"  }\n");
	}
       else
	{ bld->appendFormat(" __builtin_memset(&BITS(ret)[0],255,%u);\n",b);
	}
     }
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

/*
 * Signed subtraction overflows exactly when the arguments have
 *  different signs and the result's sign equals the RHS's sign,
 *  implemented here as (lhs^rhs) & ~(ret^rhs) & signbit.
 *
 * Unsigned subtraction overflows exactly when the RHS is greater than
 *  the LHS.  For non-multioctet operations, this is easy to test.  We
 *  implement multioctet unsigned subtraction as unsigned addition with
 *  the RHS complemented and a carry-in of 1 into the low byte; the
 *  subtraction then overflows exactly when there is *no* carry out of
 *  the top bit.
 */
static void gen_subsat(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 int b;
 int i;

 assert(wr->type == WR_SUBSAT);
 w = wr->sarith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->append("static __always_inline ");
 append_type_for_width(bld,w);
 bld->appendFormat(" subsat_%d(",w);
 append_type_for_width(bld,w);
 bld->append(" lhs, ");
 append_type_for_width(bld,w);
 bld->append(" rhs)\n");
 bld->append("{\n"/*}*/);
 bld->append(" ");
 append_type_for_width(bld,w);
 bld->append(" ret;\n");
 bld->append("\n");
 if (w <= 64)
  { unsigned long long int max;
    max = (w < 64) ? (1ULL << w) - 1ULL : 0xffffffffffffffffULL;
    if (wr->sarith.issigned)
     { // let the optimizer delete the &0x...ULL when appropriate
       bld->appendFormat(" ret = (lhs - rhs) & 0x%llxULL;\n",max);
       bld->appendFormat(" if ((lhs ^ rhs) & ~(ret ^ rhs) & (1ULL << %u)) ret = (ret & (1ULL << %u)) ? 0x%llxULL : 0x%llxULL;\n",
		w-1, w-1, max, max&~(max>>1));
     }
    else
     { bld->appendFormat(" ret = (rhs > lhs) ? 0 : lhs - rhs;\n",max);
     }
  }
 else
  { unsigned int signbit;
    signbit = 128U >> ((b * 8) - w);
    // really need only u9, but can't count on that existing, ugh
    // (for that matter, can count on u16 existing only pragmatically)
    bld->append(" u16 a;\n");
    bld->append("\n");
    for (i=0;i<b;i++)
     { bld->appendFormat(" a = BITS(lhs)[%d] + ~BITS(rhs)[%d] + %s;\n",i,i,i?"(a >> 8)":"1");
       bld->appendFormat(" BITS(ret)[%d] = a & ",i);
       if (i+1 < b) bld->append("255"); else bld->appendFormat("%d",255>>((b*8)-w));
       bld->append(";\n");
     }
    if (wr->sarith.issigned)
     { bld->appendFormat(" if ((BITS(lhs)[%u] ^ BITS(rhs)[%u]) & ~(a ^ BITS(rhs)[%u]) & %u)\n",b-1,b-1,b-1,signbit);
       bld->appendFormat("  { if (a & %u)\n"/*}*/,signbit);
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[0],255,%u);\n"/*}*/,b-1);
       bld->appendFormat("       BITS(ret)[%u] = %u;\n",b-1,signbit-1);
       bld->append(/*{*/"     }\n");
       bld->append("    else\n");
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[0],0,%u);\n"/*}*/,b-1);
       bld->appendFormat("       BITS(ret)[%u] = %u;\n",b-1,signbit);
       bld->append(/*{*/"     }\n");
       bld->append(/*{*/"  }\n");
     }
    else
     { bld->appendFormat(" if (a <= %u) __builtin_memset(&BITS(ret)[0],0,%u);\n",signbit|(signbit-1),b);
     }
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}
