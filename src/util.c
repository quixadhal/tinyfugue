/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003, 2004 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: util.c,v 35004.123 2004/02/17 06:44:43 hawkeye Exp $";


/*
 * Fugue utilities.
 *
 * String handling routines
 * Mail checker
 * Cleanup routine
 */

#include "config.h"
#if HAVE_LOCALE_H
# include <locale.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include "port.h"
#include "tf.h"
#include "util.h"
#include "search.h"	/* for tfio.h */
#include "tfio.h"
#include "output.h"	/* fix_screen() */
#include "tty.h"	/* reset_tty() */
#include "signals.h"	/* core() */
#include "variable.h"
#include "parse.h"	/* for expression in nextopt() numeric option */

static RegInfo *reginfo = NULL;
static const unsigned char *re_tables = NULL;

typedef struct mail_info_s {	/* mail file information */
    char *name;			/* file name */
    int flag;			/* new mail? */
    int error;			/* error */
    time_t mtime;		/* file modification time */
    long size;			/* file size */
    struct mail_info_s *next;
} mail_info_t;

mail_info_t *maillist = NULL;

struct timeval tvzero = { 0, 0 };	/* zero (useful in tvcmp()) */
struct timeval mail_update = { 0, 0 };	/* next mail update (0==immediately) */
int mail_count = 0;
char tf_ctype[0x100];
const int feature_locale = HAVE_SETLOCALE - 0;
const int feature_subsecond = HAVE_GETTIMEOFDAY - 0;
const int feature_ftime = HAVE_STRFTIME - 0;
const int feature_TZ = HAVE_TZSET - 0;
char current_opt = '\0';
AUTO_BUFFER(featurestr);

struct feature features[] = {
    { "core",		&feature_core, },
    { "float",		&feature_float },
    { "ftime",		&feature_ftime },
    { "history",	&feature_history },
    { "IPv6",		&feature_IPv6 },
    { "locale",		&feature_locale },
    { "MCCPv2",		&feature_MCCPv2 },
    { "process",	&feature_process },
    { "SOCKS",		&feature_SOCKS },
    { "ssl",		&feature_ssl },
    { "subsecond",	&feature_subsecond },
    { "TZ",		&feature_TZ, },
    { NULL,		NULL }
};

static char *cmatch(const char *pat, int ch);
static void  free_maillist(void);
static RegInfo *tf_reg_compile_fl(const char *pattern, int optimize,
    const char *file, int line);

#define tf_reg_compile(pat, opt) \
    tf_reg_compile_fl(pat, opt, __FILE__, __LINE__)

#if !STDC_HEADERS
int lcase(x) char x; { return is_upper(x) ? tolower(x) : x; }
int ucase(x) char x; { return is_lower(x) ? toupper(x) : x; }
#endif

void init_util1(void)
{
    int i;
    struct feature *f;

    for (i = 0; i < 0x100; i++) {
        tf_ctype[i] = 0;
    }

    tf_ctype['+']  |= IS_UNARY | IS_ADDITIVE;
    tf_ctype['-']  |= IS_UNARY | IS_ADDITIVE;
    tf_ctype['!']  |= IS_UNARY;
    tf_ctype['*']  |= IS_MULT;
    tf_ctype['/']  |= IS_MULT;

    /* tf_ctype['.']  |= IS_ADDITIVE; */ /* doesn't work right */
    tf_ctype['"']  |= IS_QUOTE;
    tf_ctype['`']  |= IS_QUOTE;
    tf_ctype['\''] |= IS_QUOTE;
    tf_ctype['/']  |= IS_STATMETA;
    tf_ctype['%']  |= IS_STATMETA;
    tf_ctype['$']  |= IS_STATMETA;
    tf_ctype[')']  |= IS_STATMETA;
    tf_ctype['\n'] |= IS_STATMETA;
    tf_ctype['\\'] |= IS_STATMETA | IS_STATEND;
    tf_ctype[';']  |= IS_STATEND;
    tf_ctype['|']  |= IS_STATEND;

    tf_ctype['b']  |= IS_KEYSTART;  /* break */
    tf_ctype['d']  |= IS_KEYSTART;  /* do, done */
    tf_ctype['e']  |= IS_KEYSTART;  /* else, elseif, endif, exit */
    tf_ctype['i']  |= IS_KEYSTART;  /* if */
    tf_ctype['l']  |= IS_KEYSTART;  /* let */
    tf_ctype['r']  |= IS_KEYSTART;  /* return, result */
    tf_ctype['s']  |= IS_KEYSTART;  /* set */
    tf_ctype['t']  |= IS_KEYSTART;  /* then, test */
    tf_ctype['w']  |= IS_KEYSTART;  /* while */

    tf_ctype['B']  |= IS_KEYSTART;  /* BREAK */
    tf_ctype['D']  |= IS_KEYSTART;  /* DO, DONE */
    tf_ctype['E']  |= IS_KEYSTART;  /* ELSE, ELSEIF, ENDIF, EXIT */
    tf_ctype['I']  |= IS_KEYSTART;  /* IF */
    tf_ctype['L']  |= IS_KEYSTART;  /* LET */
    tf_ctype['R']  |= IS_KEYSTART;  /* RETURN, RESULT */
    tf_ctype['S']  |= IS_KEYSTART;  /* SET */
    tf_ctype['T']  |= IS_KEYSTART;  /* THEN, TEST */
    tf_ctype['W']  |= IS_KEYSTART;  /* WHILE */

    Stringtrunc(featurestr, 0);
    for (f = features; f->name; f++) {
	Stringadd(featurestr, *f->flag ? '+' : '-');
	Stringcat(featurestr, f->name);
	if (f[1].name) Stringadd(featurestr, ' ');
    }
}

