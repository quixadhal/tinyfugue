/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003, 2004, 2005 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: signals.c,v 35004.62 2005/04/18 03:15:36 kkeys Exp $";

/* Signal handling, core dumps, job control, and interactive shells */

#include "tfconfig.h"
#include <signal.h>
#include <setjmp.h>
#include "port.h"
#if DISABLE_CORE
# include <sys/time.h>
# include <sys/resource.h>
#endif
#include "tf.h"
#include "util.h"
#include "pattern.h"	/* for tfio.h */
#include "search.h"	/* for tfio.h */
#include "tfio.h"
#include "world.h" /* for process.h */
#include "process.h"
#include "tty.h"
#include "output.h"
#include "signals.h"
#include "variable.h"
#include "expand.h" /* current_command */

#ifdef TF_AIX_DECLS
struct rusage *dummy_struct_rusage;
union wait *dummy_union_wait;
#endif

/* POSIX.1 systems should define WIFEXITED and WEXITSTATUS, taking an |int|
 * parameter, in <sys/wait.h>.  For posix systems, we use them.  For non-posix
 * systems, we use our own.  For systems which falsely claim to be posix,
 * but do not define the wait macros, we use our own.  We can not detect
 * systems which falsely claim to be posix and incorrectly define the wait
 * macros as taking a |union wait| parameter.  The workaround for such systems
 * is to change "#ifdef _POSIX_VERSION" to "#if 0" below.
 */

#include <sys/types.h>
#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#else
# undef WIFEXITED
# undef WEXITSTATUS
#endif
#ifdef sequent          /* the wait macros are known to be broken on Dynix */
# undef WIFEXITED
# undef WEXITSTATUS
#endif

/* These macros can take an |int| or |union wait| parameter, but the posix
 * macros are preferred because these require specific knowledge of the
 * bit layout, which may not be correct on some systems (although most
 * unix-like systems do use this layout).
 */
#ifndef WIFEXITED
# define WIFEXITED(w)  (((*(int *)&(w)) & 0xFF) == 0)   /* works most places */
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(w)  (((*(int *)&(w)) >> 8) & 0xFF) /* works most places */
#endif

typedef RETSIGTYPE (SigHandler)(int sig);

#if !HAVE_RAISE
# if HAVE_KILL
#  define raise(sig) kill(getpid(), sig)
# endif
#endif

#ifdef SIGABRT
# define ABORT SIGABRT
#else
# ifdef SIGQUIT
#  define ABORT SIGQUIT
# else
#  define ABORT SIGTERM
# endif
#endif

/* Zero out undefined signals, so we don't have to #ifdef everything later. */
#ifndef SIGHUP
# define SIGHUP 0
#endif
#ifndef SIGTRAP
# define SIGTRAP 0
#endif
#ifndef SIGABRT
# define SIGABRT 0
#endif
#ifndef SIGBUS /* not defined in Linux */
# define SIGBUS 0
#endif
#ifndef SIGPIPE
# define SIGPIPE 0
#endif
#ifndef SIGUSR1
# define SIGUSR1 0
#endif
#ifndef SIGUSR2
# define SIGUSR2 0
#endif
#ifndef SIGTSTP
# define SIGTSTP 0
#endif
#ifndef SIGWINCH
# define SIGWINCH 0
#endif

#ifndef NSIG
  /* Find an upper bound of the signals we use */
# define NSIG \
   ((SIGHUP  | SIGINT  | SIGQUIT | SIGILL  | SIGTRAP | SIGABRT | SIGFPE  | \
     SIGBUS  | SIGSEGV | SIGPIPE | SIGTERM | SIGUSR1 | SIGUSR2 | SIGTSTP | \
     SIGWINCH) + 1)
#endif

VEC_TYPEDEF(sig_set, (NSIG-1));
const int feature_core = 1 - DISABLE_CORE;

static int have_pending_signals = 0;
static sig_set pending_signals;
static RETSIGTYPE (*parent_tstp_handler)(int sig);

