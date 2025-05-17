/*
 * This code is here in a separate file so it can be shared between p4c
 *  and gen-arith.c (if-and-when we have a gen-arith.c, that is).
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
 */

static void gen_bitop(WRTYPE wrt, BUILDER *bld, const WIDTH_REC *wr, const char *name, const char *op)
{
 unsigned int w;
 int b;
 int i;

 assert(wr->type == wrt);
 w = wr->arith.w;
 b = (w + 7) >> 3;
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%d %s_%d(struct internal_bit_%d lhs, struct internal_bit_%d rhs)\n",w,name,w,w,w);
 bld->append("{\n"/*}*/);
 bld->appendFormat(" struct internal_bit_%d ret;\n",w);
 bld->append("\n");
 for (i=0;i<b;i++) bld->appendFormat(" ret.bits[%u] = lhs.bits[%u] %s rhs.bits[%u];\n",i,i,op,i);
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}

static void gen_bitand(BUILDER *bld, const WIDTH_REC *wr)
{
 gen_bitop(WR_BITAND,bld,wr,"bitand","&");
}

static void gen_bitor(BUILDER *bld, const WIDTH_REC *wr)
{
 gen_bitop(WR_BITOR,bld,wr,"bitor","|");
}

static void gen_bitxor(BUILDER *bld, const WIDTH_REC *wr)
{
 gen_bitop(WR_BITXOR,bld,wr,"bitxor","^");
}

static void gen_not(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;
 int i;
 int j;

 assert(wr->type == WR_NOT);
 w = wr->arith.w;
 assert(w > 64);
 bld->newline();
 bld->appendFormat("static __always_inline struct internal_bit_%u not_%u(struct internal_bit_%d arg)\n",w,w,w);
 bld->append("{\n"/*}*/);
 bld->append(" u16 a;\n");
 bld->append("\n");
 for (i=(w>>3)-1,j=0;i>=0;i--,j++)
  { bld->appendFormat(" ret.bits[%d] = ~arg.bits[%d];\n",j,j);
  }
 if (w & 7)
  { bld->appendFormat(" ret.bits[%d] = arg.bits[%d] ^ %d;\n",j,j,(1<<(w&7))-1);
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}