/* Convert ascii string to printable string with "^X" forms. */
/* Returns pointer to static area; copy if needed. */
String *ascii_to_print(const char *str)
{
    STATIC_BUFFER(buffer);
    char c;

    for (Stringtrunc(buffer, 0); *str; str++) {
        c = unmapchar(*str);
        if (c == '^' || c == '\\') {
            Stringadd(Stringadd(buffer, '\\'), c);
        } else if (is_print(c)) {
            Stringadd(buffer, c);
        } else if (is_cntrl(c)) {
            Stringadd(Stringadd(buffer, '^'), CTRL(c));
        } else {
            Sprintf(buffer, "\\0x%2x", c);
        }
    }
    return buffer;
}

/* Convert a printable string containing "^X" and "\nnn" to real ascii. */
/* "^@" and "\0" are mapped to '\200'. */
/* Returns pointer to static area; copy if needed. */
String *print_to_ascii(const char *src)
{
    STATIC_BUFFER(dest);

    Stringtrunc(dest, 0);
    while (*src) {
        if (*src == '^') {
            Stringadd(dest, *++src ? mapchar(CTRL(*src)) : '^');
            if (*src) src++;
        } else if (*src == '\\' && is_digit(*++src)) {
            char c;
            c = strtochr(src, (char**)&src);
            Stringadd(dest, mapchar(c));
        } else Stringadd(dest, *src++);
    }
    return dest;
}

/* String handlers
 * These are heavily used functions, so speed is favored over simplicity.
 */

int enum2int(const char *str, long val, String *vec, const char *msg)
{
    int i;
    STATIC_BUFFER(buf);

    for (i = 0; vec[i].data; ++i) {
        if (str && cstrcmp(str, vec[i].data) == 0) return i;
    }
    if (str) {
        val = is_digit(*str) ? strtoint(str, &str) : -1;
    }
    if (val >= 0 && val < i) return val;
    SStringcpy(buf, &vec[0]);
    for (i = 1; vec[i].data; ++i) Sappendf(buf, ", %S", &vec[i]);
    eprintf("invalid %s value \"%s\".  Valid values are: %S", msg, str, buf);
    return -1;
}

#if 0 /* not used */
/* case-insensitive strchr() */
char *cstrchr(register const char *s, register int c)
{
    for (c = lcase(c); *s; s++) if (lcase(*s) == c) return (char *)s;
    return (c) ? NULL : (char *)s;
}
#endif

/* c may be escaped by preceeding it with e */
char *estrchr(register const char *s, register int c, register int e)
{
    while (*s) {
        if (*s == c) return (char *)s;
        if (*s == e) {
            if (*++s) s++;
        } else s++;
    }
    return NULL;
}

#ifdef sun
# if !HAVE_INDEX
/* Workaround for some buggy Solaris 2.x systems, where libtermcap calls index()
 * and rindex(), but they're not defined in libc.
 */
#undef index
char *index(const char *s, char c)
{
    return strchr(s, c);
}

#undef rindex
char *rindex(const char *s, char c)
{
    return strrchr(s, c);
}
# endif /* HAVE_INDEX */
#endif /* sun */

#ifndef cstrcmp
/* case-insensitive strcmp() */
int cstrcmp(register const char *s, register const char *t)
{
    register int diff = 0;
    while ((*s || *t) && !(diff = lcase(*s) - lcase(*t))) s++, t++;
    return diff;
}
#endif

#if 0  /* not used */
/* case-insensitive strncmp() */
int cstrncmp(register const char *s, register const char *t, int n)
{
    register int diff = 0;
    while (n && *s && !(diff = lcase(*s) - lcase(*t))) s++, t++, n--;
    return n ? diff : 0;
}
#endif

/* numarg
 * Converts argument to a nonnegative integer.  Returns -1 for failure.
 * The *str pointer will be advanced to beginning of next word.
 */
int numarg(char **str)
{
    int result;
    if (is_digit(**str)) {
        result = strtoint(*str, str);
    } else {
        eprintf("invalid or missing numeric argument");
        result = -1;
        while (**str && !is_space(**str)) ++*str;
    }
    while (is_space(**str)) ++*str;
    return result;
}

/* stringarg
 * Returns pointer to first space-delimited word in *str.  *str is advanced
 * to next word.  If end != NULL, *end will get pointer to end of first word;
 * otherwise, word will be nul terminated.
 */
char *stringarg(char **str, const char **end)
{
    char *start;
    while (is_space(**str)) ++*str;
    for (start = *str; (**str && !is_space(**str)); ++*str) ;
    if (end) *end = *str;
    else if (**str) *((*str)++) = '\0';
    if (**str)
        while (is_space(**str)) ++*str;
    return start;
}

int stringliteral(String *dest, char **str)
{
    char quote;

    Stringtrunc(dest, 0);
    quote = **str;
    for (++*str; **str && **str != quote; ++*str) {
        if (**str == '\\') {
            if ((*str)[1] == quote || (*str)[1] == '\\') {
                ++*str;
#if 0
	    } else if ((*str)[1] == '\n') {
		/* XXX handle backslash-newline */
#endif
            } else if ((*str)[1] && pedantic) {
                eprintf("warning: the only legal escapes within this quoted "
		    "string are \\\\ and \\%c.  \\\\%c is the correct way to "
		    "write a literal \\%c inside a quoted string.",
		    quote, (*str)[1], (*str)[1]);
	    }
        }
#if 0	/* 4.0 alpha (or earlier) - why? */
        Stringadd(dest, is_space(**str) ? ' ' : **str);
#else	/* 5.0 - wanted to allow ^M, ^J, etc in fake_recv() */
        Stringadd(dest, **str);
#endif
    }
    if (!**str) {
        Sprintf(dest, "unmatched %c", quote);
        return 0;
    }
    ++*str;
    return 1;
}

int regmatch_in_scope(Value *val, const char *pattern, String *str)
{
    if (reginfo) tf_reg_free(reginfo);
    if (!val) {
	if (!(reginfo = tf_reg_compile(pattern, 0)))
	    return 0;
    } else if (val->type & TYPE_REGEX) {
	/* use precompiled regexp */
	(reginfo = val->u.ri)->links++;
    } else {
	/* compile regexp, and store it on val for future reuse */
	if (!(reginfo = tf_reg_compile(pattern, 1)))
	    return 0;
	if (val->type == TYPE_STR) {
	    /* not a non-string or an already extended string */
	    (val->u.ri = reginfo)->links++;
	    val->type |= TYPE_REGEX;
	}
    }

    return tf_reg_exec(reginfo, str, NULL, 0);
}

