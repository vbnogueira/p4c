/*
 * This code is here in a separate file so it can be shared between p4c
 *  and test-cast.c.
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
 *	- GUARDARGS expands to declarations of any trailing arguments
 *		needed by SETGUARDS (below).
 *
 *	- SETGUARDS(x) generates code to set random guard bytes on x,
 *		copying the guard bytes also to the arguments declared
 *		by GUARDARGS.  If there are no guard bytes, this
 *		typically will be an empty do-while.
 *
 *	- INIT must expand to something which can be appended to the
 *		return-value variable's declaration: either nothing or
 *		an equal sign and an initializer.  This will normally
 *		use the GUARDARGS arguments, if present.
 */

static void gen_cast(BUILDER *bld, const WIDTH_REC *wr)
{
 unsigned int fw;
 unsigned int tw;
 int fb1;
 int tb1;
 int fb2;
 int tb2;
 int minb1;

 assert(wr->type == WR_CAST);
 fw = wr->cast.fw;
 tw = wr->cast.tw;
 fb1 = fw >> 3;
 tb1 = tw >> 3;
 minb1 = (fb1 < tb1) ? fb1 : tb1;
 fb2 = (fw + 7) >> 3;
 tb2 = (tw + 7) >> 3;
 assert(((fw>64)||(tw>64))&&(fw!=tw));
 bld->newline();
 bld->append("static __always_inline ");
 append_type_for_width(bld,tw);
 bld->appendFormat(" cast_%u_to_%u(",fw,tw);
 append_type_for_width(bld,fw);
 bld->append(" arg GUARDARGS)\n");
 bld->append("{\n"/*}*/);
 if (tw > 64)
  { bld->appendFormat(" struct internal_bit_%d ret INIT;\n",tw);
    bld->append("\n");
    bld->append(" SETGUARDS(ret);\n");
    if (fw > 64)
     { // Copy as many full bytes as both widths contain
       bld->appendFormat(" __builtin_memcpy(&BITS(ret)[%d],&BITS(arg)[%d],%d);\n",tb2-minb1,fb2-minb1,minb1);
       if (tw > fw)
	{ // If we are widening...
	  // Handle partial source byte, if present.
	  if (fb2 != fb1) bld->appendFormat(" BITS(ret)[%d] = BITS(arg)[0] & %u;\n",tb2-fb2,~((~0U)<<(fw&7)));
	  // Zero any further destination bytes.
	  if (tb2 > fb2) bld->appendFormat(" __builtin_memset(&BITS(ret)[0],0,%d);\n",tb2-fb2);
	}
       else
	{ // If we are narrowing...
	  // Copy trailing partial byte, if present.
	  if (tb2 != tb1) bld->appendFormat(" BITS(ret)[0] = BITS(arg)[%d] & %d;\n",fb2-tb2,~((~0U)<<(tw&7)));
	}
     }
    else
     { int i;
       int j;
       for (i=0,j=tb2-1;i<fb1;i++,j--)
	{ bld->appendFormat(" BITS(ret)[%d] = ",j);
	  if (i) bld->appendFormat("(arg >> %d)",i*8); else bld->appendFormat("arg");
	  bld->appendFormat(" & 255;\n");
	}
       if (i < fb2)
	{ bld->appendFormat(" BITS(ret)[%d] = ",j);
	  if (i) bld->appendFormat("(arg >> %d)",i*8); else bld->appendFormat("arg");
	  bld->appendFormat(" & %u;\n",(1U<<(fw&7))-1);
	  j --;
	}
       if (j >= 0) bld->appendFormat(" __builtin_memset(&BITS(ret)[0],0,%u);\n",j+1);
     }
  }
 else
  { bld->appendFormat(" u64 ret INIT;\n");
    bld->append("\n");
    bld->append(" SETGUARDS(ret);\n");
    // know fw>64 from assert above, since tw<=64 here
    const char *pref = " ret = ";
    if (tw & 7)
     { bld->appendFormat("%s((BITS(arg)[%d] & (u64)%u) << %d)",pref,fb2-1-(tw>>3),(1U<<(tw&7))-1,tw&~7U);
       pref = " |\n       ";
     }
    for (int i=(tw>>3)-1;i>=0;i--)
     { bld->appendFormat("%s(((u64)BITS(arg)[%d]) << %d)",pref,fb2-1-i,i<<3);
       pref = " |\n       ";
     }
    bld->append(";\n");
  }
 bld->append(" return(ret);\n");
 bld->append(/*{*/"}\n");
}
