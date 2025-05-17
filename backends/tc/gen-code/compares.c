/*
 * This code is here in a separate file so it can be shared between p4c
 *  and test-compare.c (if-and-when we have a test-compare.c, that is).
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
 */

static void gen_cmp(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int w;

 assert(wr->type == WR_CMP);
 w = wr->cmp.w;
 bld->newline();
 bld->appendFormat("static __always_inline int cmp_%s_%u_%s(internal_bit_%u l, internal_bit_%u r)\n",
	(wr->cmp.cmp & CMP_SIGNED) ? "s" : "u",
	w,
	cmp_string(wr->cmp.cmp&CMP_BASE),
	w,
	w );
 bld->append("{\n"/*}*/);
 switch (wr->cmp.cmp)
  { case CMP_EQ:
       bld->append(" return(");
       if (w & 7) bld->appendFormat("!((BITS(l)[%u]^BITS(r)[%u])&%u)&&",w>>3,w>>3,(1U<<(w&7))-1U);
       bld->appendFormat("!__builtin_memcmp(&BITS(l)[0],&BITS(r)[0],%u));",w>>3);
       break;
    case CMP_NE:
       bld->append(" return(");
       if (w & 7) bld->appendFormat("((BITS(l)[%u]^BITS(r)[%u])&%u)||",w>>3,w>>3,(1U<<(w&7))-1U);
       bld->appendFormat("__builtin_memcmp(&BITS(l)[0],&BITS(r)[0],%u));",w>>3);
       break;
	{ const char *t_op;
	  const char *f_op;
	  const char *pref;
	  const char *suff;
	  const char *postop;
	  unsigned int postnum;
	  int eqrv;
    case CMP_LT:              f_op = ">"; t_op = "<"; eqrv = 0; pref = ""; postop = "&"; suff = ""; postnum = (1U<<(w&7))-1U;   if (0) {
    case CMP_LE:              f_op = ">"; t_op = "<"; eqrv = 1; pref = ""; postop = "&"; suff = ""; postnum = (1U<<(w&7))-1U; } if (0) {
    case CMP_GT:              f_op = "<"; t_op = ">"; eqrv = 0; pref = ""; postop = "&"; suff = ""; postnum = (1U<<(w&7))-1U; } if (0) {
    case CMP_GE:              f_op = "<"; t_op = ">"; eqrv = 1; pref = ""; postop = "&"; suff = ""; postnum = (1U<<(w&7))-1U; } if (0) {
    case CMP_LT | CMP_SIGNED: f_op = ">"; t_op = "<"; eqrv = 0; pref = "((i8)"; postop = "<<"; suff = ")"; postnum = 8-(w&7); } if (0) {
    case CMP_LE | CMP_SIGNED: f_op = ">"; t_op = "<"; eqrv = 1; pref = "((i8)"; postop = "<<"; suff = ")"; postnum = 8-(w&7); } if (0) {
    case CMP_GT | CMP_SIGNED: f_op = "<"; t_op = ">"; eqrv = 0; pref = "((i8)"; postop = "<<"; suff = ")"; postnum = 8-(w&7); } if (0) {
    case CMP_GE | CMP_SIGNED: f_op = "<"; t_op = ">"; eqrv = 1; pref = "((i8)"; postop = "<<"; suff = ")"; postnum = 8-(w&7); }
	  bld->append(" int i;\n");
	  bld->append("\n");
	  if (w & 7)
	   { bld->appendFormat(" if (%s(BITS(l)[%u] %s %u)%s %s %s(BITS(r)[%u] %s %u)%s) return(1);\n",
			pref, w>>3, postop, postnum, suff,
			t_op,
			pref, w>>3, postop, postnum, suff );
	     bld->appendFormat(" if (%s(BITS(l)[%u] %s %u)%s %s %s(BITS(r)[%u] %s %u)%s) return(0);\n",
			pref, w>>3, postop, postnum, suff,
			f_op,
			pref, w>>3, postop, postnum, suff );
	   }
	  bld->appendFormat(" for (i=%u;i>=0;i--)\n",(w>>3)-1);
	  bld->appendFormat("  { if (BITS(l)[i] %s BITS(r)[i]) return(1);\n"/*}*/,t_op);
	  bld->appendFormat("    if (BITS(l)[i] %s BITS(r)[i]) return(0);\n",f_op);
	  bld->append(/*{*/"  }\n");
	  bld->appendFormat(" return(%d);\n",eqrv);
	}
       break;
  }
 bld->append(/*{*/"}\n");
}
