/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef SOURCE_CODE_BUILDER_SOURCE_LOCATION
#define SOURCE_CODE_BUILDER_SOURCE_LOCATION
/*
 * This is an override for <source_location>, to get the functionality
 *  turned on regardless.  This is needed because <source_location>
 *  protects its implementation with
	#if __cplusplus > 201703L && __has_builtin(__builtin_source_location)
 *  but our __cplusplus is exactly 201703L.  So I added an override.
 *  To detect whether the override works, I also made <source_location>
 *  #define FORCE_GOT_SOURCE_LOCATION if FORCE_GET_SOURCE_LOCATION was
 *  implemented.  This means we can build this same file on a stock
 *  system and all that'll happen is we don't get the debugging assist
 *  of $P4C_BUILDER_TRACE.  (If we ever build this on a system where
 *  <source_location> works without modification, we just need to
 *  manually define FORCE_GOT_SOURCE_LOCATION somewhere.)
 *
 * We could just yank the implementation from there, but (a) that would
 *  probably be a license violation (at least, I'm not confident it
 *  isn't) and (b) that would risk breakage when built on a system
 *  where the guts of <source_location> are different.
 */
#define FORCE_GET_SOURCE_LOCATION
#define consteval constexpr // some variants need this
#include <source_location>
#undef consteval

#ifdef FORCE_GOT_SOURCE_LOCATION
#define SRCLOC std::source_location
#else
namespace P4::Util {
struct source_location {
  private:
    static const source_location val;
  public:
    static source_location current() noexcept { return(val); }
    static const char *file_name() { return("?"); }
    static const char *function_name() { return("?"); }
    static unsigned int line() { return(0); }
    static unsigned int column() { return(0); }
  } ;
#define SRCLOC P4::Util::source_location
}
#endif // FORCE_GOT_SOURCE_LOCATION
#endif // SOURCE_CODE_BUILDER_SOURCE_LOCATION

#ifndef LIB_SOURCECODEBUILDER_H_
#define LIB_SOURCECODEBUILDER_H_

#include <ctype.h>

#include "absl/strings/cord.h"
#include "absl/strings/str_format.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/stringify.h"

namespace P4::Util {
class SourceCodeBuilder {
    int indentLevel;  // current indent level
    unsigned indentAmount;

    absl::Cord buffer;
    bool endsInSpace;
    bool supressSemi = false;

 private:
    const char *fne(const char *n)
    {
      const char *p;
      const char *s;
      for (p=n,s=0;*p;p++) if (*p == '/') s = p;
      return(s?s+1:n);
    }
    static unsigned int trace_serial;
    static unsigned char trace_serial_buf[16];
    static int trace_state;
    static FILE *trace_file;
    static SourceCodeBuilder *last_user;
    static int builder_keep_trace()
    {
      char *env;
      env = getenv("P4C_BUILDER_TRACE");
      if (! env) return(0);
      trace_file = fopen(env,"r+");
      if (! trace_file) return(0);
      return(1);
    }
    bool want_trace()
    {
      switch (trace_state)
       { case 0:
	    trace_state = builder_keep_trace() + 1;
	    return(trace_state-1);
	    break;
	 case 1:
	    return(false);
	    break;
	 case 2:
	    return(true);
	    break;
	 default:
	    BUG("impossible keep_trace value");
	    break;
       }
    }
    void write_vis(const char *s)
    {
      while (1)
       { switch (*s)
	  { case '\0':
	       return;
	       break;
	    case '\a': fprintf(trace_file,"\\a"); break;
	    case '\b': fprintf(trace_file,"\\b"); break;
// grr	    case '\e': fprintf(trace_file,"\\e"); break;
	    case   27: fprintf(trace_file,"\\e"); break;
	    case '\f': fprintf(trace_file,"\\f"); break;
	    case '\n': fprintf(trace_file,"\\n"); break;
	    case '\r': fprintf(trace_file,"\\r"); break;
	    case '\t': fprintf(trace_file,"\\t"); break;
	    case '\v': fprintf(trace_file,"\\v"); break;
	    default:
	       unsigned char c = *s;
	       if ((c < 32) || ((c > 126) && (c < 160)))
		{ if ((s[1] >= '0') && (s[1] <= '9'))
		   { fprintf(trace_file,"\\%03o",c);
		   }
		  else
		   { fprintf(trace_file,"\\%o",c);
		   }
		}
	       else
		{ putc(c,trace_file);
		}
	       break;
	  }
	 s ++;
       }
    }
    void log_user(void)
    {
      if (last_user != this)
       { last_user = this;
	 fprintf(trace_file,"[builder = %p]\n",(void *)last_user);
       }
    }
    const char *trace_serial_string()
    {
#define BASECHARS "#%+,-./0123456789:=@ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz"
#define BASE 73
      int x;
      unsigned long long int n;
      unsigned long long int s;
      s = trace_serial;
      for (n=BASE;n<=s;n*=BASE) ;
      x = 0;
      trace_serial_buf[x++] = 0xab;
      for (n/=BASE;n;n/=BASE)
       { trace_serial_buf[x++] = BASECHARS[s/n];
	 s %= n;
       }
      trace_serial_buf[x++] = 0xbb;
      trace_serial_buf[x++] = '\0';
      return((const char *)&trace_serial_buf[0]);
#undef BASECHARS
#undef BASE
    }
    template <typename... Args>
    void save_trace(const SRCLOC loc, const absl::FormatSpec<Args...> &format, Args &&...args)
    {
      const char *ts;
      if (! want_trace()) return;
      ts = trace_serial_string();
      trace_serial ++;
      log_user();
      auto s = absl::StrFormat(format,std::forward<Args>(args)...);
      const char *cp = s.c_str();
      fprintf(trace_file,"%s %s line %d [%s]\n",ts,fne(loc.file_name()),loc.line(),loc.function_name());
      putc('\t',trace_file);
      write_vis(cp);
      putc('\n',trace_file);
      fflush(trace_file);
      buffer.Append(ts);
    }
    void save_trace_more(const char *s)
    {
      if (! want_trace()) return;
      log_user();
      putc('\t',trace_file);
      write_vis(s);
      putc('\n',trace_file);
      fflush(trace_file);
    }
    void save_trace_more(const cstring s)
    {
      save_trace_more(s.c_str());
    }
#define LOCARG const SRCLOC loc = SRCLOC::current()

