/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: keyboard.c,v 35004.27 1997/11/16 22:04:58 hawkeye Exp $ */

/**************************************************
 * Fugue keyboard handling.
 * Handles all keyboard input and keybindings.
 **************************************************/

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "macro.h"	/* Macro, find_macro(), do_macro()... */
#include "keyboard.h"
#include "output.h"	/* iput(), idel(), redraw()... */
#include "history.h"	/* history_sub() */
#include "expand.h"	/* process_macro() */
#include "search.h"
#include "commands.h"
#include "tty.h"	/* no_tty */

static int literal_next = FALSE;
static TrieNode *keynode = NULL;	/* current node matched by input */

int pending_line = FALSE;
int pending_input = FALSE;
TIME_T keyboard_time = 0;

static int  NDECL(dokey_newline);
static int  FDECL(replace_input,(Aline *aline));
static void FDECL(handle_input_string,(CONST char *input, unsigned int len));


STATIC_BUFFER(scratch);                 /* buffer for manipulating text */
STATIC_BUFFER(current_input);           /* unprocessed keystrokes */
static TrieNode *keytrie = NULL;        /* root of keybinding trie */

Stringp keybuf;                         /* input buffer */
int keyboard_pos = 0;                   /* current position in buffer */

/*
 * Some dokey operations are implemented internally with names like
 * DOKEY_FOO; others are implemented as macros in stdlib.tf with names
 * like /dokey_foo.  handle_dokey_command() looks first for an internal
 * function in efunc_table[], then for a macro, so all operations can be done
 * with "/dokey foo".  Conversely, internally-implemented operations should
 * have macros in stdlib.tf of the form "/def dokey_foo = /dokey foo",
 * so all operations can be performed with "/dokey_foo".
 */
enum {
#define bicode(a, b)  a
#include "keylist.h"
#undef bicode
};

static CONST char *efunc_table[] = {
#define bicode(a, b)  b
#include "keylist.h"
#undef bicode
};


void init_keyboard()
{
    Stringinit(keybuf);
}

/* Find the macro assosiated with <key> sequence. */
Macro *find_key(key)
    CONST char *key;
{
    return (Macro *)trie_find(keytrie, (unsigned char*)key);
}

int bind_key(spec)   /* install Macro's binding in key structures */
    Macro *spec;
{
    Macro *macro;
    int status;

    if ((macro = find_key(spec->bind))) {
        if (redef) {
            kill_macro(macro);
            /* intrie is guaranteed to succeed */
        } else {
            eprintf("Binding %s already exists.", ascii_to_print(spec->bind));
            return 0;
        }
    }

    status = intrie(&keytrie, spec, (unsigned char*)spec->bind);

    if (status < 0) {
        eprintf("'%s' is %s an existing keybinding.",
            ascii_to_print(spec->bind),
            (status == TRIE_SUPER) ? "prefixed by" : "a prefix of");
        return 0;
    }

    if (macro && redef)
        do_hook(H_REDEF, "%% Redefined %s %s", "%s %s",
            "binding", ascii_to_print(spec->bind));

    return 1;
}

void unbind_key(macro)
    Macro *macro;
{
    untrie(&keytrie, (unsigned char*)macro->bind);
    keynode = NULL;  /* in case it pointed to a node that no longer exists */
}

