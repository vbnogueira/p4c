/* This file is in the public domain. */

/*
 * This program is an egregious hack.
 *
 * The right thing to do, of course, is to have support for this stuff
 *  in the compiler.  But under fairly weak assumptions, we can convert
 *  labeled structure to vanilla C with relatively mild textual hacks.
 *
 * The only assumption we really need is that the program is syntactic.
 *  There are some things which it cannot handle which a real
 *  in-compiler implementation can - notably cases where a
 *  control-structure statement's body is not a brace-bracketed
 *  compound statement - but it detects them and errors out.
 *
 * This program assumes - mostly in its handling of # lines - that it's
 *  dealing with gcc (reading gcc output, writing input for gcc).
 *  Since the programs for which it's necessary generally use other
 *  gccisms (notably nested functions), this seems relatively safe.
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

extern const char *__progname;

/* This happens to be the largest prime less than 2^32, but it can be
   pretty much any prime in that general vicinity.  Even composite numbers
   could be accommodated with a bit of effort in setup_tmp to ensure that
   a `large' subgroup is generated. */
#define PRIME 4294967291U
/* NLETTERS is the smallest N such that 26^N >= PRIME;
   26^7 is 8031810176, about twice PRIME. */
#define NLETTERS 7

typedef struct filestr FILESTR;
typedef struct linestr LINESTR;
typedef struct token TOKEN;
typedef struct ctlstruct CTLSTRUCT;
typedef struct caselabel CASELABEL;

struct caselabel {
  CASELABEL *link;
  int ntokens;
  TOKEN **tokens;
  int label;
  } ;

struct ctlstruct {
  CTLSTRUCT *link;
  unsigned int type;
#define CST_OTHER  0x00
#define CST_DO     0x01
#define CST_WHILE  0x02
#define CST_FOR    0x04
#define CST_SWITCH 0x08
  unsigned int state;
#define CSS_UNSET 1
#define CSS_PAREN 2
#define CSS_BODY  3
  int break_label;
  int cont_label;
  CASELABEL *cases;
  char *tag;
  int bracelevel;
  int parenlevel;
  } ;

struct filestr {
  char *file;
  int refcnt;
  unsigned int flags;
  } ;

struct linestr {
  FILESTR *file;
  int line;
  } ;

struct token {
  char *text;
  int len;
  LINESTR loc;
  } ;

#define BUF_SIZE 8192
#define MAX_PUSH 8

static const char *infn;
static char *tmpfn;
static unsigned char in_buf[BUF_SIZE];
static int in_ptr;
static int in_fill;
static int in_pushbuf[MAX_PUSH];
static int in_push;
static unsigned char out_buf[BUF_SIZE];
static int out_fill;
static int in_fd;
static int tmp_fd;
static int in_atnl;
static TOKEN *tok_pushbuf[MAX_PUSH];
static int tok_push;
static LINESTR in_loc;
static LINESTR out_loc;
static CTLSTRUCT *csstack;
static int bracelevel;
static int parenlevel;
static int next_label_num;
static int semi_label;

#define UCLETTERS							\
	    'A': case 'B': case 'C': case 'D': case 'E': case 'F':	\
       case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':	\
       case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':	\
       case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':	\
       case 'Y': case 'Z'
#define LCLETTERS							\
	    'a': case 'b': case 'c': case 'd': case 'e': case 'f':	\
       case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':	\
       case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':	\
       case 's': case 't': case 'u': case 'v': case 'w': case 'x':	\
       case 'y': case 'z'
#define LETTERS UCLETTERS: case LCLETTERS
#define DIGITS								\
	    '0': case '1': case '2': case '3': case '4':		\
       case '5': case '6': case '7': case '8': case '9'
#define SYMFIRST LETTERS: case '_': case '$'
#define SYMCHAR  LETTERS: case '_': case '$': case DIGITS

#define TWOCHAR(a,b) (((a)*256)+(b))

