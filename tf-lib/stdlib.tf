;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; TinyFugue - programmable mud client
;;;; Copyright (C) 1994 Ken Keys
;;;;
;;;; TinyFugue (aka "tf") is protected under the terms of the GNU
;;;; General Public License.  See the file "COPYING" for details.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; $Id: stdlib.tf,v 35000.23 1997/11/19 07:27:13 hawkeye Exp $

;;; TF macro library

;;; DO NOT EDIT THIS FILE.
;;;
;;; Personal commands should be performed in %HOME/.tfrc; public commands
;;; for all users should be performed in %TFLIBDIR/local.tf.  This file will
;;; be replaced when tf is upgraded; %HOME/.tfrc and %TFLIBDIR/local.tf will
;;; not, so changes you make to them will not be lost.

;;; This library is essential to the correct operation of TF.  It is as
;;; much a part of TF as the executable file.  It is designed so that
;;; it can be used by one or many users.

;;; Many "hidden" macros here are named starting with "~" to minimize
;;; conflicts with the user's namespace; you should not give your own
;;; macros names beginning with "~".  Also, you probably don't want to
;;; use the -i flag in defining your personal macros, although you can.


;;; visual status bar

/set status_fields \
    @more:8:Br :1 @world :1 \
    @read:6 :1 @active:11 :1 @log:5 :1 @mail:6 :1 insert:6 :1 @clock:5

/set status_int_more \
     moresize() == 0 ? "" : \
     moresize() > 9999 ? "MuchMore" : \
     pad("More", 0, moresize(), 4)
/set status_int_world   ${world_name}
/set status_int_read    nread() ? "(Read)" : ""
/set status_int_active  nactive() ? pad("(Active:", 0, nactive(), 2, ")") : ""
/set status_int_log     nlog() ? "(Log)" : ""
/set status_int_mail    nmail() ? "(Mail)" : ""
/set status_var_insert  insert ? "" : "(Over)"
/set status_int_clock   ftime("%I:%M", time())


;;; file compression

/if ( systype() =~ "unix" ) \
    /def -i COMPRESS_SUFFIX = .Z%;\
    /def -i COMPRESS_READ = zcat%;\
/elseif ( systype() =~ "os/2" ) \
    /def -i COMPRESS_SUFFIX = .zip%;\
    /def -i COMPRESS_READ = unzip -p%;\
/endif

;;; Help for newbies
/def -i -h'SEND help' -Fq send_help = \
    /if (${world_name} =~ "") \
        /echo -e %% You are not connected to a world.%; \
        /echo -e %% Use "/help" to get help on TinyFugue.%; \
    /endif

;;; High priority for library hooks/triggers.  This is a hack.
/set maxpri=2147483647


;;; Commands
;; Some of these use helper macros starting with ~ to reduce conflicts with
;; the user's namespace.

;;; /sys <command>
; Executes an "inline" shell command.
; Only works for commands that do not require input or redraw the screen.

/def -i sys = /quote -S -decho \\!!%{*-:}


;;; /send [-nW] [-T<type>] [-w<world>] text
/def -i send = \
    /if (!getopts("nWT:w:", "")) /break%; /endif%; \
    /let text=%{*}%; \
    /if (opt_W) \
        /~send $(/listsockets -s)%; \
    /elseif (opt_T !~ "") \
        /~send $(/listsockets -s -T%{opt_T})%; \
    /else \
        /@test send(text, {opt_w-${world_name}}, !opt_n)%; \
    /endif