/* returns 0 at EOF, 1 otherwise */
int handle_keyboard_input(int read_flag)
{
    char buf[64];
    CONST char *s;
    int i, count = 0;
    static int key_start = 0;
    static int input_start = 0;
    static int place = 0;
    static int eof = 0;

    if (!eof && read_flag) {
        /* read a block of text */
        if ((count = read(STDIN_FILENO, buf, sizeof(buf))) < 0) {
            /* error or interrupt */
            if (errno == EINTR) return 1;
            die("handle_keyboard_input: read", errno);
        } else if (count > 0) {
            /* something was read */
            keyboard_time = time(NULL);
        } else {
            /* nothing was read, and nothing is buffered */
            if (no_tty) {
                eof = 1;
                /* Don't close stdin; we don't want the fd to be reused. */
#if 0
            } else {
                internal_error(__FILE__, __LINE__);
                eputs("read 0 from stdin tty");
#endif
            }
        }
    }

    if (count == 0 && place == 0)
        return !eof;

    for (i = 0; i < count; i++) {
        if (istrip) buf[i] &= 0x7F;
        if (!isprint(buf[i]) && buf[i] & 0x80) {
            if (meta_esc)
                Stringadd(current_input, '\033');
            buf[i] &= 0x7F;
        }
        Stringadd(current_input, mapchar(buf[i]));
    }

    s = current_input->s;
    if (!s) return !eof; /* no good chars; current_input not yet allocated */
    while (place < current_input->len) {
        if (!keynode) keynode = keytrie;
        if ((pending_input = pending_line))
            break;
        if (literal_next) {
            place++;
            key_start++;
            literal_next = FALSE;
            continue;
        }
        while (place < current_input->len && keynode && keynode->children)
            keynode = keynode->u.child[(unsigned char)s[place++]];
        if (!keynode) {
            /* No keybinding match; check for builtins. */
            if (s[key_start] == '\n' || s[key_start] == '\r') {
                handle_input_string(s + input_start, key_start - input_start);
                place = input_start = ++key_start;
                dokey_newline();
                /* handle_input_line(); */
            } else if (s[key_start] == '\b' || s[key_start] == '\177') {
                handle_input_string(s + input_start, key_start - input_start);
                place = input_start = ++key_start;
                do_kbdel(keyboard_pos - 1);
            } else {
                /* No builtin; try a suffix. */
                place = ++key_start;
            }
            keynode = NULL;
        } else if (!keynode->children) {
            /* Total match; process everything up to here and call the macro. */
            Macro *macro = (Macro *)keynode->u.datum;
            handle_input_string(s + input_start, key_start - input_start);
            key_start = input_start = place;
            keynode = NULL;  /* before do_macro(), for reentrance */
            do_macro(macro, NULL);
        } /* else, partial match; just hold on to it for now. */
    }

    /* Process everything up to a possible match. */
    handle_input_string(s + input_start, key_start - input_start);

    /* Shift the window if there's no pending partial match. */
    if (key_start >= current_input->len) {
        Stringterm(current_input, 0);
        place = key_start = 0;
    }
    input_start = key_start;
    if (pending_line && !read_depth)
        handle_input_line();
    return !eof;
}

/* Update the input window and keyboard buffer. */
static void handle_input_string(input, len)
    CONST char *input;
    unsigned int len;
{
    if (len == 0) return;

    /* if this is a fresh line, input history is already synced;
     * if user deleted line, input history is already synced;
     * if called from replace_input, we don't want input history synced.
     */
    if (keybuf->len) sync_input_hist();

    if (keyboard_pos == keybuf->len) {                    /* add to end */
        Stringncat(keybuf, input, len);
    } else if (insert) {                                  /* insert in middle */
        Stringcpy(scratch, keybuf->s + keyboard_pos);
        Stringterm(keybuf, keyboard_pos);
        Stringncat(keybuf, input, len);
        SStringcat(keybuf, scratch);
    } else if (keyboard_pos + len < keybuf->len) {        /* overwrite */
        memcpy(keybuf->s + keyboard_pos, input, len);
    } else {                                              /* write past end */
        Stringterm(keybuf, keyboard_pos);
        Stringncat(keybuf, input, len);
    }                      
    keyboard_pos += len;
    iput(len);
}


struct Value *handle_input_command(args)
    char *args;
{
    handle_input_string(args, strlen(args));
    return newint(1);
}


/*
 *  Builtin key functions.
 */