/*
 * This routine computes "random" file names until it finds a
 *  nonexistent one for use as a temp file.  This is done by getting
 *  the current time, shoving it through a 32-bit CRC to spread it out
 *  across the available space, and then raising 2 to that power modulo
 *  PRIME.  (Actually, 2^m where m is the bit-reversal of the CRC, but
 *  for our purposes the bit-reversal is just as good.)  This is then
 *  rendered in base 26.  If the resulting file exists, the next power
 *  of 2 (modulo PRIME) is tried.  Since PRIME is prime, 2 generates
 *  the entire subgroup of Z modulo PRIME.
 *
 * Unpredictability in the cryptographic sense does not matter here,
 *  only reducing the chance of collisions with other processes doing
 *  such things - this is why the current time is a good enough seed.
 *
 * We don't do the usual table-lookup CRC implementation because this
 *  is done only once and it does not have to be particularly fast, and
 *  the data being fed through it is quite small (8 bytes, on most
 *  ports), so there's no point in wasting the memory and time to
 *  compute a 256-entry table of which we will use (usually) at most 8
 *  entries.
 */
static void setup_tmp(void)
{
 unsigned int n;
 unsigned long long int v;
 struct timeval now;
 int i;
 int j;
 int o;

 gettimeofday(&now,0);
 n = 0x12345678;
 for (i=0;i<sizeof(now);i++)
  { n ^= ((unsigned char *)&now)[i];
    for (j=0;j<8;j++) n = (n >> 1) ^ ((n&1) ? 0xedb88320 : 0);
  }
 v = 1;
 for (i=0;i<32;i++)
  { v = (v * v) % PRIME;
    if (n & 1)
     { v *= 2;
       if (v >= PRIME) v -= PRIME;
     }
    n >>= 1;
  }
 o = strlen(infn);
 tmpfn = malloc(o+1+NLETTERS+1);
 bcopy(infn,tmpfn,o);
 tmpfn[o++] = '.';
 tmpfn[o+NLETTERS] = '\0';
 while (1)
  { n = v;
    for (i=0;i<NLETTERS;i++)
     { tmpfn[o+i] = "abcdefghijklmnopqrstuvwxyz"[n%26];
       n /= 26;
     }
    if (n) abort(); /* indicates NLETTERS is too small for PRIME */
    tmp_fd = open(tmpfn,O_RDWR|O_CREAT|O_EXCL,0600);
    if (tmp_fd >= 0) break;
    switch (errno)
     { case EEXIST:
	  break;
       default:
	  fprintf(stderr,"%s: temp file %s: %s\n",__progname,tmpfn,strerror(errno));
	  exit(1);
	  break;
     }
    v *= 2;
    if (v >= PRIME) v -= PRIME;
  }
 unlink(tmpfn);
}

static char *quotify(const char *s)
{
 int l;
 int i;
 int j;
 char *rv;

 l = 2;
 for (i=0;s[i];i++)
  { l ++;
    switch (s[i])
     { case '\\': case '\"': l ++; break;
     }
  }
 rv = malloc(l+1);
 j = 0;
 rv[j++] = '"';
 for (i=0;s[i];i++)
  { switch (s[i])
     { case '\\': case '\"': rv[j++] = '\\'; break;
     }
    rv[j++] = s[i];
  }
 rv[j++] = '"';
 if (j != l) abort();
 rv[j] = '\0';
 return(rv);
}

static FILESTR *new_filestr(const char *s)
{
 FILESTR *fs;
 char *q;

 fs = malloc(sizeof(FILESTR)+strlen(s)+1);
 fs->file = (void *) (fs+1);
 strcpy(fs->file,s);
 fs->flags = 0;
 q = rindex(fs->file,'"');
 if (q && q[1])
  { *++q = '\0';
    for (++q;*q;q++)
     { switch (*q)
	{ case '1': fs->flags |= 1; break;
	  case '2': fs->flags |= 2; break;
	  case '3': fs->flags |= 4; break;
	  case '4': fs->flags |= 8; break;
	  case ' ': break;
	  default:
	     fprintf(stderr,"%s: warning: unrecognized flag %c ignored\n",__progname,*q);
	     break;
	}
     }
  }
 fs->refcnt = 1;
 return(fs);
}

static void incref(FILESTR *fs)
{
 fs->refcnt ++;
}