RegInfo *new_reg_scope(RegInfo *ri, String *Str)
{
    RegInfo *old;

    old = reginfo;
    reginfo = ri ? ri : reginfo;	/* use new ri or inherit old reginfo */
    if (reginfo) reginfo->links++;

    return old;
}

void restore_reg_scope(RegInfo *old)
{
    if (reginfo) tf_reg_free(reginfo);
    reginfo = (RegInfo *)old;
}

/* returns length of substituted string, or -1 if no substitution */
/* n>=0:  nth substring */
/* n=-1:  left substring */
/* n=-2:  right substring */
int regsubstr(String *dest, int n)
{
    int idx;
    idx = (n < 0) ? 0 : n * 2;

    if (!(reginfo && reginfo->Str && n < reginfo->ovecsize/3 && reginfo->re &&
	reginfo->ovector[idx] >= 0))
    {
        return -1;
    }
    if (n < -2 || reginfo->ovector[idx+1] < 0) {
        internal_error(__FILE__, __LINE__, "invalid subexp %d", n);
        return -1;
    }
    if (n == -1) {
        SStringncat(dest, reginfo->Str, reginfo->ovector[0]);
        return reginfo->ovector[0];
    } else if (n == -2) {
        SStringocat(dest, reginfo->Str, reginfo->ovector[1]);
        return reginfo->Str->len - reginfo->ovector[1];
    } else {
        SStringoncat(dest, reginfo->Str, reginfo->ovector[idx],
            reginfo->ovector[idx+1] - reginfo->ovector[idx]);
	return reginfo->ovector[idx+1] - reginfo->ovector[idx];
    }
}

static RegInfo *tf_reg_compile_fl(const char *pattern, int optimize,
    const char *file, int line)
{
    RegInfo *ri;
    const char *emsg, *s;
    int eoffset, n;
    /* PCRE_DOTALL optimizes patterns starting with ".*" */
    int options = PCRE_DOLLAR_ENDONLY | PCRE_DOTALL | PCRE_CASELESS;

    ri = dmalloc(NULL, sizeof(RegInfo), file, line);
    if (!ri) return NULL;
    ri->extra = NULL;
    ri->ovector = NULL;
    ri->Str = NULL;
    ri->links = 1;

    if (warn_curly_re && (s = estrchr(pattern, '{', '\\')) &&
	(is_digit(s[1]) || s[1] == ','))
    {
	eprintf("%s", "Warning:  "
	    "regexp contains '{', which has a new meaning in version 5.0.  "
	    "(This warning can be disabled with '/set warn_curly_re=off'.)");
    }
    for (s = pattern; *s; s++) {
	if (*s == '\\') {
	    if (s[1]) s++;
	} else if (is_upper(*s)) {
	    options &= ~PCRE_CASELESS;
	    break;
	}
    }

    ri->re = pcre_compile((char*)pattern, options, &emsg, &eoffset, re_tables);
    if (!ri->re) {
	eprintf("regexp error: character %d: %s", eoffset, emsg);
	goto tf_reg_compile_error;
    }
    n = pcre_info(ri->re, NULL, NULL);
    if (n < 0) goto tf_reg_compile_error;
    ri->ovecsize = 3 * (n + 1);
    ri->ovector = dmalloc(NULL, sizeof(int) * ri->ovecsize, file, line);
    if (!ri->ovector) goto tf_reg_compile_error;
    if (optimize) {
	ri->extra = pcre_study(ri->re, 0, &emsg);
	if (emsg) {
	    eprintf("%s", emsg);
	    goto tf_reg_compile_error;
	}
    }
    return ri;

tf_reg_compile_error:
    tf_reg_free(ri);
    return NULL;
}

int tf_reg_exec(RegInfo *ri,
    String *Sstr,	/* String to match.  Will be saved for regsubstr(). */
    const char *str,	/* Used if Sstr is NULL; not saved. */
    int startoffset)
{
    int result, len;

    /* If ovector was stolen by find_and_run_matches(), make a new one. */
    if (!ri->ovector) {
	ri->ovector = MALLOC(sizeof(int) * ri->ovecsize);
	if (!ri->ovector) return 0;
    }

    /* Free old saved Str. */
    if (ri->Str) {
	Stringfree(ri->Str);
	ri->Str = NULL;
    }

    if (Sstr) {
	str = Sstr->data;
	len = Sstr->len;
    } else {
	len = strlen(str);
    }
    result = pcre_exec(ri->re, ri->extra, str, len, startoffset,
	startoffset ? PCRE_NOTBOL : 0, ri->ovector, ri->ovecsize);
    if (result < 0) {
	result = 0;
    } else {
	if (result == 0) result = 1; /* shouldn't happen, with ovector */
	if (Sstr) (ri->Str = Sstr)->links++;	/* save, for regsubstr() */
    }
    return result;
}

void tf_reg_free(RegInfo *ri)
{
    if (--ri->links > 0) return;
    if (ri->ovector) FREE(ri->ovector);
    if (ri->re) pcre_free(ri->re);
    if (ri->extra) pcre_free(ri->extra);
    if (ri->Str) Stringfree(ri->Str);
    FREE(ri);
}

/* call with (pat, NULL, -1) to zero-out pat.
 * call with (pat, str, -1) to init pat with some outside string (ie, strdup).
 * call with (pat, str, mflag) to init pat with some outside string.
 */
int init_pattern(Pattern *pat, const char *str, int mflag)
{
    return init_pattern_str(pat, str) && init_pattern_mflag(pat, mflag, 0);
}