struct Value *handle_dokey_command(args)
    char *args;
{
    CONST char **ptr;
    STATIC_BUFFER(buffer);
    Macro *macro;

    ptr = (CONST char **)binsearch((GENERIC*)args, (GENERIC*)efunc_table,
        sizeof(efunc_table)/sizeof(char*), sizeof(char*), cstrstructcmp);

    if (!ptr) {
        Stringcat(Stringcpy(buffer, "dokey_"), args);
        if ((macro = find_macro(buffer->s)))
            return newint(do_macro(macro, NULL));
        else eprintf("No editing function %s", args); 
        return newint(0);
    }

    switch (ptr - efunc_table) {

    case DOKEY_FLUSH:      return newint(screen_flush(FALSE));
    case DOKEY_HPAGE:      return newint(dokey_hpage());
    case DOKEY_LINE:       return newint(dokey_line());
    case DOKEY_LNEXT:      return newint(literal_next = TRUE);
    case DOKEY_NEWLINE:    return newint(dokey_newline());
    case DOKEY_PAGE:       return newint(dokey_page());
    case DOKEY_RECALLB:    return newint(replace_input(recall_input(-1,FALSE)));
    case DOKEY_RECALLBEG:  return newint(replace_input(recall_input(-2,FALSE)));
    case DOKEY_RECALLEND:  return newint(replace_input(recall_input(2,FALSE)));
    case DOKEY_RECALLF:    return newint(replace_input(recall_input(1,FALSE)));
    case DOKEY_REDRAW:     return newint(redraw());
    case DOKEY_REFRESH:    return newint((logical_refresh(), keyboard_pos));
    case DOKEY_SEARCHB:    return newint(replace_input(recall_input(-1,TRUE)));
    case DOKEY_SEARCHF:    return newint(replace_input(recall_input(1,TRUE)));
    case DOKEY_SELFLUSH:   return newint(screen_flush(TRUE));
    default:               return newint(0); /* impossible */
    }
}

static int dokey_newline()
{
    reset_outcount();
    inewline();
    /* We might be in the middle of a macro (^M -> /dokey newline) now,
     * so we can't process the input now, or weird things will happen with
     * current_command and mecho.  So we just set a flag and wait until
     * later when things are cleaner.
     */
    pending_line = TRUE;
    return 1;  /* return value isn't really used */
}

static int replace_input(aline)
    Aline *aline;
{
    if (!aline) {
        bell(1);
        return 0;
    }
    if (keybuf->len) {
        Stringterm(keybuf, keyboard_pos = 0);
        logical_refresh();
    }
    handle_input_string(aline->str, aline->len);
    return 1;
}

int do_kbdel(place)
    int place;
{
    if (place >= 0 && place < keyboard_pos) {
        Stringcpy(scratch, keybuf->s + keyboard_pos);
        SStringcat(Stringterm(keybuf, place), scratch);
        idel(place);
    } else if (place > keyboard_pos && place <= keybuf->len) {
        Stringcpy(scratch, keybuf->s + place);
        SStringcat(Stringterm(keybuf, keyboard_pos), scratch);
        idel(place);
    } else {
        bell(1);
    }
    sync_input_hist();
    return keyboard_pos;
}

#define isinword(c) (isalnum(c) || (wordpunct && strchr(wordpunct, (c))))

int do_kbword(dir)
    int dir;
{
    int stop = (dir < 0) ? -1 : keybuf->len;
    int place = keyboard_pos - (dir < 0);

    while (place != stop && !isinword(keybuf->s[place])) place += dir;
    while (place != stop && isinword(keybuf->s[place])) place += dir;
    return place + (dir < 0);
}

int do_kbmatch()
{
    static CONST char *braces = "(){}[]";
    CONST char *type;
    int dir, stop, depth = 0, place = keyboard_pos;

    while (1) {
        if (place >= keybuf->len) return -1;
        if ((type = strchr(braces, keybuf->s[place]))) break;
        ++place;
    }
    dir = ((type - braces) % 2) ? -1 : 1;
    stop = (dir < 0) ? -1 : keybuf->len;
    do {
        if      (keybuf->s[place] == type[0])   depth++;
        else if (keybuf->s[place] == type[dir]) depth--;
        if (depth == 0) return place;
    } while ((place += dir) != stop);
    return -1;
}

int handle_input_line()
{
    String *line;

    SStringcpy(scratch, keybuf);
    Stringterm(keybuf, keyboard_pos = 0);
    pending_line = FALSE;

    if (*scratch->s == '^') {
        if (!(line = history_sub(scratch->s + 1))) {
            oputs("% No match.");
            return 0;
        }
        SStringcpy(keybuf, line);
        iput(keyboard_pos = keybuf->len);
        inewline();
        Stringterm(keybuf, keyboard_pos = 0);
    } else
        line = scratch;

    if (kecho) tfprintf(tferr, "%s%S", kprefix, line);
    record_input(line->s);
    return process_macro(line->s, NULL, sub);
}

#ifdef DMALLOC
void free_keyboard()
{
    Stringfree(keybuf);
    Stringfree(scratch);
    Stringfree(current_input);
}
#endif