/def -i ~send = \
    /while ({#}) \
        /@test send(text, {1}, !opt_n)%; \
        /shift%; \
    /done


;; null commands
/def -i :	= /@test 1
/def -i true	= /@test 1
/def -i false	= /@test 0

;;

/def -i bg = /fg -n

;; /world [-nlq] [<name>]
;; /world [-nlq] <host> <port>

/def -i world = \
    /let args=%*%; \
    /if (args =~ "") \
        /let args=$(/nth 1 $(/listworlds -s))%; \
        /if (args =/ "default") \
            /let args=$(/nth 2 $(/listworlds -s))%; \
        /endif%; \
    /endif%; \
    /if /!@fg -s %args%; \
    /then /@connect %args%; \
    /endif


;; /purgeworld <name>...
/def -i purgeworld = /unworld $(/listworlds -s %*)


;; for loop.
; syntax:  /for <var> <start> <end> <command>

/def -i for	= \
    /@eval \
        /let %1=%2%%; \
        /while ( %1 <= %3 ) \
            %-3%%; \
            /@test ++%1%%; \
        /done

;; flag toggler.
/def -i toggle	= /@test %1 := !%1

;; result negation.
/def -i not	= /@eval %*%; /@test !%?

;; expression evaluator.
/def -i expr	= /@test echo((%*))

;; replace text in input buffer.
/def -i grab	= /@test kblen() & dokey("dline")%; /input %*

;; partial hilites.
/def -i partial = /def -F -p%{hpri-0} -Ph -t"$(/escape " %*)"

;; triggers.
/def -i trig	= /trigpc 0 100 %*
/def -i trigc	= /trigpc 0 %{1-x} %-1
/def -i trigp	= /trigpc %{1-x} 100 %-1

/def -i undeft	= /untrig -anGgurfdhbBC0 - %*

/def -i nogag = \
    /if ({#}) \
        /untrig -ag - %*%;\
    /else \
        /set gag=0%;\
    /endif

/def -i nohilite = \
    /if ({#}) \
        /untrig -aurfdhbBC0 - %*%;\
    /else \
        /set hilite=0%;\
    /endif

;; macro existance test.
/def -i ismacro = /list -s -i %{*-@} %| /return %?


;; cut-and-paste tool
/def -i paste = \
    /echo -e %% Entering paste mode.  Type "/endpaste" to end.%; \
    /let line=%; \
    /while ((line:=read()) !/ "/endpaste") \
        /if (line =/ "/quit" | line =/ "/help*") \
            /echo -e %% Type "/endpaste" to end /paste.%; \
        /endif%; \
        /send - %{*-%{paste_prefix-:|}} %{line}%; \
    /done

;; other useful stuff.

/def -i first	= /echo - %1
/def -i rest	= /echo - %-1
/def -i last	= /echo - %L
/def -i nth	= /shift %1%; /echo - %1

/def -i cd	= /lcd %{*-%HOME}
/def -i pwd	= /last $(/lcd)

/def -i man	= /help %*

/def -i signal	= /quote -S -decho !kill -%{1-l} $[getpid()]

/def -i split	= /@test regmatch("^([^=]*[^ =])? *=? *(.*)", {*})

/def -i ver	= \
    /@test regmatch('version (.*). % Copyright', $$(/version))%; \
    /echo - %P1


;;; Extended world definition macros

/def -i addtiny		= /addworld -T"tiny"	%*
/def -i addlp		= /addworld -T"lp"	%*
/def -i addlpp		= /addworld -T"lpp"	%*
/def -i adddiku		= /addworld -T"diku"	%*
/def -i addtelnet	= /addworld -T"telnet"	%*


;; Auto-switch connect hook
/def -iFp0 -agG -hCONNECT ~connect_switch_hook = /@fg %1

;; Proxy server connect hook
/eval /def -iFp%{maxpri} -agG -hPROXY proxy_hook = /proxy_command

/def -i proxy_command = \
    telnet ${world_host} ${world_port}%; \
    /trigger -hCONNECT ${world_name}%; \
    /if (${world_character} !~ "") \
        /trigger -hLOGIN ${world_name}%; \
    /endif

;; Heuristics to detect worlds that use prompts, but have not been classified
;; as such by the user's /addworld definition.

/def -iFmregexp -h'PROMPT [Ll]ogin:( *)$' -T'^$' ~detect_worldtype_1 = \
    /@test prompt(strcat({*}, P1))%;\
    /addworld -Ttelnet ${world_name}%;\
    /set lp=1%;\
    /localecho on%;\
    /echo %% This looks like a telnet world, so I'm redefining it as one.%;\
    /echo %% You should use /addworld -T to explicitly set it yourself.

/def -iFmregexp -h'PROMPT ^By what name .* be known\\? *$' -T^$ \
  ~detect_worldtype_2 = \
    /@test prompt(P0)%; \
    /addworld -Tlp ${world_name}%; \
    /set lp=1%; \
    /echo %% This looks like an lp-prompt world, so I'm redefining it as one.%;\
    /echo %% You should use /addworld -T to explicitly set it yourself.


;; Default worldtype hook: tiny login format (for backward compatibility),
;; but do not change any flags.
/eval \
	/def -mglob -T{} -hLOGIN -iFp%{maxpri} ~default_login_hook = \
		/send connect $${world_character} $${world_password}

;; Tiny hooks: login format, lp=off.
/eval \
	/def -mglob -T{tiny|tiny.*} -hWORLD -iFp%{maxpri} ~world_hook_tiny = \
		/set lp=0%; \
	/def -mglob -T{tiny|tiny.*} -hLOGIN -iFp%{maxpri} ~login_hook_tiny = \
		/send connect $${world_character} $${world_password}

;; Generic prompt-world hooks: lp=on.
/eval \
    /def -mglob -Tprompt -hWORLD -iFp%{maxpri} ~world_hook_prompt = \
        /set lp=1

;; LP/Diku/Aber/etc. hooks: login format, lp=on.
/eval \
    /def -mglob -T{lp|lp.*|diku|diku.*|aber|aber.*} -hWORLD -iFp%{maxpri} \
    ~world_hook_lp = \
        /set lp=1%; \
    /def -mglob -T{lp|lp.*|diku|diku.*|aber|aber.*} -hLOGIN -iFp%{maxpri} \
    ~login_hook_lp = \
        /send -- $${world_character}%%; \
        /send -- $${world_password}

;; Hooks for LP-worlds with telnet end-of-prompt markers:
;; login format, lp=off.
/eval \
    /def -mglob -T{lpp|lpp.*} -hWORLD -iFp%{maxpri} ~world_hook_lpp = \
        /set lp=0%; \
    /def -mglob -T{lpp|lpp.*} -hLOGIN -iFp%{maxpri} ~login_hook_lpp = \
        /send -- $${world_character}%%; \
        /send -- $${world_password}

;; Telnet hooks: login format, lp=on, and localecho=on (except at
;; password prompt).
/eval \
    /def -mglob -T{telnet|telnet.*} -hCONNECT -iFp%{maxpri} ~con_hook_telnet = \
	/def -w -qhPROMPT -n1 -iFp%{maxpri} = /localecho on%;\
    /def -mglob -T{telnet|telnet.*} -hWORLD -iFp%{maxpri} ~world_hook_telnet = \
	/set lp=1%; \
    /def -mglob -T{telnet|telnet.*} -hLOGIN -iFp%{maxpri} ~login_hook_telnet = \
	/send -- $${world_character}%%; \
	/send -- $${world_password}%; \
    /def -mregexp -T'^telnet(\\\\..*)?$$' -h'PROMPT [Pp]assword:( *)$$' \
    -iFp%{maxpri} ~telnet_passwd = \
	/@test prompt(strcat({*}, P1))%%;\
	/def -w -q -hSEND -iFn1p%{maxpri} ~echo_$${world_name} =\
	    /localecho on%%;\
	/localecho off

;; /telnet <host> [<port>]
;; Defines a telnet-world and connects to it.
/def -i telnet = \
	/addtelnet %{1},%{2-23} %1 %{2-23}%; \
	/connect %{1},%{2-23}


;;; default filenames
; This is ugly, mainly to keep backward compatibility with the lame old
; "~/tiny.*" filenames and *FILE macros.  The new style, "~/*.tf", has
; a sensible suffix, and works on 8.3 FAT filesystems.  (A subdirectory
; would be nice, but then /save* would fail if the user hasn't created
; the subdirectory).

/if ( TINYPREFIX =~ "" & TINYSUFFIX =~ "" ) \
;   New-style names make more sense.
    /set TINYPREFIX=~/%; \
    /set TINYSUFFIX=.tf%; \
;   Old-style names on unix systems, for backward compatibility.
    /if ( systype() =~ "unix" ) \
        /set TINYPREFIX=~/tiny.%; \
        /set TINYSUFFIX=%; \
    /endif%; \
/endif

/eval /def -i MACROFILE		= %{TINYPREFIX}macros%{TINYSUFFIX}
/eval /def -i HILITEFILE	= %{TINYPREFIX}hilite%{TINYSUFFIX}
/eval /def -i GAGFILE		= %{TINYPREFIX}gag%{TINYSUFFIX}
/eval /def -i TRIGFILE		= %{TINYPREFIX}trig%{TINYSUFFIX}
/eval /def -i BINDFILE		= %{TINYPREFIX}bind%{TINYSUFFIX}
/eval /def -i HOOKFILE		= %{TINYPREFIX}hook%{TINYSUFFIX}
/eval /def -i WORLDFILE		= %{TINYPREFIX}world%{TINYSUFFIX}
/eval /def -i LOGFILE		= tiny.log


;;; define load* and save* macros with default filenames.

/def -i ~def_file_command = \
    /def -i %1%2	= \
        /%1 %%{1-$${%{3}FILE}} %{-3}

/~def_file_command  load  def     MACRO
/~def_file_command  load  hilite  HILITE
/~def_file_command  load  gag     GAG
/~def_file_command  load  trig    TRIG
/~def_file_command  load  bind    BIND
/~def_file_command  load  hook    HOOK
/~def_file_command  load  world   WORLD

/~def_file_command  save  def	MACRO   -mglob -h0 -b{} -t{} ?*
/~def_file_command  save  gag	GAG     -mglob -h0 -b{} -t -ag
/~def_file_command  save  trig	TRIG    -mglob -h0 -b{} -t -an
/~def_file_command  save  bind	BIND    -mglob -h0 -b
/~def_file_command  save  hook	HOOK    -mglob -h

/def -i savehilite = \
    /save %{1-${HILITEFILE}} -mglob -h0 -b{} -t -aurfdhbBC0%;\
    /save -a %{1-${HILITEFILE}} -mglob -h0 -b{} -t -P


;;; list macros

/def -i listdef		= /list %*
/def -i listfullhilite	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -aurfdhbBC0
/def -i listpartial	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -P
/def -i listhilite	= /listfullhilite%; /listpartial
/def -i listgag		= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -ag
/def -i listtrig	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -an
/def -i listbind	= /list -mglob -h0 -b'$(/escape ' %*)'
/def -i listhook	= /list -mglob -h'$(/escape ' %*)'


;;; purge macros

/def -i purgedef	= /purge -mglob -h0 -b{} - %{1-?*}
/def -i purgehilite	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -aurfdhbBC0
/def -i purgegag	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -ag
/def -i purgetrig	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -an
/def -i purgedeft	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' ?*
/def -i purgebind	= /purge -mglob -h0 -b'$(/escape ' %*)'
/def -i purgehook	= /purge -mglob -h'$(/escape ' %*)'


;; library loading

/set _loaded_libs=

/def -i ~loaded = \
    /if /@test _loaded_libs !/ "*{%{1}}*"%; /then \
        /set _loaded_libs=%{_loaded_libs} %{1}%;\
    /endif

/def -i require = \
    /if /@test _loaded_libs !/ "*{%{L}}*"%; /then \
        /load %{-L} %{TFLIBDIR}/%{L}%;\
    /endif

;; meta-character quoter
;; /escape <metachars> <string>
/def -i escape = \
    /let meta=$[strcat({1}, "\\")]%;\
    /let dest=%;\
    /let tail=%-1%;\
    /let i=garbage%;\
    /while ((i := strchr(tail, meta)) >= 0) \
        /let dest=$[strcat(dest, substr(tail,0,i), "\\", substr(tail,i,1))]%;\
        /let tail=$[substr(tail, i+1)]%;\
    /done%;\
    /echo -- %{dest}%{tail}


;;; /loadhist [-lig] [-w<world>] file

/def -i loadhist = \
    /let file=%L%; \
    /quote -S /recordline %-L '%%{file-${LOGFILE}}

;;; /keys simulation
;; For backward compatibilty only.
;; Supports '/keys <mnem> = <key>' and '/keys' syntax.

/def -i keys =\
    /if ( {*} =/ "" ) \
        /list -Ib%;\
    /elseif ( {*} =/ "*,*" ) \
        /echo -e %% The /keys comma syntax is no longer supported.%;\
        /echo -e %% See /help bind, /help dokey.%;\
    /elseif ( {*} =/ "{*} = ?*" ) \
        /def -ib'%{-2}' = /dokey %1%;\
    /elseif ( {*} =/ "*=*" ) \
        /echo -e %% '=' must be surrounded by spaces.%;\
        /echo -e %% See /help bind, /help dokey.%;\
    /else \
        /echo -e %% Bad /keys syntax.%;\
    /endif


;;; Retry connections

;; /retry <world> [<delay>]
;; Try to connect to <world>.  Repeat every <delay> seconds (default 60)
;; until successful.

/def -i retry = \
    /def -mglob -p%{maxpri} -F -h'CONFAIL $(/escape ' %1) *' ~retry_fail_%1 =\
        /repeat -%{2-60} 1 /connect %1%;\
    /def -mglob -1 -p%{maxpri} -F -h'CONNECT $(/escape ' %1)' ~retry_succ_%1=\
        /undef ~retry_fail_%1%;\
    /connect %1

;; /retry_off [<world>]
;; Cancels "/retry <world>" (default: all worlds)

/def -i retry_off = /purge -mglob {~retry_fail_%{1-*}|~retry_succ_%{1-*}}


;;; Hilites for pages and whispers
;; Simulates "/hilite page" and "/hilite whisper" in old versions.

/def -i hilite_whisper	= \
  /def -ip2ah -mregexp -t'^[^ ]* whispers,? ".*" (to [^ ]*)?$$' ~hilite_whisper1

/def -i hilite_page	= \
  /def -ip2ah -mglob -t'{*} pages from *[,:] *' ~hilite_page1%;\
  /def -ip2ah -mglob -t'You sense that {*} is looking for you in *' ~hilite_page2%;\
  /def -ip2ah -mglob -t'The message was: *' ~hilite_page3%;\
  /def -ip2ah -mglob -t'{*} pages[,:] *' ~hilite_page4%;\
  /def -ip2ah -mglob -t'In a page-pose*' ~hilite_page5

/def -i nohilite_whisper	= /purge -mglob -I ~hilite_whisper[1-9]
/def -i nohilite_page		= /purge -mglob -I ~hilite_page[1-9]


;;; backward compatible commands

/def -i cat = \
    /echo -e %% Entering cat mode.  Type "." to end.%; \
    /let line=%; \
    /let all=%; \
    /while ((line:=read()) !~ ".") \
        /if (line =/ "/quit") \
            /echo -e %% Type "." to end /cat.%; \
        /endif%; \
        /@test all := \
            strcat(all, (({1} =~ "%%" & all !~ "") ? "%%;" : ""), line)%; \
    /done%; \
    /recordline -i %all%; \
    /@test eval(all)

/def -i time = /@test echo(ftime({*-%%{time_format}}, time())), time()

/def -i rand = \
    /if ( {#} == 0 ) /echo $[rand()]%;\
    /elseif ( {#} == 1 ) /echo $[rand({1})]%;\
    /elseif ( {#} == 2 ) /echo $[rand({1}, {2})]%;\
    /else /echo -e %% %0: too many arguments%;\
    /endif

; Since the default page key (TAB) is not obvious to a new user, we display
; instructions when he executes "/more on" if he hasn't re-bound the key.
/def -i more = \
    /if ( {*} =/ "{on|1}" & ismacro("-ib'^I' = /dokey page") ) \
        /echo -e %% "More" paging enabled.  Use TAB to scroll.%;\
    /endif%; \
    /set more %*

/def -i nolog		= /log off
/def -i nowrap		= /set wrap off
/def -i nologin		= /set login off
/def -i noquiet		= /set quiet off

/def -i act		= /trig %*
/def -i reply		= /set borg %*

/def -i background	= /set background %*
/def -i bamf		= /set bamf %*
/def -i borg		= /set borg %*
/def -i clearfull	= /set clearfull %*
/def -i cleardone	= /set cleardone %*
/def -i insert		= /set insert %*
/def -i login		= /set login %*
/def -i lp		= /set lp %*
/def -i lpquote		= /set lpquote %*
/def -i quiet		= /set quiet %*
/def -i quitdone	= /set quitdone %*
/def -i redef		= /set redef %*
/def -i shpause		= /set shpause %*
/def -i sockmload	= /set sockmload %*
/def -i sub		= /set sub %*
/def -i visual		= /set visual %*

/def -i gpri		= /set gpri %*
/def -i hpri		= /set hpri %*
/def -i isize		= /set isize %*
/def -i ptime		= /set ptime %*
/def -i wrapspace	= /set wrapspace %*

/def -i wrap = \
    /if ({*} > 1) \
        /set wrapsize=%*%; \
        /set wrap=1%; \
    /else \
        /set wrap %*%;\
    /endif

/def -i ~do_prefix = \
    /if ( {-1} =/ "{|off|0|on|1}" ) \
        /set %{1}echo %{-1}%; \
    /elseif ( {-1} =/ "{all|2}" & {1} =~ "m" ) \
        /set %{1}echo %{-1}%; \
    /else \
        /set %{1}prefix=%{-1}%; \
        /set %{1}prefix%; \
        /set %{1}echo=1%; \
    /endif

/def -i kecho = /~do_prefix k %*
/def -i mecho = /~do_prefix m %*
/def -i qecho = /~do_prefix q %*


;;; Other standard libraries

/def -hload -ag ~gagload
/eval /load %TFLIBDIR/kbbind.tf
/eval /if (systype() =~ "os/2") /load %TFLIBDIR/kb-os2.tf%; /endif
/eval /load %TFLIBDIR/color.tf
/eval /load %TFLIBDIR/changes.tf
/undef ~gagload


;;; constants

/set pi=3.141592654
/set e=2.718281828


;;; Load local public config file

/def -hloadfail -ag ~gagloadfail
/eval /load %{TFLIBDIR}/local.tf
/undef ~gagloadfail