static void decref(FILESTR *fs)
{
 fs->refcnt --;
 if (fs->refcnt > 0) return;
 if (fs->refcnt < 0) abort();
 free(fs);
}

static int get_in(void)
{
 if (in_push > 0) return(in_pushbuf[--in_push]);
 if (in_ptr < 0) return(-1);
 if (in_ptr >= in_fill)
  { in_ptr = 0;
    in_fill = read(in_fd,&in_buf[0],BUF_SIZE);
    if (in_fill < 0)
     { fprintf(stderr,"%s: read error on %s: %s\n",__progname,infn,strerror(errno));
       exit(1);
     }
    if (in_fill == 0)
     { in_ptr = -1;
       return(-1);
     }
  }
 return(in_buf[in_ptr++]);
}

static void push_in(int v)
{
 if (in_push >= MAX_PUSH) abort();
 in_pushbuf[in_push++] = v;
}

static TOKEN *newtoken(char *text, int len, LINESTR loc)
{
 TOKEN *t;

 t = malloc(sizeof(TOKEN));
 t->text = text;
 t->len = len;
 t->loc = loc;
 incref(loc.file);
 return(t);
}

static int digitval(int c)
{
 switch (c)
  { case '0': return(0); break;
    case '1': return(1); break;
    case '2': return(2); break;
    case '3': return(3); break;
    case '4': return(4); break;
    case '5': return(5); break;
    case '6': return(6); break;
    case '7': return(7); break;
    case '8': return(8); break;
    case '9': return(9); break;
  }
 abort();
}

static void out_write(const void *data, int len)
{
 int w;
 int left;
 const char *dp;

 dp = data;
 left = len;
 while (left > 0)
  { w = write(tmp_fd,dp,left);
    if (w < 0)
     { fprintf(stderr,"%s: write to %s: %s\n",__progname,tmpfn,strerror(errno));
       exit(1);
     }
    if (w == 0)
     { fprintf(stderr,"%s: write to %s: zero write\n",__progname,tmpfn);
       exit(1);
     }
    dp += w;
    left -= w;
  }
}

static void out_flush(void)
{
 out_write(&out_buf[0],out_fill);
 out_fill = 0;
}

static void out_str(const char *s, int l)
{
 if (l < 0) l = strlen(s);
 if (out_fill+l > BUF_SIZE)
  { out_flush();
    if (l >= BUF_SIZE)
     { out_write(s,l);
     }
    else
     { bcopy(s,&out_buf[0],l);
       out_fill = l;
     }
  }
 else
  { bcopy(s,&out_buf[out_fill],l);
    out_fill += l;
  }
}

static void out_num(int v)
{
 char buf[32];

 out_str(&buf[0],sprintf(&buf[0],"%d",v));
}

static void gen_sharp_line(LINESTR *loc)
{
 int i;

 out_str("\n# ",3);
 out_num(loc->line);
 out_str(loc->file->file,-1);
 for (i=0;i<4;i++)
  { if (loc->file->flags & (1 << i))
     { out_str(" ",1);
       out_num(i+1);
     }
  }
 out_str("\n",1);
}

static void switch_loc(LINESTR *ls)
{
 if (ls->file != out_loc.file)
  { incref(ls->file);
    decref(out_loc.file);
    out_loc = *ls;
    gen_sharp_line(&out_loc);
  }
 else if (ls->line == out_loc.line)
  { out_str(" ",1);
  }
 else if ((ls->line > out_loc.line) && (ls->line < out_loc.line+5))
  { while (out_loc.line < ls->line)
     { out_str("\n",1);
       out_loc.line ++;
     }
  }
 else
  { out_loc.line = ls->line;
    gen_sharp_line(&out_loc);
  }
}

static void copy_rest_of_line(void)
{
 int c;
 char cc;

 while (1)
  { c = get_in();
    if (c < 0) break;
    cc = c;
    out_str(&cc,1);
    if (c == '\n') break;
  }
}

