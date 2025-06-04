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
 unsigned int bits;
 unsigned int bytes;
 int i;
 const char *suf;

 assert(wr->type == WR_ADD);
 bits = wr->arith.w;
 bytes = (bits + 7) >> 3;
 assert(bits > 64);
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u add_%u(struct internal_bit_%u lhs, struct internal_bit_%u rhs GUARDARGS)\n",bits,bits,bits,bits);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",bits);
 // really need only u9, but can't count on that existing, ugh
 // (for that matter, can't count on u16 existing except pragmatically)
 bld->append(" u16 a;\n");
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 suf = "";
 for (i=bytes-1;i>=0;i--)
  { bld->appendFormat(" a = BITS(lhs)[%d] + BITS(rhs)[%d]%s;\n",i,i,suf);
    suf = " + (a >> 8)";
    bld->appendFormat(" BITS(ret)[%d] = a & ",i);
    if (i > 0) bld->append("255"); else bld->appendFormat("%u",255>>((bytes*8)-bits));
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
 const char *suf;

 assert(wr->type == WR_SUB);
 w = wr->arith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u sub_%u(struct internal_bit_%u lhs, struct internal_bit_%u rhs GUARDARGS)\n",w,w,w,w);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 // really need only u9, but can't count on that existing, ugh
 // (for that matter, can't count on u16 existing except pragmatically)
 bld->append(" u16 a;\n");
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 suf = "";
 for (i=b-1;i>=0;i--)
  { bld->appendFormat(" a = BITS(lhs)[%u] - BITS(rhs)[%u]%s;\n",i,i,suf);
    suf = " - ((a >> 8) & 1)";
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
 bld->appendFormat("static __always_inline struct internal_bit_%u mul_%u(struct internal_bit_%u lhs, struct internal_bit_%u rhs GUARDARGS)\n",w,w,w,w);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 bld->append(" u32 a;\n");
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 for (i=0;i<b;i++)
  { bld->appendFormat(" a =%s",i?" (a >> 8) +":"");
    for (j=i;j>=0;j--)
     { bld->appendFormat(" (BITS(lhs)[%d] * BITS(rhs)[%d])%s",b-1-j,b-1-((int)i-j),j?" +":"");
     }
    bld->append(";\n");
    bld->appendFormat(" BITS(ret)[%u] = a & ",b-1-i);
    if (i+1 < b) bld->append("255"); else bld->appendFormat("%u",255U>>((b*8)-w));
    bld->append(";\n");
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

static void gen_bxsmul(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int bw;
 unsigned long long int sv;
 unsigned int b;
 int i;
 const char *suf;

 assert(wr->type == WR_BXSMUL);
 bw = wr->bxsmul.bw;
 sv = wr->bxsmul.sv;
 b = (bw + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u bxsmul_%u_%llu(struct internal_bit_%u arg GUARDARGS)\n",bw,bw,sv,bw);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%u ret;\n",bw);
 if (sv > 0x007fffff)
  { bld->append(" u64 a;\n");
    suf = "ULL";
  }
 else
  { bld->append(" u32 a;\n");
    suf = "U";
  }
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 for (i=b-1;i>=0;i--)
  { bld->appendFormat(" a =%s (BITS(arg)[%u] * %llu%s);\n",(i<(int)b-1)?" (a >> 8) +":"",i,sv,suf);
    bld->appendFormat(" BITS(ret)[%u] = a & ",i);
    if (i > 0) bld->append("255"); else bld->appendFormat("%u",255U>>((b*8)-bw));
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
 int bytes;

 assert(wr->type == WR_NEG);
 w = wr->arith.w;
 assert(w > 64);
 bytes = (w + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u neg_%u(struct internal_bit_%u arg GUARDARGS)\n",w,w,w);
 bld->append("{\n"/*}*/);
 bld->append(" u16 a;\n");
 bld->appendFormat(" struct internal_bit_%u ret;\n",w);
 bld->append("\n");
 bld->append(" SETGUARDS(ret);\n");
 for (i=(w>>3)-1,j=0;i>=0;i--,j++)
  { bld->appendFormat(" a = %s + (255 ^ BITS(arg)[%d]);\n",j?"(a >> 8)":"1",bytes-1-j);
    bld->appendFormat(" BITS(ret)[%d] = a & 255;\n",bytes-1-j);
  }
 if (w & 7)
  { // always a>>8: w>64, so the above loop ran >once
    bld->appendFormat(" BITS(ret)[0] = ((a >> 8) + (255 ^ BITS(arg)[0])) & %d;\n",(1<<(w&7))-1);
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
 unsigned int bits;
 unsigned int bytes;
 int i;
 const char *suf;

 assert(wr->type == WR_ADDSAT);
 bits = wr->sarith.w;
 bytes = (bits + 7) >> 3;
 bld->newline();
 bld->append("static __always_inline ");
 append_type_for_width(bld,bits);
 bld->appendFormat(" addsat_%d(",bits);
 append_type_for_width(bld,bits);
 bld->append(" lhs, ");
 append_type_for_width(bld,bits);
 bld->append(" rhs");
 if (bits > 64) bld->append(" GUARDARGS");
 bld->append(")\n");
 bld->append("{\n"/*}*/);
 bld->append(" ");
 append_type_for_width(bld,bits);
 bld->append(" ret;\n");
 bld->append("\n");
 if (bits <= 64)
  { unsigned long long int max;
    max = (bits < 64) ? (1ULL << bits) - 1ULL : 0xffffffffffffffffULL;
    // let the optimizer delete the &0x...ULL when appropriate
    bld->appendFormat(" ret = (lhs + rhs) & 0x%llxULL;\n",max);
    if (wr->sarith.issigned)
     { bld->appendFormat(" if (~(lhs ^ rhs) & (ret ^ lhs) & (1ULL << %u)) ret = (lhs & (1ULL << %u)) ? 0x%llxULL : 0x%llxULL;\n",
		bits-1, bits-1, max&~(max>>1), max);
     }
    else
     { bld->appendFormat(" if ((ret < lhs) || (ret < rhs)) ret = 0x%llxULL;\n",max);
     }
  }
 else
  { // really need only u9, but can't count on that existing, ugh
    // (for that matter, can't count on u16 existing except pragmatically)
    bld->append(" u16 a;\n");
    bld->append("\n");
    bld->append(" SETGUARDS(ret);\n");
    suf = "";
    for (i=bytes-1;i>=0;i--)
     { bld->appendFormat(" a = BITS(lhs)[%d] + BITS(rhs)[%d]%s;\n",i,i,suf);
       suf = " + (a >> 8)";
       bld->appendFormat(" BITS(ret)[%d] = a & ",i);
       if (i > 0) bld->append("255"); else bld->appendFormat("%u",255>>((bytes*8)-bits));
       bld->append(";\n");
     }
    if (wr->sarith.issigned)
     { unsigned int signbit;
       signbit = 128U >> ((bytes * 8) - bits);
       bld->appendFormat(" if (~(BITS(lhs)[0] ^ BITS(rhs)[0]) & (a ^ BITS(lhs)[0]) & %u)\n",signbit);
       bld->appendFormat("  { if (BITS(lhs)[0] & %u)\n"/*}*/,signbit);
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[1],0,%u);\n"/*}*/,bytes-1);
       bld->appendFormat("       BITS(ret)[0] = %u;\n",signbit);
       bld->append(/*{*/"     }\n");
       bld->append("    else\n");
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[1],255,%u);\n"/*}*/,bytes-1);
       bld->appendFormat("       BITS(ret)[0] = %u;\n",signbit-1);
       bld->append(/*{*/"     }\n");
       bld->append(/*{*/"  }\n");
     }
    else
     { bld->appendFormat(" if (a > %d)",255>>((bytes*8)-bits));
       // we know bits > 64, and thus bytes > 1, at this point
       if (bits & 7)
	{ bld->append("\n");
	  bld->appendFormat("  { __builtin_memset(&BITS(ret)[1],255,%u);\n"/*}*/,bytes-1);
	  bld->appendFormat("    BITS(ret)[0] = %u;\n",255>>((bytes*8)-bits));
	  bld->append(/*{*/"  }\n");
	}
       else
	{ bld->appendFormat(" __builtin_memset(&BITS(ret)[0],255,%u);\n",bytes);
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
 unsigned int bits;
 int bytes;
 int i;

 assert(wr->type == WR_SUBSAT);
 bits = wr->sarith.w;
 bytes = (bits + 7) >> 3;
 bld->newline();
 bld->append("static __always_inline ");
 append_type_for_width(bld,bits);
 bld->appendFormat(" subsat_%d(",bits);
 append_type_for_width(bld,bits);
 bld->append(" lhs, ");
 append_type_for_width(bld,bits);
 bld->append(" rhs");
 if (bits > 64) bld->append(" GUARDARGS");
 bld->append(")\n");
 bld->append("{\n"/*}*/);
 bld->append(" ");
 append_type_for_width(bld,bits);
 bld->append(" ret;\n");
 bld->append("\n");
 if (bits <= 64)
  { unsigned long long int max;
    max = (bits < 64) ? (1ULL << bits) - 1ULL : 0xffffffffffffffffULL;
    if (wr->sarith.issigned)
     { // let the optimizer delete the &0x...ULL when appropriate
       bld->appendFormat(" ret = (lhs - rhs) & 0x%llxULL;\n",max);
       bld->appendFormat(" if ((lhs ^ rhs) & ~(ret ^ rhs) & (1ULL << %u)) ret = (ret & (1ULL << %u)) ? 0x%llxULL : 0x%llxULL;\n",
		bits-1, bits-1, max, max&~(max>>1));
     }
    else
     { bld->appendFormat(" ret = (rhs > lhs) ? 0 : lhs - rhs;\n",max);
     }
  }
 else
  { unsigned int signbit;
    signbit = 128U >> ((bytes * 8) - bits);
    // really need only u9, but can't count on that existing, ugh
    // (for that matter, can't count on u16 existing except pragmatically)
    bld->append(" u16 a;\n");
    bld->append("\n");
    bld->append(" SETGUARDS(ret);\n");
    for (i=bytes-1;i>=0;i--)
     { bld->appendFormat(" a = BITS(lhs)[%u] - BITS(rhs)[%u]%s;\n",i,i,i?" - ((a >> 8) & 1)":"");
       bld->appendFormat(" BITS(ret)[%u] = a & ",i);
       if (i > 0) bld->append("255"); else bld->appendFormat("%u",255>>((bytes*8)-bits));
       bld->append(";\n");
     }
    if (wr->sarith.issigned)
     { bld->appendFormat(" if ((BITS(lhs)[0] ^ BITS(rhs)[0]) & ~(a ^ BITS(rhs)[0]) & %u)\n",signbit);
       bld->appendFormat("  { if (a & %u)\n"/*}*/,signbit);
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[1],255,%u);\n"/*}*/,bytes-1);
       bld->appendFormat("       BITS(ret)[0] = %u;\n",signbit-1);
       bld->append(/*{*/"     }\n");
       bld->append("    else\n");
       bld->appendFormat("     { __builtin_memset(&BITS(ret)[1],0,%u);\n"/*}*/,bytes-1);
       bld->appendFormat("       BITS(ret)[0] = %u;\n",signbit);
       bld->append(/*{*/"     }\n");
       bld->append(/*{*/"  }\n");
     }
    else
     { bld->appendFormat(" if (a <= %u) __builtin_memset(&BITS(ret)[0],0,%u);\n",signbit|(signbit-1),bytes);
     }
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}