int init_pattern_str(Pattern *pat, const char *str)
{
    pat->ri = NULL;
    pat->mflag = -1;
    pat->str = (!str) ? NULL : STRDUP(str);
    return 1;
}

int init_pattern_mflag(Pattern *pat, int mflag, int opt)
{
    char saved_opt = current_opt;
    current_opt = opt;
    if (!pat->str || pat->mflag >= 0) goto ok;
    pat->mflag = mflag;
    switch (mflag) {
    case MATCH_GLOB:
        if (smatch_check(pat->str)) goto ok;
	break;
    case MATCH_REGEXP:
#if 0
        char *s = pat->str;
        while (*s == '(' || *s == '^') s++;
        if (strncmp(s, ".*", 2) == 0)
            eprintf("Warning: leading \".*\" in a regexp is inefficient.");
#endif
	if ((pat->ri = tf_reg_compile(pat->str, 1))) goto ok;
	break;
    default:
        goto ok;
    }
    FREE(pat->str);
    pat->str = NULL;
    current_opt = saved_opt;
    return 0;
ok:
    current_opt = saved_opt;
    return 1;
}

void free_pattern(Pattern *pat)
{
    if (pat->str) FREE(pat->str);
    if (pat->ri) tf_reg_free(pat->ri);
    pat->str = NULL;
    pat->ri  = NULL;
}

int patmatch(
    const Pattern *pat,
    String *Sstr,	/* String to match.  Will be saved for regsubstr(). */
    const char *str)	/* Used if Sstr is NULL; not saved. */
{
    if (Sstr) str = Sstr->data;
    if (!pat->str) return 1;

    switch (pat->mflag) {
    /* Even a blank regexp must be exec'd, so Pn will work. */
    case MATCH_REGEXP: return !!tf_reg_exec(pat->ri, Sstr, str, 0);
    case MATCH_GLOB:   return !smatch(pat->str, str);
    case MATCH_SIMPLE: return !strcmp(pat->str, str);
    case MATCH_SUBSTR: return !!strstr(str, pat->str);
    default: eprintf("internal error: pat->mflag == %d", pat->mflag);
    }

    return 0;
}

/* class is a pointer to a string of the form "[...]..."
 * ch is compared against the character class described by class.
 * If ch matches, cmatch() returns a pointer to the char after ']' in class;
 * otherwise, cmatch() returns NULL.
 */
static char *cmatch(const char *class, int ch)
{
    int not;

    ch = lcase(ch);
    if ((not = (*++class == '^'))) ++class;

    while (1) {
        if (*class == ']') return (char*)(not ? class + 1 : NULL);
        if (*class == '\\') ++class;
        if (class[1] == '-' && class[2] != ']') {
            char lo = *class;
            class += 2;
            if (*class == '\\') ++class;
            if (ch >= lcase(lo) && ch <= lcase(*class)) break;
        } else if (lcase(*class) == ch) break;
        ++class;
    }
    return not ? NULL : (estrchr(++class, ']', '\\') + 1);
}

/* smatch_check() should be used on pat to check pattern syntax before
 * calling smatch().
 */
/* Based on code by Leo Plotkin. */

int smatch(const char *pat, const char *str)
{
    const char *start = str;
    static int inword = FALSE;

    while (*pat) {
        switch (*pat) {

        case '\\':
            pat++;
            if (lcase(*pat++) != lcase(*str++)) return 1;
            break;

        case '?':
            if (!*str || (inword && is_space(*str))) return 1;
            str++;
            pat++;
            break;

        case '*':
            while (*pat == '*' || *pat == '?') {
                if (*pat == '?') {
                    if (!*str || (inword && is_space(*str))) return 1;
                    str++;
                }
                pat++;
            }
            if (inword) {
                while (*str && !is_space(*str))
                    if (!smatch(pat, str++)) return 0;
                return smatch(pat, str);
            } else if (!*pat) {
                return 0;
            } else if (*pat == '{') {
                if (str == start || is_space(str[-1]))
                    if (!smatch(pat, str)) return 0;
                for ( ; *str; str++)
                    if (is_space(*str) && !smatch(pat, str+1)) return 0;
                return 1;
            } else if (*pat == '[') {
                while (*str) if (!smatch(pat, str++)) return 0;
                return 1;
            } else {
                char c = (pat[0] == '\\' && pat[1]) ? pat[1] : pat[0];
                for (c = lcase(c); *str; str++)
                    if (lcase(*str) == c && !smatch(pat, str))
                        return 0;
                return 1;
            }

        case '[':
            if (inword && is_space(*str)) return 1;
            if (!(pat = cmatch(pat, *str++))) return 1;
            break;

        case '{':
            if (str != start && !is_space(*(str - 1))) return 1;
            {
                const char *end;
                int result = 1;

                /* This can't happen if smatch_check is used first. */
                if (!(end = estrchr(pat, '}', '\\'))) {
                    eprintf("smatch: unmatched '{'");
                    return 1;
                }

                inword = TRUE;
                for (pat++; pat <= end; pat++) {
                    if ((result = smatch(pat, str)) == 0) break;
                    if (!(pat = estrchr(pat, '|', '\\'))) break;
                }
                inword = FALSE;
                if (result) return result;
                pat = end + 1;
                while (*str && !is_space(*str)) str++;
            }
            break;

        case '}': case '|':
            if (inword) return (*str && !is_space(*str));
            /* else FALL THROUGH to default case */

        default:
            if (lcase(*pat++) != lcase(*str++)) return 1;
            break;
        }
    }
    return lcase(*pat) - lcase(*str);
}