static void lineno_line(void)
{
 int c;
 int l;
 static char *b = 0;
 static int a = 0;
 int n;

 do c = get_in(); while (c == ' ');
 switch (c)
  { case DIGITS:
       l = 0;
       while (1)
	{ switch (c)
	   { case DIGITS:
		l = (l * 10) + digitval(c);
		break;
	     default:
		goto _break_while;
	   }
	  c = get_in();
	}
_break_while:;
       n = 0;
       while (1)
	{ if (n >= a) b = realloc(b,a=n+8);
	  if ((c == '\n') || (c < 0)) break;
	  b[n++] = c;
	  c = get_in();
	}
       b[n] = '\0';
       decref(in_loc.file);
       in_loc.file = new_filestr(b);
       in_loc.line = l;
       switch_loc(&in_loc);
       in_loc.file->flags &= ~3;
       break;
    default:
       push_in(c);
       out_str("\n#",2);
       copy_rest_of_line();
       break;
  }
}

static TOKEN *get_token(void)
{
 int c;
 int d;
 char *buf;
 int nbuf;
 int abuf;

 void tokc(int c)
  { if (nbuf >= abuf) buf = realloc(buf,abuf=nbuf+8);
    buf[nbuf++] = c;
  }

 void endbuf(void)
  { tokc('\0');
    nbuf --;
  }

 void skip_string(int delim)
  { int c;
    tokc(delim);
    while (1)
     { c = get_in();
       if (c == delim)
	{ tokc(c);
	  return;
	}
       if (c == '\\')
	{ tokc('\\');
	  c = get_in();
	}
       if (c >= 0) tokc(c); else return;
     }
  }

 TOKEN *done(void)
  { endbuf();
    return(newtoken(buf,nbuf,in_loc));
  }

 void skip_number(int sawdot)
  { int c;
    int inexp;
    int base;
    int insuf;
    inexp = 0;
    insuf = 0;
    base = 10;
    while (1)
     { c = get_in();
       switch (c)
	{ case '.':
	     if (sawdot || inexp || insuf || (base != 10))
	      { push_in(c);
		return;
	      }
	     sawdot = 1;
	     break;
	  case DIGITS:
	     if (insuf)
	      { push_in(c);
		return;
	      }
	     break;
	  case 'a': case 'A':
	  case 'b': case 'B':
	  case 'c': case 'C':
	  case 'd': case 'D':
	     if (insuf || (base != 16))
	      { push_in(c);
		return;
	      }
	     break;
	  case 'f': case 'F':
	     if (base == 10)
	      { insuf = 1;
		break;
	      }
	     if (base != 16)
	      { push_in(c);
		return;
	      }
	     break;
	  case 'e': case 'E':
	     if (insuf)
	      { push_in(c);
		return;
	      }
	     if (base == 16) break;
	     if (inexp)
	      { push_in(c);
		return;
	      }
	     inexp = 1;
	     break;
	  case 'u': case 'U':
	  case 'l': case 'L':
	     if (sawdot || inexp)
	      { push_in(c);
		return;
	      }
	     insuf = 1;
	     break;
	  case 'x': case 'X':
	     if ((nbuf != 1) || (buf[0] != '0'))
	      { push_in(c);
		return;
	      }
	     base = 16;
	     break;
	  case '-': case '+':
	     if (inexp != 1)
	      { push_in(c);
		return;
	      }
	     inexp = 2;
	     break;
	  default:
	     push_in(c);
	     return;
	}
       tokc(c);
     }
  }

 if (tok_push > 0) return(tok_pushbuf[--tok_push]);
 buf = 0;
 nbuf = 0;
 abuf = 0;
 while (1)
  { c = get_in();
    if (c < 0) return(newtoken(0,-1,in_loc));
    if (in_atnl && (c == '#'))
     { lineno_line();
       continue;
     }
    switch (c)
     { case '\n':
	  in_loc.line ++;
	  in_atnl = 1;
	  break;
       case ' ': case '\t': case '\v': case '\f':
	  break;
	  break;
       case '\'': case '"':
	  skip_string(c);
	  return(done());
	  break;
       case SYMFIRST:
	  tokc(c);
	  d = get_in();
	  if (c == 'L')
	   { switch (d)
	      { case '\'': case '"':
		   skip_string(d);
		   return(done());
		   break;
	      }
	   }
	  push_in(d);
	  while (1)
	   { c = get_in();
	     switch (c)
	      { case SYMCHAR:
		   tokc(c);
		   break;
		default:
		   push_in(c);
		   return(done());
		   break;
	      }
	   }
	  break;
       case DIGITS:
	  tokc(c);
	  skip_number(0);
	  return(done());
	  break;
       case '!': case '%': case '*':
       case '/': case '=': case '^':
	  tokc(c);
	  c = get_in();
	  if (c == '=') tokc('='); else push_in(c);
	  return(done());
	  break;
       case '&': case '+': case '|':
	  tokc(c);
	  d = get_in();
	  if ((d == c) || (d == '=')) tokc(d); else push_in(d);
	  return(done());
	  break;
       case '-':
	  tokc('-');
	  c = get_in();
	  switch (c)
	   { case '-': case '=': case '>':
		tokc(c);
		break;
	     default:
		push_in(c);
		break;
	   }
	  return(done());
	  break;
       case '.':
	  tokc('.');
	  c = get_in();
	  switch (c)
	   { case DIGITS:
		tokc(c);
		skip_number(1);
		break;
	     case '.':
		c = get_in();
		switch (c)
		 { case '.':
		      tokc('.');
		      tokc('.');
		      return(done());
		      break;
		 }
		push_in(c);
		push_in('.');
		break;
	     default:
		push_in(c);
		break;
	   }
	  return(done());
	  break;
       case '<': case '>':
	  tokc(c);
	  d = get_in();
	  if (d == '=')
	   { tokc('=');
	   }
	  else if (d == c)
	   { tokc(c);
	     d = get_in();
	     if (d == '=') tokc('='); else push_in(d);
	   }
	  else
	   { push_in(d);
	   }
	  return(done());
	  break;
       default:
	  tokc(c);
	  return(done());
	  break;
     }
  }
}