static void   handle_interrupt(void);
static void   terminate(int sig);
static void   coremsg(void);
static RETSIGTYPE core_handler(int sig);
static RETSIGTYPE signal_scheduler(int sig);
static RETSIGTYPE signal_jumper(int sig);
#ifndef SIG_IGN
static RETSIGTYPE SIG_IGN(int sig);
#endif


static SigHandler *old_sighup_handler;
static SigHandler *setsighandler(int sig, SigHandler *func);

static jmp_buf jumpenv;
static int fatal_signal = 0;

/* HAVE_SIGACTION doesn't mean we NEED_sigaction.  On some systems that have
 * it, struct sigaction will not get defined unless _POSIX_SOURCE or similar
 * is defined, so it's best to avoid it if we don't need it.
 */
#ifdef SA_RESTART
# define NEED_sigaction
#endif
#ifdef SA_ACK
# define NEED_sigaction
#endif

static SigHandler *setsighandler(int sig, SigHandler *func)
{
    if (!sig) return NULL;
#ifndef NEED_sigaction
    return signal(sig, func);
#else
    {
        struct sigaction act;
        SigHandler *oldfunc;

        sigaction(sig, NULL, &act);
        oldfunc = act.sa_handler;
# ifdef SA_RESTART
        /* Disable system call restarting, so select() is interruptable. */
        act.sa_flags &= ~SA_RESTART;
# endif
# ifdef SA_ACK
        /* Disable OS2 SA_ACK, so signals can be re-installed POSIX-style. */
        act.sa_flags &= ~SA_ACK;
# endif
        act.sa_handler = func;
        sigaction(sig, &act, NULL);
        return oldfunc;
    }
#endif /* HAVE_SIGACTION */
}

/* Returns s, unless s is NULL, accessing s would cause a SIGBUS or SIGSEGV,
 * or s is too long, in which case it returns another valid string describing
 * the problem. */
const char *checkstring(const char *s) {
    SigHandler *old_sigsegv_handler, *old_sigbus_handler;
    const char *p;

    if (!s) return "";
    fatal_signal = 0;

    old_sigsegv_handler = setsighandler(SIGSEGV, signal_jumper);
    old_sigbus_handler = setsighandler(SIGBUS, signal_jumper);

    if (setjmp(jumpenv)) {
	if (fatal_signal == SIGSEGV)
	    s = "(invalid string: segmentation violation)";
	else if (fatal_signal == SIGBUS)
	    s = "(invalid string: bus error)";
	else
	    s = "(invalid string)";
	goto exit;
    }

    for (p = s; *p; p++) {
	if (p - s > 255) {
	    s = "(invalid string: too long)";
	    break;
	}
    }
    
exit:
    setsighandler(SIGBUS, old_sigbus_handler);
    setsighandler(SIGSEGV, old_sigsegv_handler);
    return s;
}

void init_signals(void)
{
    VEC_ZERO(&pending_signals);
    have_pending_signals = 0;

    old_sighup_handler = setsighandler(SIGHUP  , signal_scheduler);
    setsighandler(SIGINT  , signal_scheduler);
    setsighandler(SIGQUIT , core_handler);
    setsighandler(SIGILL  , core_handler);
    setsighandler(SIGTRAP , core_handler);
    setsighandler(SIGABRT , core_handler);
    setsighandler(SIGFPE  , SIG_IGN);
    setsighandler(SIGBUS  , core_handler);
    setsighandler(SIGSEGV , core_handler);
    setsighandler(SIGPIPE , SIG_IGN);
    setsighandler(SIGTERM , signal_scheduler);
    setsighandler(SIGUSR1 , signal_scheduler);
    setsighandler(SIGUSR2 , signal_scheduler);
    parent_tstp_handler = setsighandler(SIGTSTP , signal_scheduler);
    setsighandler(SIGWINCH, signal_scheduler);

#if DISABLE_CORE
    {
	struct rlimit rlim;
	rlim.rlim_cur = rlim.rlim_max = 0;
	setrlimit(RLIMIT_CORE, &rlim);
    }
#endif
}

#ifndef SIG_IGN
static RETSIGTYPE SIG_IGN(int sig)
{
    setsighandler(sig, SIG_IGN);  /* restore handler (POSIX) */
}
#endif