/* verify syntax of smatch pattern */
int smatch_check(const char *pat)
{
    int inword = FALSE;
    const char *patstart = pat;

    while (*pat) {
        switch (*pat) {
        case '\\':
            if (*++pat) pat++;
            break;
        case '[':
            if (!(pat = estrchr(pat, ']', '\\'))) {
                eprintf("glob error: unmatched '['");
                return 0;
            }
            pat++;
            break;
        case '{':
            if (inword) {
                eprintf("glob error: nested '{'");
                return 0;
            }
	    if (!(pat==patstart || is_space(pat[-1]) || strchr("*?]", pat[-1])))
	    {
                eprintf("glob error: '%c' before '{' can never match", pat[-1]);
		return 0;
	    }
            inword = TRUE;
            pat++;
            break;
        case '}':
	    if (!(!pat[1] || is_space(pat[1]) || strchr("*?[", pat[1]))) {
                eprintf("glob error: '%c' after '}' can never match", pat[1]);
		return 0;
	    }
            inword = FALSE;
            pat++;
            break;
        case '?':
        case '*':
        default:
	    if (inword && is_space(*pat)) {
                eprintf("glob error: space inside '{...}' can never match");
		return 0;
	    }
            pat++;
            break;
        }
    }
    if (inword) eprintf("glob error: unmatched '{'");
    return !inword;
}

/* remove leading and trailing spaces.  Modifies s and *s */
char *stripstr(char *s)
{
    char *end;

    while (is_space(*s)) s++;
    if (*s) {
        for (end = s + strlen(s) - 1; is_space(*end); end--);
        *++end = '\0';
    }
    return s;
}


/* General command option parser

   startopt should be called before nextopt.  args is the argument list to be
   parsed, opts is a string containing a series of codes describing valid
   options.  A letter or digit indicates a valid option character.  A '-'
   indicates that no option is expected; the argument should directly follow
   the dash.  A punctuation character following an option code indicates that
   it takes an argument:
     ':' string
     '#' integer
     '@' time of the form "-h:m[:s[.f]]" or "-s[.f]".
   The '-' option code must always take an argument.  String arguments may be
   omitted.

   nextopt returns the next option character.  The new offset is returned in
   *offp.  If option takes a string argument, a pointer to it is returned in
   *arg; an integer argument is returned in *num; a time argument is returned
   in *tvp.  If end of options is reached, nextopt returns '\0'.  " - " or
   " -- " marks the end of options, and is consumed.  "\0", "=", or a word not
   beggining with "-" marks the end of options, and is not consumed.  If an
   invalid option is encountered, an error message is printed and '?' is
   returned.

   Option Syntax Rules:
      All options must be preceded by '-'.
      Options may be grouped after a single '-'.
      There must be no space between an option and its argument.
      String option-arguments may be quoted.  Quotes in the arg must be escaped.
      All options must precede operands.
      A '--' or '-' with no option may be used to mark the end of the options.
*/

static String *optstr;
static const char *options;
static int inword;

void startopt(String *args, const char *opts)
{
    optstr = args;
    options = opts;
    inword = 0;
}

char nextopt(char **arg, void *uval, int *type, int *offp)
{
    char *q, *end, opt;
    STATIC_BUFFER(buffer);

    current_opt = '\0';
    if (!inword) {
        while (is_space(optstr->data[*offp])) (*offp)++;
        if (optstr->data[*offp] != '-') {
            return '\0';
        } else {
            if (optstr->data[++(*offp)] == '-') {
		(*offp)++;
		if (optstr->data[*offp] && !is_space(optstr->data[*offp])) {
		    eprintf("invalid option syntax: --%c", optstr->data[*offp]);
		    goto nextopt_error;
		}
	    }
	    if (!optstr->data[*offp] || is_space(optstr->data[*offp])) {
                while (is_space(optstr->data[*offp])) ++(*offp);
                return '\0';
            }
        }
    } else if (optstr->data[*offp] == '=') {
        /* '=' ends, but isn't consumed, for stuff like: /def -t"foo"=bar */
        return '\0';
    }

    current_opt = opt = optstr->data[*offp];

    if ((is_digit(opt) || opt == ':') && (q = strchr(options, '-'))) {
	/* valid time/number option */
	opt = '-';
    } else if ((opt != '@' && opt != ':' && opt != '#') &&
	(q = strchr(options, opt)))
    {
	/* valid letter/punctuation option */
	++*offp;
    } else {
	/* invalid option */
        int dash=1;
        const char *p;
        STATIC_BUFFER(helpbuf);
        Stringtrunc(helpbuf, 0);
        if (opt != '?')
	    eprintf("invalid option"); /* eprintf() shows current_opt */
        for (p = options; *p; p++) {
	    if (dash || is_punct(p[1])) Stringcat(helpbuf, " -");
	    if (*p != '-') Stringadd(helpbuf, *p);
	    dash=0;
	    if (p[1] == ':') { Stringcat(helpbuf,"<string>");  p++; dash=1; }
	    if (p[1] == '#') { Stringcat(helpbuf,"<integer>"); p++; dash=1; }
	    if (p[1] == '@') { Stringcat(helpbuf,"<time>");    p++; dash=1; }
        }
	current_opt = '\0'; /* so it doesn't appear in "options:" message */
        eprintf("options:%S", helpbuf);
	goto nextopt_error;
    }

    q++;

    /* option takes a string or numeric_expression argument */
    if (*q == ':' || *q == '#') {
        Stringtrunc(buffer, 0);
        if (is_quote(optstr->data[*offp])) {
            end = optstr->data + *offp;
            if (!stringliteral(buffer, &end)) {
                eprintf("%S", buffer);
		goto nextopt_error;
            }
            *offp = end - optstr->data;
        } else {
            while (optstr->data[*offp] && !is_space(optstr->data[*offp]))
                Stringadd(buffer, optstr->data[(*offp)++]);
        }
	if (arg)
	    *arg = buffer->data;
	if (type) *type = TYPE_STR;

	/* option takes a numeric argument expression */
	if (*q == '#') {
	    Value *val = expr_value(buffer->data);
	    if (!val) {
		goto nextopt_error;
	    }
	    *(int*)uval = valint(val);
	    freeval(val);
	    if (type) *type = TYPE_INT;
	}

    /* option takes a time argument */
    } else if (*q == '@') {
	Value val[1];
	if (!parsenumber(optstr->data + *offp, &end, TYPE_TIME, val)) {
            eprintf("%s", strerror(errno));
	    goto nextopt_error;
        }
	*(struct timeval*)uval = val->u.tval;
        *offp = end - optstr->data;
	if (type) *type = TYPE_TIME;

    /* option takes no argument */
    } else {
        if (arg) *arg = NULL;
	if (type) *type = 0;
    }

    inword = (optstr->data[*offp] && !is_space(optstr->data[*offp]));
    return opt;

nextopt_error:
    current_opt = '\0';
    return '?';
}