static void push_token(TOKEN *t)
{
 if (tok_push >= MAX_PUSH) abort();
 tok_pushbuf[tok_push++] = t;
}

static void free_token(TOKEN *t)
{
 if (! t) return;
 free(t->text);
 decref(t->loc.file);
 free(t);
}

static void put_free_token(TOKEN *t)
{
 if (! t) return;
 switch_loc(&t->loc);
 if (t->text) out_str(t->text,t->len);
 free_token(t);
}

static int nextlabel(void)
{
 return(next_label_num++);
}

static int *labelvar_break(CTLSTRUCT *cs)
{
 return(&cs->break_label);
}

static int *labelvar_cont(CTLSTRUCT *cs)
{
 return(&cs->cont_label);
}

static CTLSTRUCT *find_tag(const char *tag, unsigned int bits)
{
 CTLSTRUCT *cs;

 for (cs=csstack;cs;cs=cs->link)
  { if (cs->tag && (cs->type & bits) && !strcmp(cs->tag,tag)) return(cs);
  }
 return(0);
}

static CTLSTRUCT *push_csstack(void)
{
 CTLSTRUCT *cs;

 cs = malloc(sizeof(CTLSTRUCT));
 cs->link = csstack;
 csstack = cs;
 cs->type = CST_OTHER;
 cs->state = CSS_UNSET;
 cs->break_label = -1;
 cs->cont_label = -1;
 cs->cases = 0;
 cs->tag = 0;
 cs->bracelevel = bracelevel;
 cs->parenlevel = parenlevel;
 return(cs);
}