static void handle_interrupt(void)
{
    int c;

    VEC_CLR(SIGINT, &pending_signals);
    /* so status line macros in setup_screen() aren't gratuitously killed */

    if (!interactive)
        die("Interrupt, exiting.", 0);
    reset_kbnum();
    fix_screen();
    puts("C) continue tf; X) exit; T) disable triggers; P) kill processes\r");
    fflush(stdout);
    c = igetchar();
    if (ucase(c) == 'X')
        die("Interrupt, exiting.", 0);
    if (ucase(c) == 'T') {
        set_var_by_id(VAR_borg, 0);
        oputs("% Cyborg triggers disabled.");
    } else if (ucase(c) == 'P') {
        kill_procs();
        oputs("% All processes killed.");
    }
    redraw();
}

int suspend(void)
{
#if SIGTSTP
    if (parent_tstp_handler == SIG_DFL) {      /* true for job-control shells */
        check_mail();
        fix_screen();
        reset_tty();
        raise(SIGSTOP);
        cbreak_noecho_mode();
        get_window_size();
        redraw();
        check_mail();
        return 1;
    }
#endif
    oputs("% Job control not supported.");
    return 0;
}


static RETSIGTYPE core_handler(int sig)
{
    setsighandler(sig, core_handler);  /* restore handler (POSIX) */

    if (sig == SIGQUIT) {
	if (interactive) {
	    fix_screen();
#if DISABLE_CORE
	    puts("SIGQUIT received.  Exit?  (y/n)\r");
#else
	    puts("SIGQUIT received.  Dump core and exit?  (y/n)\r");
#endif
	    fflush(stdout);
	    if (igetchar() != 'y') {
		redraw();
		return;
	    }
	}
        fputs("Abnormal termination - SIGQUIT\r\n", stderr);
    }
    setsighandler(sig, SIG_DFL);
    if (sig != SIGQUIT) {
        minimal_fix_screen();
        coremsg();
        fprintf(stderr, "> Abnormal termination - signal %d\r\n\n", sig);
        fputs("Please email the author with the error messages above, and describe\r\n", stderr);
	fputs("what you were doing at the time.\r\n", stderr);
#if DISABLE_CORE
        fputs("Also, if you can, reinstall tf with --enable-core, attempt to reproduce the\r\n", stderr);
	fputs("error, get a stack trace and send it to the author.\r\n", stderr);
#else /* cores are enabled */
        fputs("Also, if you can, include a stack trace in your email.\r\n",
            stderr);
# ifdef PLATFORM_UNIX
        fputs("To get a stack trace, do this:\r\n", stderr);
        fputs("cd src\r\n", stderr);
        fputs("script\r\n", stderr);
        fputs("gdb -q tf   ;# if gdb is unavailable, use 'dbx tf' instead.\r\n",
            stderr);
        fputs("run\r\n", stderr);
        fputs("(do whatever is needed to reproduce the core dump)\r\n", stderr);
        fputs("where\r\n", stderr);
        fputs("quit\r\n", stderr);
        fputs("exit\r\n", stderr);
        fputs("\r\n", stderr);
        fputs("Then include the \"typescript\" file in your email.\r\n", stderr);
        fputs("\n", stderr);
# endif /* PLATFORM_UNIX */
#endif /* DISABLE_CORE */
    }

    if (interactive) {
	close_all();
        fputs("\nPress any key.\r\n", stderr);
        fflush(stderr);
        igetchar();
    }
    reset_tty();

    raise(sig);
}

void crash(int internal, const char *fmt, const char *file, int line, long n)
{
    setsighandler(SIGQUIT, SIG_DFL);
    minimal_fix_screen();
    reset_tty();
    if (internal) coremsg();
    fprintf(stderr, "> %s:  %s, line %d\r\n",
        internal ? "Internal error" : "Aborting", file, line);
    fputs("> ", stderr);
    fprintf(stderr, fmt, n);
    fputs("\r\n\n", stderr);
    raise(SIGQUIT);
}