#if HAVE_TZSET
int ch_timezone(void)
{
    tzset();
    return 1;
}
#endif

int ch_locale(void)
{
#if HAVE_SETLOCALE
    const char *lang;

#define tf_setlocale(cat, name, value) \
    do { \
        lang = setlocale(cat, value); \
        if (lang) { \
            eprintf("%s category set to \"%s\" locale.", name, lang); \
        } else { \
            eprintf("Invalid locale for %s.", name); \
        } \
    } while (0)

    tf_setlocale(LC_CTYPE, "LC_CTYPE", "");
    tf_setlocale(LC_TIME,  "LC_TIME",  "");

    /* XXX memory leak: old re_tables can't be freed if any compiled regexps
     * use it.  But we don't expect the user to change locales very often
     * (typically, locale is set at startup, and then never changed).
     */
    re_tables = pcre_maketables();

    return 1;
#else
    eprintf("Locale support is unavailable.");
    return 1;
#endif /* HAVE_SETLOCALE */
}

int ch_maildelay(void)
{
    mail_update = tvzero;
    return 1;
}

int ch_mailfile(void)
{
    mail_info_t *info, **oldp, *newlist = NULL;
    char *path, *name;
    const char *end;

    path = (TFMAILPATH && *TFMAILPATH) ? TFMAILPATH : MAIL;
    if (path) {
	while (*(name = stringarg(&path, &end))) {
	    for (oldp = &maillist; *oldp; oldp = &(*oldp)->next) {
		if (strncmp(name, (*oldp)->name, end-name) == 0 &&
		    !(*oldp)->name[end-name])
			break;
	    }
	    if (*oldp) {
		info = *oldp;
		*oldp = (*oldp)->next;
	    } else {
		info = XMALLOC(sizeof(mail_info_t));
		info->name = strncpy(XMALLOC(end-name+1), name, end-name);
		info->name[end-name] = '\0';
		info->mtime = -2;
		info->size = -2;
		info->flag = 0;
		info->error = 0;
	    }
	    info->next = newlist;
	    newlist = info;
	}
    }
    free_maillist();
    maillist = newlist;
    ch_maildelay();
    return 1;
}

static void free_maillist(void)
{
    mail_info_t *info;
    while (maillist) {
        info = maillist;
        maillist = maillist->next;
        FREE(info->name);
        FREE(info);
    }
}

void init_util2(void)
{
    String *path;
    const char *name;

    ch_locale();

    if (MAIL || TFMAILPATH) {  /* was imported from environment */
        ch_mailfile();
#ifdef MAILDIR
    } else if ((name = getvar("LOGNAME")) || (name = getvar("USER"))) {
        (path = Stringnew(NULL, 0, 0))->links++;
        Sprintf(path, "%s/%s", MAILDIR, name);
        set_var_by_name("MAIL", path, 0);
        Stringfree(path);
#endif
    } else {
        eputs("% Warning:  Can't figure out name of mail file.");
    }
}

#if USE_DMALLOC
void free_util(void)
{
    Stringfree(featurestr);
    free_maillist();
    if (reginfo) {
	tf_reg_free(reginfo);
	reginfo = NULL;
    }
}
#endif


/* check_mail()
 * Enables the "(Mail)" indicator iff there is unread mail.
 * Calls the MAIL hook iff there is new mail.
 *
 * Logic:
 * If the file exists and is not empty, we follow this chart:
 *
 *                 || first   | new     | mtime same   | mtime changed
 * Timestamps      || test    | file    | as last test | since last test
 * ================++=========+=========+==============+=================
 * mtime <  atime  || 0,mail  | 0       | 0            | 0      
 * mtime == atime  || 0,mail  | 1,hook% | same         | 0
 * mtime >  atime  || 0,mail  | 1,hook  | 1            | 1,hook
 *
 * "0" or "1" means turn the "(Mail)" indicator off or on; "same" means
 * leave it in the same state it was in before the check.
 * "mail" means print a message.  "hook" means call the MAIL hook.
 *
 * [%]Problem:
 * If the file is created between checks, and mtime==atime, there is
 * no way to tell if the mail has been read.  If shell() or suspend()
 * is used to read mail, we can avoid this situation by checking mail
 * before and after shell() and suspend().  There is no way to avoid
 * it if mail is read in an unrelated process (e.g, another window).
 *
 * Note that mail readers can write to the mail file, causing a change 
 * in size, a change in mtime, and mtime==atime.
 */