/*
 * Events calling for action:
 *
 *	{
 *		bracelevel ++
 *	}
 *		bracelevel --
 *		If this brace closes a tagged construct's body:
 *		  Emit any continue label for the construct.
 *		  Emit any break label for the construct.
 *		  Emit any case/default labels for the construct, with
 *		    their gotos.
 *		  Emit the brace.
 *		  If the construct is a do-while, peek ahead and check
 *		    that a while follows; if so, consume and emit it.
 *	(
 *		parenlevel ++
 *	)
 *		parenlevel --
 *		If this closes the parenthesized blob for the
 *		top-of-stack, peek ahead to ensure it's followed by an
 *		open brace.  Mark the CTLSTRUCT as being in body.
 *	for, switch, while
 *		Peek ahead to see if a tag follows; if so:
 *		  Open a new CTLSTRUCT.  Peek ahead to ensure it's
 *		  followed by a left paren.  Mark the CTLSTRUCT as
 *		  expecting a parenthesized blob.
 *	
 *	do
 *		Peek ahead to see if a tag follows; if so:
 *		  Open a new CTLSTRUCT.  Peek ahead to ensure it's
 *		  followed by an open brace.  Mark the CTLSTRUCT as
 *		  being in body.
 */
static void process(void)
{
 TOKEN *t;
 TOKEN *tf[3];
 CTLSTRUCT *cs;
 CASELABEL *cl;
 unsigned int thisbl;
 unsigned int findbits;
 int *(*labelvar)(CTLSTRUCT *);

 in_loc.file = new_filestr(quotify(infn));
 in_loc.line = 1;
 out_loc.file = new_filestr("");
 out_loc.line = 0;
 in_ptr = 0;
 in_fill = 0;
 in_push = 0;
 out_fill = 0;
 in_atnl = 1;
 tok_push = 0;
 bracelevel = 0;
 parenlevel = 0;
 next_label_num = 0;
 semi_label = -1;
 while (1)
  { t = get_token();
    if (! t->text) break;
    if (t->len == 1)
     { switch (t->text[0])
	{ case '{'/*}*/:
	     bracelevel ++;
	     break;
	  case /*{*/'}':
	     bracelevel --;
	     if ( !csstack ||
		  (parenlevel != csstack->parenlevel) ||
		  (bracelevel != csstack->bracelevel) ) break;
	     cs = csstack;
	     csstack = cs->link;
	     if (cs->state != CSS_BODY) abort();
	     if (cs->cont_label >= 0)
	      { out_str(" LAB__",-1);
		out_num(cs->cont_label);
		out_str(":continue;",-1);
	      }
	     if (cs->break_label >= 0)
	      { switch (cs->type)
		 { case CST_DO:
		   case CST_WHILE:
		   case CST_FOR:
		      if (cs->cont_label < 0) out_str(" continue;",-1);
		      break;
		 }
		out_str(" LAB__",-1);
		out_num(cs->break_label);
		out_str(":break;",-1);
	      }
	     if (cs->cases) out_str(" break;",-1);
	     while ((cl=cs->cases))
	      { int i;
		cs->cases = cl->link;
		for (i=0;i<cl->ntokens;i++) put_free_token(cl->tokens[i]);
		free(cl->tokens);
		out_str(" goto LAB__",-1);
		out_num(cl->label);
		out_str(";",1);
		free(cl);
	      }
	     put_free_token(t);
	     if (cs->type == CST_DO)
	      { t = get_token();
		if ((t->len == 5) && !bcmp(t->text,"while",5))
		 { put_free_token(t);
		 }
		else
		 { fprintf(stderr,"%s: warning: do-while body not followed by while\n",__progname);
		   fprintf(stderr,"# %d %s\n",t->loc.line,t->loc.file->file);
		   push_token(t);
		 }
	      }
	     free(cs->tag);
	     free(cs);
	     t = 0;
	     break;
	  case '(':
	     parenlevel ++;
	     break;
	  case ')':
	     parenlevel --;
	     if ( !csstack ||
		  (parenlevel != csstack->parenlevel) ||
		  (bracelevel != csstack->bracelevel) ) break;
	     if (cs->state != CSS_PAREN) abort();
	     put_free_token(t);
	     t = get_token();
	     if ((t->len != 1) || (t->text[0] != '{'/*}*/))
	      { fprintf(stderr,"%s: control structure ( ) not followed by {\n"/*}*/,__progname);
		fprintf(stderr,"# %d %s\n",t->loc.line,t->loc.file->file);
		exit(1);
	      }
	     cs->state = CSS_BODY;
	     push_token(t);
	     t = 0;
	     break;
	}
     }
    if ( t &&
	 ( ( (t->len == 2) &&
	     !bcmp(t->text,"do",2) &&
	     ((thisbl=CST_DO),1) ) ||
	   ( (t->len == 3) &&
	     !bcmp(t->text,"for",3) &&
	     ((thisbl=CST_FOR),1) ) ||
	   ( (t->len == 5) &&
	     !bcmp(t->text,"while",5) &&
	     ((thisbl=CST_WHILE),1) ) ||
	   ( (t->len == 6) &&
	     !bcmp(t->text,"switch",6) &&
	     ((thisbl=CST_SWITCH),1) ) ) )
     { do
	{ tf[0] = get_token();
	  if ((tf[0]->len == 1) && (tf[0]->text[0] == '<'))
	   { tf[1] = get_token();
	     if (tf[1]->text[0] == '"')
	      { tf[2] = get_token();
		if ((tf[2]->len == 1) && (tf[2]->text[0] == '>'))
		 { cs = push_csstack();
		   cs->type = thisbl;
		   cs->tag = strdup(tf[1]->text);
		   free_token(tf[0]);
		   free_token(tf[1]);
		   free_token(tf[2]);
		   put_free_token(t);
		   t = get_token();
		   if (cs->type == CST_DO)
		    { if ((t->len != 1) || (t->text[0] != '{'/*}*/))
		       { fprintf(stderr,"%s: tagged do not followed by {\n"/*}*/,__progname);
			 fprintf(stderr,"# %d %s\n",t->loc.line,t->loc.file->file);
			 exit(1);
		       }
		      cs->state = CSS_BODY;
		    }
		   else
		    { if ((t->len != 1) || (t->text[0] != '('))
		       { fprintf(stderr,"%s: tagged for/while/switch not followed by {\n"/*}*/,__progname);
			 fprintf(stderr,"# %d %s\n",t->loc.line,t->loc.file->file);
			 exit(1);
		       }
		      cs->state = CSS_PAREN;
		    }
		   push_token(t);
		   t = 0;
		   break;
		 }
		push_token(tf[2]);
	      }
	     push_token(tf[1]);
	   }
	  push_token(tf[0]);
	} while (0);
     }
    else if ( t &&
	      ( ( (t->len == 5) &&
		  !bcmp(t->text,"break",5) &&
		  ( (findbits=CST_DO|CST_FOR|CST_WHILE|CST_SWITCH),
		    (labelvar=&labelvar_break),
		    1 ) ) ||
		( (t->len == 8) &&
		  !bcmp(t->text,"continue",8) &&
		  ( (findbits=CST_DO|CST_FOR|CST_WHILE),
		    (labelvar=&labelvar_cont),
		    1 ) ) ) )
     { do
	{ tf[0] = get_token();
	  if ((tf[0]->len == 1) && (tf[0]->text[0] == '<'))
	   { tf[1] = get_token();
	     if (tf[1]->text[0] == '"')
	      { tf[2] = get_token();
		if ((tf[2]->len == 1) && (tf[2]->text[0] == '>'))
		 { int *lv;
		   cs = find_tag(tf[1]->text,findbits);
		   if (! cs)
		    { fprintf(stderr,"%s <%s>: tag not found\n",t->text,tf[1]->text);
		      fprintf(stderr,"# %d%s\n",t->loc.line,t->loc.file->file);
		      exit(1);
		    }
		   lv = (*labelvar)(cs);
		   if (*lv < 0) *lv = nextlabel();
		   out_str(" goto LAB__",-1);
		   out_num(*lv);
		   free_token(tf[0]);
		   free_token(tf[1]);
		   free_token(tf[2]);
		   free_token(t);
		   t = 0;
		   break;
		 }
		push_token(tf[2]);
	      }
	     push_token(tf[1]);
	   }
	  push_token(tf[0]);
	} while (0);
     }
    else if ( t &&
	      ( ( (t->len == 4) &&
		  !bcmp(t->text,"case",4) ) ||
		( (t->len == 7) &&
		  !bcmp(t->text,"default",7) ) ) )
     { do
	{ tf[0] = get_token();
	  if ((tf[0]->len == 1) && (tf[0]->text[0] == '<'))
	   { tf[1] = get_token();
	     if (tf[1]->text[0] == '"')
	      { tf[2] = get_token();
		if ((tf[2]->len == 1) && (tf[2]->text[0] == '>'))
		 { TOKEN **tv;
		   int at;
		   int nt;
		   int ques;
		   int bl;
		   int pl;
		   cs = find_tag(tf[1]->text,CST_SWITCH);
		   if (! cs)
		    { fprintf(stderr,"%s <%s>: tag not found\n",t->text,tf[1]->text);
		      fprintf(stderr,"# %d%s\n",t->loc.line,t->loc.file->file);
		      exit(1);
		    }
		   free_token(tf[0]);
		   free_token(tf[1]);
		   free_token(tf[2]);
		   tv = 0;
		   at = 0;
		   nt = 0;
		   ques = 0;
		   bl = 0;
		   pl = 0;
		   while (1)
		    { if (nt >= at) tv = realloc(tv,(at=nt+8)*sizeof(*tv));
		      tv[nt++] = t;
		      if (t->len == 1)
		       { switch (t->text[0])
			  { case '(': pl ++; break;
			    case ')': pl --; break;
			    case '{'/*}*/: bl ++; break;
			    case /*{*/'}': bl --; break;
			    case '?':
			       if ((bl == 0) && (pl == 0)) ques ++;
			       break;
			    case ':':
			       if ((bl == 0) && (pl == 0))
				{ if (ques > 0)
				   { ques --;
				     continue;
				   }
				  goto case_done;
				}
			       break;
			  }
		       }
		      t = get_token();
		    }
case_done:;
		   t = 0;
		   cl = malloc(sizeof(CASELABEL));
		   cl->link = cs->cases;
		   cs->cases = cl;
		   cl->ntokens = nt;
		   cl->tokens = tv;
		   cl->label = nextlabel();
		   out_str(" LAB__",-1);
		   out_num(cl->label);
		   out_str(":;",2);
		   break;
		 }
		push_token(tf[2]);
	      }
	     push_token(tf[1]);
	   }
	  push_token(tf[0]);
	} while (0);
     }
    put_free_token(t);
  }
 free_token(t);
 while (tok_push)
  { t = get_token();
    free_token(t);
  }
 out_str("\n",1);
}