static void coremsg(void)
{
    fputs("\r\n\nPlease report the following message verbatim to hawkeye@tcp.com.\n", stderr);
    fputs("Also describe what you were doing in tf when this\r\n", stderr);
    fputs("occured, and whether you can repeat it.\r\n\n", stderr);
    fprintf(stderr, "> %s\r\n", version);
    if (*sysname) fprintf(stderr, "> %s\r\n", sysname);
    fprintf(stderr, "> %s\r\n", featurestr->data);
    fprintf(stderr,"> virtscreen=%ld, visual=%ld, expnonvis=%ld, "
	"emulation=%ld, lp=%ld, sub=%ld\r\n",
        virtscreen, visual, expnonvis, emulation, lpflag, sub);
#if SOCKS
    fprintf(stderr,"> SOCKS %d\r\n", SOCKS);
#endif
    fprintf(stderr,"> TERM=\"%.32s\"\r\n", TERM ? TERM : "(NULL)");
    fprintf(stderr,"> cmd=\"%.32s\"\r\n",
	current_command ? current_command : "(NULL)");
    if (loadfile) {
	fprintf(stderr,"> line %d-%d of file \"%.32s\"\r\n",
	    loadstart, loadline,
	    loadfile->name ? loadfile->name : "(NULL)");
    }
}

static void terminate(int sig)
{
    setsighandler(sig, SIG_DFL);
    fix_screen();
    reset_tty();
    fprintf(stderr, "Terminating - signal %d\r\n", sig);
    raise(sig);
}

static RETSIGTYPE signal_scheduler(int sig)
{
    setsighandler(sig, signal_scheduler);  /* restore handler (POSIX) */
    VEC_SET(sig, &pending_signals);        /* set flag to deal with it later */
    have_pending_signals++;
}

static RETSIGTYPE signal_jumper(int sig)
{
    fatal_signal = sig;
    longjmp(jumpenv, 1);
    /* don't need to restore handler */
}

void process_signals(void)
{
    if (!have_pending_signals) return;
    if (VEC_ISSET(SIGINT, &pending_signals))   handle_interrupt();
    if (VEC_ISSET(SIGTSTP, &pending_signals))  suspend();
    if (VEC_ISSET(SIGWINCH, &pending_signals))
        if (!get_window_size()) operror("TIOCGWINSZ ioctl");
    if (VEC_ISSET(SIGHUP, &pending_signals))   do_hook(H_SIGHUP, NULL, "");
    if (VEC_ISSET(SIGTERM, &pending_signals))  do_hook(H_SIGTERM, NULL, "");
    if (VEC_ISSET(SIGUSR1, &pending_signals))  do_hook(H_SIGUSR1, NULL, "");
    if (VEC_ISSET(SIGUSR2, &pending_signals))  do_hook(H_SIGUSR2, NULL, "");

    if (VEC_ISSET(SIGHUP, &pending_signals) && old_sighup_handler == SIG_DFL)
	terminate(SIGHUP);
    if (VEC_ISSET(SIGTERM, &pending_signals))
	terminate(SIGTERM);

    have_pending_signals = 0;
    VEC_ZERO(&pending_signals);
}

int interrupted(void)
{
    return VEC_ISSET(SIGINT, &pending_signals);
}

int shell_status(int result)
{
    /* If the next line causes errors like "request for member `w_S' in
     * something not a structure or union", then <sys/wait.h> must have
     * defined WIFEXITED and WEXITSTATUS incorrectly (violating Posix.1).
     * The workaround is to not #include <sys/wait.h> at the top of this
     * file, so we can use our own definitions.
     */
    return (WIFEXITED(result)) ? WEXITSTATUS(result) : -1;
}

int shell(const char *cmd)
{
    int result;

    check_mail();
    fix_screen();
    reset_tty();
    setsighandler(SIGTSTP, parent_tstp_handler);
    result = system(cmd);
    setsighandler(SIGTSTP, signal_scheduler);
    cbreak_noecho_mode();
    if (result == -1) {
        eprintf("%s", strerror(errno));
    } else if (shpause && interactive) {
        puts("\r\n% Press any key to continue tf.\r");
        igetchar();
    }
    get_window_size();
    redraw();
    if (result == -1) return result;
    check_mail();
#ifdef PLATFORM_OS2
    return result;
#else /* UNIX */
    return shell_status(result);
#endif
}