void check_mail(void)
{
    struct stat buf;
    static int depth = 0;
    int old_mail_count = mail_count;
    mail_info_t *info;

    if (depth) return;                         /* don't allow recursion */
    depth++;

    if (!maillist || tvcmp(&maildelay, &tvzero) <= 0) {
        if (mail_count) {
	    mail_count = 0;
	    update_status_field(NULL, STAT_MAIL);
	}
        depth--;
        return;
    }

    mail_count = 0;
    for (info = maillist; info; info = info->next) {
        if (stat(expand_filename(info->name), &buf) == 0) {
            errno = (buf.st_mode & S_IFDIR) ? EISDIR : 0;
        }
        if (errno) {
            /* Error, or file does not exist. */
            if (errno == ENOENT) {
                info->error = 0;
            } else if (info->error != errno) {
                eprintf("%s: %s", info->name, strerror(errno));
                info->error = errno;
            }
            info->mtime = -1;
            info->size = -1;
            info->flag = 0;
            continue;
        } else if (buf.st_size == 0) {
            /* There is no mail (the file exists, but is empty). */
            info->flag = 0;
        } else if (info->size == -2) {
            /* There is mail, but this is the first time we've checked.
             * There is no way to tell if it has been read; assume it has. */
            info->flag = 0;
            oprintf("%% You have mail in %s", info->name);  /* not "new" */
        } else if (buf.st_mtime > buf.st_atime) {
            /* There is unread mail. */
            info->flag = 1; mail_count++;
            if (buf.st_mtime > info->mtime)
                do_hook(H_MAIL, "%% You have new mail in %s", "%s", info->name);
        } else if (info->size < 0 && buf.st_mtime == buf.st_atime) {
            /* File did not exist last time; assume new mail is unread. */
            info->flag = 1; mail_count++;
            do_hook(H_MAIL, "%% You have new mail in %s", "%s", info->name);
        } else if (buf.st_mtime > info->mtime || buf.st_mtime < buf.st_atime) {
            /* Mail has been read. */
            info->flag = 0;
        } else if (info->flag >= 0) {
            /* There has been no change since the last check. */
            mail_count += info->flag;
        }

        info->error = 0;
        info->mtime = buf.st_mtime;
        info->size = buf.st_size;
    }

    if (mail_count != old_mail_count)
        update_status_field(NULL, STAT_MAIL);

    depth--;
}


/* Converts a string to a numeric Value.
 * <typeset> is a bitmap of the types that are allowed.
 * *<endp> will get a pointer to the first character past the number.
 * If val is NULL, parsenumver() will allocate a Value.
 * Returns pointer to value for success, NULL for error.
 * Format	Type
 * ------	----
 * h:m[:s[.f]]	TIME
 * i[.f]Ee	FLOAT
 * i[.f]	TIME or FLOAT (depending on what's allowed and length of <f>).
 * i		INT
 */
Value *parsenumber(const char *str, char **caller_endp, int typeset, Value *val)
{
    int neg = 0, allocated, digits;
    char *endp;

    if ((allocated = !val))
	val = newval();

#if NO_FLOAT
    typeset &= ~TYPE_FLOAT;
#endif

    for ( ; *str == '+' || *str == '-'; str++) {
        if (*str == '-') neg = !neg;
    }

    if (typeset & TYPE_INT) {
	val->type = TYPE_INT;
	errno = 0;
        val->u.ival = strtolong(str, &endp);
	if ((typeset & TYPE_TIME) && (*endp == '.' || *endp == ':'))
	    goto parse_time;
	if (typeset & TYPE_FLOAT && (*endp == '.' || lcase(*endp) == 'e'))
	    goto parse_float;
	if (val->u.ival == LONG_MAX && errno) {
	    if (typeset & TYPE_FLOAT) goto parse_float;
	    eprintf("integer value too large");
	    goto fail;
	}
	if (neg) val->u.ival = -val->u.ival;
	goto success;
    }

parse_time:
    if (typeset & TYPE_TIME) {
	val->type = TYPE_TIME;
	errno = 0;
        val->u.tval.tv_usec = 0;
        val->u.tval.tv_sec = strtolong(str, &endp);
	if (*endp == ':') {
	    val->type |= TYPE_HMS;
	    if (val->u.tval.tv_sec > 596523) /* will wrap when *3600 */
		goto time_overflow;
	    val->u.tval.tv_sec *= 3600;
	    endp++;
	    if (is_digit(*endp)) {
		long fieldval;
		fieldval = strtolong(endp, &endp);
		if (fieldval > 35791394) /* will wrap when *60 */
		    goto time_overflow;
		fieldval *= 60;
		val->u.tval.tv_sec += fieldval;
		if (val->u.tval.tv_sec < 0) /* wrapped */
		    goto time_overflow;
		if (*endp == ':') {
		    ++endp;
		    if (is_digit(*endp)) {
			fieldval = strtolong(endp, &endp);
			if (fieldval == LONG_MAX && errno) /* wrapped */
			    goto time_overflow;
			val->u.tval.tv_sec += fieldval;
			if (val->u.tval.tv_sec < 0) /* wrapped */
			    goto time_overflow;
		    }
		}
	    }
	    if (*endp == '.') {
		endp++;
		for (digits=0; digits < 6 && is_digit(*endp); digits++, endp++)
		    val->u.tval.tv_usec = 10*val->u.tval.tv_usec + (*endp-'0');
		while (digits++ < 6)
		    val->u.tval.tv_usec *= 10;
		while (is_digit(*endp))
		    endp++;
	    }
	} else if (*endp == '.') {
	    if (val->u.tval.tv_sec == LONG_MAX && errno) {
		if (typeset & TYPE_FLOAT) goto parse_float;
		goto time_overflow;
	    }
	    endp++;
	    for (digits = 0; digits < 6 && is_digit(*endp); digits++, endp++)
		val->u.tval.tv_usec = 10 * val->u.tval.tv_usec + (*endp - '0');
	    if ((is_digit(*endp) || lcase(*endp) == 'e') &&
		(typeset & TYPE_FLOAT))
		    goto parse_float;
	    while (digits++ < 6)
		val->u.tval.tv_usec *= 10;
	    while (is_digit(*endp))
		endp++;
	} else {
	    if (val->u.tval.tv_sec == LONG_MAX && errno) {
		if (typeset & TYPE_FLOAT) goto parse_float;
		goto time_overflow;
	    }
	}
	if (neg) {
	    val->u.tval.tv_sec = -val->u.tval.tv_sec;
	    val->u.tval.tv_usec = -val->u.tval.tv_usec;
	}
	goto success;

time_overflow:
	errno = ERANGE;
	eprintf("time value too large");
	goto fail;
    }

parse_float:
    val->type = TYPE_FLOAT;
#if NO_FLOAT
    eprintf("floating point numeric values are not enabled.");
    goto fail;
#else
    val->u.fval = strtod(str, &endp);
    if (val->u.fval == HUGE_VAL && errno == ERANGE) {
        eprintf("numeric value too large");
        goto fail;
    }
    if (neg) val->u.fval = -val->u.fval;
    goto success;
#endif

success:
    if (caller_endp) *caller_endp = endp;
    val->count = 1;
    val->name = NULL;
    val->sval = NULL;
    return val;

fail:
    if (allocated)
	freeval(val);
    return NULL;
}