 public:
    SourceCodeBuilder(LOCARG) : indentLevel(0), indentAmount(4), endsInSpace(false)
    {
      save_trace(loc,"created %p",(void *)this);
    }
    ~SourceCodeBuilder()
    {
      save_trace_more(absl::StrFormat("destroyed %p",(void *)this));
    }

    void increaseIndent(LOCARG) {
	indentLevel += indentAmount;
	save_trace(loc,"increase indent to %d",indentLevel);
    }
    void decreaseIndent(LOCARG) {
        indentLevel -= indentAmount;
        if (indentLevel < 0) BUG("Negative indent");
	save_trace(loc,"decrease indent to %d",indentLevel);
    }
    void newline(LOCARG) {
	save_trace(loc,"newline");
        buffer.Append("\n");
        endsInSpace = true;
    }
    void spc(LOCARG) {
	save_trace(loc,"spc (%s)",endsInSpace?"unnecessary":"appended");
        if (!endsInSpace) buffer.Append(" ");
        endsInSpace = true;
    }

    void append(cstring str, LOCARG) { append(str.c_str(),loc); }
    void appendLine(const char *str, LOCARG) {
	save_trace(loc,"appendLine:");
        append(str);
        newline();
    }
    void appendLine(cstring str, LOCARG) {
	save_trace(loc,"appendLine:");
        append(str);
        newline();
    }
    void append(const std::string &str, LOCARG) {
        if (str.empty()) return;
        endsInSpace = ::isspace(str.back());
	save_trace(loc,"append:");
	save_trace_more(str);
        buffer.Append(str);
    }
    [[deprecated("use string / char* version instead")]]
    void append(char c, LOCARG) {
        std::string str(1, c);
        append(str,loc);
    }
    void append(const char *str, LOCARG) {
        if (str == nullptr) BUG("Null argument to append");
        if (strlen(str) == 0) return;
        endsInSpace = ::isspace(str[strlen(str) - 1]);
	save_trace(loc,"append:");
	save_trace_more(str);
        buffer.Append(str);
    }

    template <typename... Args>
    void appendFormat_(const SRCLOC loc, const absl::FormatSpec<Args...> &format, Args &&...args) {
        // FIXME: Sink directly to cord
        append(absl::StrFormat(format, std::forward<Args>(args)...), loc);
    }
#define appendFormat(...) appendFormat_(SRCLOC::current(), __VA_ARGS__)
    void append(unsigned u, LOCARG) { appendFormat_(loc, "%d", u); }
    void append(int u, LOCARG) { appendFormat_(loc, "%d", u); }

    void endOfStatement(bool addNl = false, LOCARG) {
        if (!supressSemi) append(";",loc);
        supressSemi = false;
        if (addNl) newline(loc);
    }
    void supressStatementSemi() { supressSemi = true; }

    void blockStart(LOCARG) {
        append("{",loc);
        newline(loc);
        increaseIndent(loc);
    }

    void emitIndent(LOCARG) {
	save_trace(loc,"emitIndent");
        buffer.Append(std::string(indentLevel, ' '));
        if (indentLevel > 0) endsInSpace = true;
    }

    void blockEnd(bool nl, LOCARG) {
        decreaseIndent(loc);
        emitIndent(loc);
        append("}",loc);
        if (nl) newline(loc);
    }

    std::string toString() const { return std::string(buffer); }
    void commentStart(LOCARG) { append("/* ",loc); }
    void commentEnd(LOCARG) { append(" */",loc); }
    bool lastIsSpace() const { return endsInSpace; }
};
#define SCB_VARIABLE_DECLS \
	unsigned int P4::Util::SourceCodeBuilder::trace_serial = 0;\
	unsigned char P4::Util::SourceCodeBuilder::trace_serial_buf[sizeof(P4::Util::SourceCodeBuilder::trace_serial_buf)];\
	int P4::Util::SourceCodeBuilder::trace_state = 0;\
	FILE *P4::Util::SourceCodeBuilder::trace_file = 0;\
	P4::Util::SourceCodeBuilder *P4::Util::SourceCodeBuilder::last_user = 0;
#undef LOCARG
}  // namespace P4::Util

#endif /* LIB_SOURCECODEBUILDER_H_ */