static void copyback(void)
{
 unsigned long long int total;
 int r;
 unsigned char *bp;
 int left;
 int w;

 total = 0;
 if (lseek(tmp_fd,0,SEEK_SET) < 0)
  { fprintf(stderr,"%s: can't rewind %s: %s\n",__progname,tmpfn,strerror(errno));
    exit(1);
  }
 if (lseek(in_fd,0,SEEK_SET) < 0)
  { fprintf(stderr,"%s: can't rewind %s: %s\n",__progname,infn,strerror(errno));
    exit(1);
  }
 while (1)
  { r = read(tmp_fd,&in_buf[0],BUF_SIZE);
    if (r < 0)
     { fprintf(stderr,"%s: rereading %s: %s\n",__progname,tmpfn,strerror(errno));
       exit(1);
     }
    if (r == 0) break;
    bp = &in_buf[0];
    left = r;
    while (left > 0)
     { w = write(in_fd,bp,left);
       if (w < 0)
	{ fprintf(stderr,"%s: rewriting %s: %s\n",__progname,infn,strerror(errno));
	  exit(1);
	}
       if (w == 0)
	{ fprintf(stderr,"%s: rewriting %s: zero write\n",__progname,infn);
	  exit(1);
	}
       bp += w;
       left -= w;
     }
    total += r;
  }
 ftruncate(in_fd,total);
}

int main(int, char **);
int main(int ac, char **av)
{
 if (ac != 2)
  { fprintf(stderr,"Usage: %s file.i\n",__progname);
    exit(1);
  }
 infn = av[1];
 in_fd = open(infn,O_RDWR,0);
 if (in_fd < 0)
  { fprintf(stderr,"%s: %s: %s\n",__progname,infn,strerror(errno));
    exit(1);
  }
 setup_tmp();
 process();
 out_flush();
 copyback();
 exit(0);
}