#if 0
int strtotime(struct timeval *tvp, const char *str, char **endp)
{
    Value val[1];

    if (!parsenumber(str, endp, TYPE_TIME | TYPE_INT, val))
	return -1;
    if (val->type & TYPE_TIME) {
	*tvp = val->u.tval;
	return 1;
    } else {
	tvp->tv_sec = val->u.ival;
	tvp->tv_usec;
	return 0;
    }
}
#endif


/* Converts a time-of-day to an absolute time within the last 24h.
 * BUG: doesn't handle switches to/from daylight savings time. (?)
 */
void abstime(struct timeval *tvp)
{
    struct tm *tm;
    time_t now;

    time(&now);
    tm = localtime(&now);
    tm->tm_hour = tvp->tv_sec / 3600;
    tm->tm_min = (tvp->tv_sec / 60) % 60;
    tm->tm_sec = tvp->tv_sec % 60;
    tm->tm_isdst = -1;
    tvp->tv_sec = mktime(tm);
    if (tvp->tv_sec > now)
        tvp->tv_sec -= 24 * 60 * 60;
    /* don't touch tvp->tv_usec */
}

void normalize_time(struct timeval *tvp)
{
    while (tvp->tv_usec < (tvp->tv_sec > 0 ? 0 : -999999)) {
        tvp->tv_sec--;
        tvp->tv_usec += 1000000;
    }
    while (tvp->tv_usec > (tvp->tv_sec < 0 ? 0 : 999999)) {
        tvp->tv_sec++;
        tvp->tv_usec -= 1000000;
    }
}

/* a = b - c */
void tvsub(struct timeval *a, struct timeval *b, struct timeval *c)
{
    a->tv_sec = b->tv_sec - c->tv_sec;
    a->tv_usec = b->tv_usec - c->tv_usec;
    normalize_time(a);
}

/* a = b + c */
void tvadd(struct timeval *a, struct timeval *b, struct timeval *c)
{
    a->tv_sec = b->tv_sec + c->tv_sec;
    a->tv_usec = b->tv_usec + c->tv_usec;
    normalize_time(a);
}

/* appends a formatted time string to buf.
 * If fmt is NULL, trailing zeros are omitted.
 */
void tftime(String *buf, String *fmt, struct timeval *intv)
{
    STATIC_STRING(defaultfmt, "%c", 0);
    STATIC_STRING(Ffmt, "%Y-%m-%d", 0);
    STATIC_STRING(Tfmt, "%H:%M:%S", 0);
    struct timeval tv = *intv;

    if (!fmt || strcmp(fmt->data, "@") == 0) {
        if (tv.tv_sec < 0 || tv.tv_usec < 0) {
	    tv.tv_usec = -tv.tv_usec;
	    if (tv.tv_sec == 0) Stringadd(buf, '-');
	    /* if (sec < 0), Sprintf will take care of the '-'.
	     * We shouldn't, because negating sec could cause overflow. */
	}
        Sappendf(buf, "%ld", tv.tv_sec);
#if HAVE_GETTIMEOFDAY
        Sappendf(buf, ".%06ld", tv.tv_usec);
        if (!fmt) {
            int i;
            for (i = 1; i < 6; i++)
                if (buf->data[buf->len - i] != '0') break;
            Stringtrunc(buf, buf->len - i + 1);
        }
#endif /* HAVE_GETTIMEOFDAY */
    } else {
#if HAVE_STRFTIME
	int i, start, oldlen;
        static char fmtbuf[4] = "%?";
        time_t adj_sec, adj_usec;

        if (!*fmt->data) fmt = defaultfmt;
        adj_sec = tv.tv_sec;
        adj_usec = tv.tv_usec;
        if (tv.tv_usec < 0) { adj_sec -= 1; adj_usec += 1000000; }
	for (start = i = 0; i < fmt->len; i++) {
            if (fmt->data[i] != '%') {
                /* do nothing */
	    } else {
		SStringoncat(buf, fmt, start, i - start);
		oldlen = buf->len;
		++i;
		if (fmt->data[i] == '@') {
		    tftime(buf, NULL, &tv);
		} else if (fmt->data[i] == '.') {
		    Sappendf(buf, "%06ld", adj_usec);
		} else if (fmt->data[i] == 's') {
		    Sappendf(buf, "%ld", adj_sec);
		} else if (fmt->data[i] == 'F') {
		    tftime(buf, Ffmt, &tv);
		} else if (fmt->data[i] == 'T') {
		    tftime(buf, Tfmt, &tv);
		} else {
		    struct tm *tm;
		    int j = 1;
		    tm = localtime(&adj_sec);
		    fmtbuf[j] = fmt->data[i];
		    if (fmtbuf[j] == 'E' || fmtbuf[j] == 'O')
			fmtbuf[++j] = fmt->data[++i];
		    fmtbuf[++j] = '\0';
		    Stringtrunc(buf, buf->len + 32);
		    buf->len += strftime(buf->data + buf->len, 32, fmtbuf, tm);
		}
		if (buf->charattrs || fmt->charattrs)
		    extend_charattrs(buf, oldlen,
			fmt->charattrs ? fmt->charattrs[i] : 0);
		start = i+1;
	    }
        }
	SStringoncat(buf, fmt, start, i - start);
#else
        char *str = ctime(&tv.tv_sec);
        Stringcat(buf, str, strlen(str)-1);  /* -1 removes ctime()'s '\n' */
#endif /* HAVE_STRFTIME */
    }
}

void die(const char *why, int err)
{
    fix_screen();
    reset_tty();
    if ((errno = err)) perror(why);
    else {
        fputs(why, stderr);
        fputc('\n', stderr);
    }
    exit(1);
}

