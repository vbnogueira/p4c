/*
 * This code is here in a separate file so it can be shared between p4c
 *  and gen-shift.c.
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
 *	- ADDGUARDS(x,y) takes two structs internal_bit_* and returns
 *		another one, whose data is equal to x but with the
 *		guard bytes, when present, copied from y.  (If there
 *		are no guard bytes, it can return x and ignore y.)
 */

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
  { bld->appendFormat(" struct internal_bit_%u rv INIT;\n",lw);
    o = sv >> 3;
    sv &= 7;
    if (sv) bld->append(" u16 a;\n");
    bld->newline();
    if (o) bld->appendFormat(" __builtin_memset(&BITS(rv)[%u],0,%u);\n",((lw+7)>>3)-o,o);
    if (sv)
     { pref = "";
       suff = "";
       // This can be optimized more in some cases.
       // "First make it work..."
       if (lw & 7)
	{ i = 0;
	}
       else
	{ i = 1;
	  o ++;
	}
       nb = lw >> 3;
       for (;o<nb;o++,i++)
	{ bld->appendFormat(" a = %sBITS(v)[%u] << %u%s;\n",pref,nb-i,sv,suff);
	  pref = "(a >> 8) | (";
	  suff = ")";
	  bld->appendFormat(" BITS(rv)[%u] = a & 255;\n",nb-o);
	}
       bld->appendFormat(" a = %sBITS(v)[%u] << %u%s;\n",pref,nb-i,sv,suff);
       bld->appendFormat(" BITS(rv)[%u] = a & %u;\n",nb-o,(lw&7)?(1U<<(lw&7))-1U:255);
     }
    else
     { if (lw & 7)
	{ bld->appendFormat(" __builtin_memcpy(&BITS(rv)[1],&BITS(v)[%u],%u);\n",o+1,(lw>>3)-o);
	  bld->appendFormat(" BITS(rv)[0] = BITS(v)[%u] & %u;\n",o,(1U<<(lw&7))-1U);
	}
       else
	{ bld->appendFormat(" __builtin_memcpy(&BITS(rv)[0],&BITS(v)[%u],%u);\n",o,(lw>>3)-o);
	}
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
 int nb;

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
  { bld->appendFormat(" struct internal_bit_%u rv INIT;\n",lw);
    if (sv >= (lw & ~7U))
     { // No bits survive from anything below the top byte of v
       bld->newline();
       bld->appendFormat(" __builtin_memset(&BITS(rv)[0],0,%u);\n",(lw-1)>>3);
       if (sv == (lw & ~7U))
	{ bld->appendFormat(" BITS(rv)[%u] = BITS(v)[0]",(lw-1)>>3);
	  if (lw & 7) bld->appendFormat(" & %u",(1U<<(lw&7))-1);
	  bld->append(";\n");
	}
       else if (lw & 7)
	{ bld->appendFormat(" BITS(rv)[%u] = (BITS(v)[0] & %u) >> %u;\n",(lw-1)>>3,(1U<<(lw&7))-1U,sv-(lw&~7U));
	}
       else
	{ bld->appendFormat(" BITS(rv)[%u] = BITS(v)[0] >> %u;\n",(lw-1)>>3,sv-(lw&~7U));
	}
     }
    else if (! (sv & 7))
     { bld->newline();
       if (lw & 7)
	{ bld->appendFormat(" __builtin_memcpy(&BITS(rv)[%u],&BITS(v)[1],%u);\n",(sv>>3)+1,(lw-sv)>>3);
	  bld->appendFormat(" BITS(rv)[%u] = BITS(v)[0] & %u;\n",sv>>3,(1U<<(lw&7))-1U);
	  bld->appendFormat(" __builtin_memset(&BITS(rv)[0],0,%u);\n",sv>>3);
	}
       else
	{ bld->appendFormat(" __builtin_memcpy(&BITS(rv)[%u],&BITS(v)[0],%u);\n",sv>>3,(lw-sv)>>3);
	  bld->appendFormat(" __builtin_memset(&BITS(rv)[0],0,%u);\n",sv>>3);
	}
     }
    else
     { bld->append(" u32 a;\n");
       bld->newline();
       bld->append(" a = BITS(v)[0]");
       if (lw & 7) bld->appendFormat(" & %u",(1U<<(lw&7))-1);
       bld->append(";\n");
       s = 8 + ((lw - 1) & 7) - ((lw - 1 - sv) & 7);
       j = (lw - 9) >> 3;
       i = (lw - 1 - sv) >> 3;
       nb = (lw - 1) >> 3;
       if (i < nb) bld->appendFormat(" __builtin_memset(&BITS(rv)[0],0,%u);\n",nb-i);
       for (;i>0;i--,j--)
	{ bld->appendFormat(" a = (a << 8) | BITS(v)[%u];\n",nb-j);
	  bld->appendFormat(" BITS(rv)[%u] = (a >> %u) & 255;\n",nb-i,s);
	}
       if (s == 8)
	{ bld->appendFormat(" BITS(rv)[%u] = a & 255;\n",nb-i);
	}
       else if (s > 8)
	{ bld->appendFormat(" BITS(rv)[%u] = (a >> %u) & 255;\n",nb-i,s-8);
	}
       else
	{ bld->appendFormat(" a = (a << 8) | BITS(v)[%u];\n",nb-j);
	  bld->appendFormat(" BITS(rv)[%u] = (a >> %u) & 255;\n",nb-i,s);
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
  { bld->appendFormat(" struct internal_bit_%u rv INIT;\n",lw);
    if (sv >= (lw & ~7U))
     { // No bits survive from anything below the top byte of v
       bld->newline();
       if (sv == lw-1)
	{ // All bits copies of sign bit
	  if (lw & 7)
	   { bld->appendFormat(" __builtin_memset(&BITS(rv)[1],(BITS(v)[0]&%u)?255:0,%u);\n",1U<<((lw-1)&7),lw>>3);
	     bld->appendFormat(" BITS(rv)[0] = (BITS(v)[0] & %u) ? %u : 0;\n",1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	   }
	  else
	   { bld->appendFormat(" __builtin_memset(&BITS(rv)[0],(BITS(v)[0]&%u)?255:0,%u);\n",1U<<((lw-1)&7),lw>>3);
	   }
	}
       else
	{ if (lw & 7)
	   { bld->appendFormat(" if (BITS(v)[0] & %u)\n",1U<<((lw-1)&7));
	     bld->appendFormat("  { __builtin_memset(&BITS(rv)[1],255,%u);\n"/*}*/,(lw>>3)-1);
	     bld->appendFormat("    BITS(rv)[0] = %u;\n",(1U<<(lw&7))-1U);
	     bld->append(/*{*/"  }\n");
	     bld->append(" else\n");
	     bld->appendFormat("  { __builtin_memset(&BITS(rv)[0],0,%u);\n"/*}*/,lw>>3);
	     bld->append(/*{*/"  }\n");
	   }
	  else
	   { bld->appendFormat(" __builtin_memset(&BITS(rv)[0],(BITS(v)[0]&128)?255:0,%u);\n",(lw-1)>>3);
	   }
	  bld->appendFormat(" BITS(rv)[%u] = (",(lw-1)>>3);
	  if (sv == (lw & ~7U))
	   { bld->appendFormat("BITS(v)[0]");
	     if (lw & 7) bld->appendFormat(" & %u",(1U<<(lw&7))-1);
	   }
	  else if (lw & 7)
	   { bld->appendFormat("(BITS(v)[0] & %u) >> %u",(1U<<(lw&7))-1U,sv-(lw&~7U));
	   }
	  else
	   { bld->appendFormat("BITS(v)[0] >> %u",sv-(lw&~7U));
	   }
	  bld->appendFormat(") | ((BITS(v)[0] & %u) ? %u : 0);\n",1U<<((lw-1)&7),(255U<<((lw-(sv&7))&7))&255U);
	}
     }
    else if (! (sv & 7))
     { bld->newline();
       if (lw & 7)
	{ bld->appendFormat(" __builtin_memcpy(&BITS(rv)[%u],&BITS(v)[0],%u);\n",sv>>3,(lw+8-sv)>>3);
	  bld->appendFormat(" BITS(rv)[%u] |= (BITS(v)[0] & %u) ? %u : 0;\n",(lw>>3)-((lw-sv)>>3),1U<<((lw-1)&7),(255U<<(lw&7))&255U);
	  if ((sv >> 3) > 1) bld->appendFormat(" __builtin_memset(&BITS(rv)[1],(BITS(v)[0]&%u)?255:0,%u);\n",1U<<((lw-1)&7),(sv>>3)-1);
	  bld->appendFormat(" BITS(rv)[0] = (BITS(v)[0] & %u) ? %u : 0;\n",1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	}
       else
	{ bld->appendFormat(" __builtin_memcpy(&BITS(rv)[%u],&BITS(v)[0],%u);\n",sv>>3,(lw-sv)>>3);
	  bld->appendFormat(" __builtin_memset(&BITS(rv)[0],(BITS(v)[0]&128)?255:0,%u);\n",sv>>3);
	}
     }
    else
     { bld->append(" u32 a;\n");
       bld->newline();
       switch (lw & 7)
	{ case 0:
	     bld->append(" a = BITS(v)[0] | ((BITS(v)[0] & 128) ? ~(u32)255 : 0);\n");
	     break;
	  case 1:
	     bld->append(" a = (BITS(v)[0] & 1) ? ~(u32)0 : 0;\n");
	     break;
	  default:
	     bld->appendFormat(" a = (BITS(v)[0] & %u) | ((BITS(v)[0] & %u) ? ~(u32)%u : 0);\n",(1U<<(lw&7))-1,1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	     break;
	}
       s = 8 + ((lw - 1) & 7) - ((lw - 1 - sv) & 7);
       j = (lw - 9) >> 3;
       i = (lw - 1 - sv) >> 3;
       m = (lw & 7) ? (1U << (lw & 7)) - 1U : 255;
       if (i < (int)((lw-1) >> 3))
	{ if (lw & 7)
	   { if (((lw-1) >> 3) - i > 1) bld->appendFormat(" __builtin_memset(&BITS(rv)[1],(BITS(v)[0]&%u)?255:0,%u);\n",1U<<((lw-1)&7),((lw-1)>>3)-i-1);
	     bld->appendFormat(" BITS(rv)[0] = (BITS(v)[0] & %u) ? %u : 0;\n",1U<<((lw-1)&7),(1U<<(lw&7))-1U);
	     m = 255;
	   }
	  else
	   { bld->appendFormat(" __builtin_memset(&BITS(rv)[0],(BITS(v)[0]&%u)?255:0,%u);\n",1U<<((lw-1)&7),((lw-1)>>3)-i);
	   }
	}
       for (;i>0;i--,j--)
	{ bld->appendFormat(" a = (a << 8) | BITS(v)[%u];\n",((lw-1)>>3)-j);
	  bld->appendFormat(" BITS(rv)[%u] = (a >> %u) & %u;\n",((lw-1)>>3)-i,s,m);
	  m = 255;
	}
       if (s == 8)
	{ bld->appendFormat(" BITS(rv)[%u] = a & %u;\n",((lw-1)>>3)-i,m);
	}
       else if (s > 8)
	{ bld->appendFormat(" BITS(rv)[%u] = (a >> %u) & %u;\n",((lw-1)>>3)-i,s-8,m);
	}
       else
	{ bld->appendFormat(" a = (a << 8) | BITS(v)[%u];\n",((lw-1)>>3)-j);
	  bld->appendFormat(" BITS(rv)[%u] = (a >> %u) & %u;\n",((lw-1)>>3)-i,s,m);
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
     { bld->appendFormat("%s%s = BITS(%s)[%d] & %d;\n",indent,shint,sharg,rw>>3,(1<<(rw&7))-1);
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
       bld->appendFormat("BITS(%s)[%d]%s;\n",sharg,i,s_set?")":"");
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
 if (lw > 64) bld->appendFormat(" struct internal_bit_%u rv INIT;\n",lw);
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
     { bld->appendFormat("    for (o=%u,left=%u;%s>=8;o--,%s-=8,left-=8) BITS(rv)[o] = 0;\n",(lw-1)>>3,lw,shvar,shvar);
       bld->append      ("    a = 0;\n");
       bld->appendFormat("    for (i=%u;left>=8;left-=8)\n",(lw-1)>>3);
     }
    else
     { bld->appendFormat("    a = 0;\n");
       bld->appendFormat("    for (i=o=%u,left=%u;left>=8;left-=8)\n",(lw-1)>>3,lw);
     }
    bld->appendFormat("     { a = (a >> 8) | (BITS(v)[i--] << %s);\n"/*}*/,shvar);
    bld->append      ("       BITS(rv)[o--] = a & 255;\n");
    bld->append (/*{*/"     }\n");
    bld->append      ("    if (left)\n");
    bld->appendFormat("     { if (left > %s) a = (a >> 8) | (BITS(v)[i--] << %s); else a >>= 8;\n"/*}*/,shvar,shvar);
    bld->append      ("       BITS(rv)[o--] = a & ((1U << left) - 1);\n");
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
 if (lw > 64) bld->appendFormat(" struct internal_bit_%u rv INIT;\n",lw);
 bld->append(" u16 a;\n");
 bld->append(" int i;\n");
 bld->append(" int o;\n");
 bld->newline();
 gen_sh_x_shvar(bld,lw,rw,"sh","s",&shvar);
 // the value in the variable named by shvar is < lw, now
 // (but we can't assert() that; it's a packet-processing-time thing)
 if (lw <= 64)
  { bld->appendFormat("    return(v>>%s);\n",shvar);
  }
 else
  { bld->appendFormat("    i = %u - (%s >> 3);\n",(lw-1)>>3,shvar);
    bld->appendFormat("    %s = 8 - (%s & 7ULL);\n",shvar,shvar);
    bld->appendFormat("    a = BITS(v)[i--] << %s;\n",shvar);
    bld->appendFormat("    o = %u;\n",(lw-1)>>3);
    bld->append      ("    for (;i>=0;i--)\n");
    bld->appendFormat("     { a = (a >> 8) | (BITS(v)[i] << %s);\n"/*}*/,shvar);
    bld->appendFormat("       BITS(rv)[o--] = a & 255;\n");
    bld->append      (/*{*/"     }\n");
    if (lw & 7) bld->appendFormat("    a &= %u << %s;\n",(1U<<(lw&7))-1U,shvar);
    bld->append      ("    BITS(rv)[o--] = a >> 8;\n");
    bld->append      ("    for (;o>=0;o--) BITS(rv)[o] = 0;\n");
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
 if (lw > 64) bld->appendFormat(" struct internal_bit_%u rv INIT;\n",lw);
 bld->append(" u16 a;\n");
 bld->append(" int i;\n");
 bld->append(" int o;\n");
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
    bld->appendFormat("    sign = (BITS(v)[0] & %u) ? 255 : 0;\n",1U<<((lw-1)&7));
    bld->appendFormat("    i = %s >> 3;\n",shvar);
    bld->appendFormat("    %s = 8 - (%s & 7ULL);\n",shvar,shvar);
    if (lw & 7)
     { bld->appendFormat("    a = ((i == %u) ? (BITS(v)[0] & %u) | ((sign << %u) & 255) : BITS(v)[%u-i]) << %s;\n",
		lw>>3, (1U<<(lw&7))-1U, lw&7, lw>>3, shvar);
       bld->append      ("    i ++;\n");
       bld->appendFormat("    o = %u;\n",lw>>3);
       bld->appendFormat("    for (i=%u-i;i>0;i--)\n",lw>>3);
       bld->appendFormat("     { a = (a >> 8) | (BITS(v)[i] << %s);\n"/*}*/,shvar);
       bld->appendFormat("       BITS(rv)[o--] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->append      ("    if (i == 0)\n");
       bld->appendFormat("     { a = (a >> 8) | ((BITS(v)[0] & %u) << %s) | (sign << (%s + %u));\n"/*}*/,
		(1U<<(lw&7))-1U, shvar, shvar, lw&7);
       bld->append      ("       BITS(rv)[o--] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->appendFormat("    a |= sign << (8 + %s);\n",shvar);
       bld->append      ("    if (o == 0)\n");
       bld->appendFormat("     { BITS(rv)[o--] = (a >> 8) & %u;\n"/*}*/,(1U<<(lw&7))-1U);
       bld->append      (/*{*/"     }\n");
       bld->append      ("    else\n");
       bld->append      ("     { BITS(rv)[o--] = (a >> 8) & 255;\n"/*}*/);
       bld->appendFormat("       for (;o>0;o--) BITS(rv)[o] = sign;\n");
       bld->appendFormat("       BITS(rv)[0] = sign & %u;\n",(1U<<(lw&7))-1U);
       bld->append      (/*{*/"     }\n");
     }
    else
     { bld->appendFormat("    i = %u - i;\n",(lw-1)>>3);
       bld->appendFormat("    a = BITS(v)[i--] << %s;\n",shvar);
       bld->appendFormat("    o = %u;\n",(lw-1)>>3);
       bld->append      ("    for (;i>=0;i--)\n");
       bld->appendFormat("     { a = (a >> 8) | (BITS(v)[i] << %s);\n"/*}*/,shvar);
       bld->appendFormat("       BITS(rv)[o--] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->append      ("    if (o >= 0)\n");
       bld->appendFormat("     { a = (a >> 8) | (sign << %s);\n"/*}*/,shvar);
       bld->appendFormat("       BITS(rv)[o--] = a & 255;\n");
       bld->append      (/*{*/"     }\n");
       bld->append      ("    for (;o>=0;o--) BITS(rv)[o] = sign;\n");
     }
    bld->append      ("    return(rv);\n");
  }
 bld->append(/*{*/"  } while (0);\n");
 bld->append(" return(");
 if (lw <= 64)
  { bld->appendFormat("(v&%lluULL)?%lluULL:0",1ULL<<(lw-1),((1ULL<<(lw-1))<<1)|1ULL);
  }
 else
  { bld->appendFormat("ADDGUARDS(((BITS(v)[0]&%u)?(struct internal_bit_%u){{"/*}}*/,1U<<((lw-1)&7),lw);
    bld->appendFormat("%u",(1U<<(((lw-1)&7)+1))-1);
    for (i=(lw-1)>>3;i>0;i--) bld->append(",255");
    bld->appendFormat(/*{{*/"}}:(struct internal_bit_%u){{0}}",lw);
    bld->append("),v)");
  }
 bld->append(");\n");
 bld->append(/*{*/"}\n");
}
