# $Id: Makefile,v 33000.2 1994/04/01 01:29:35 hawkeye Exp $
########################################################################
#  TinyFugue - programmable mud client
#  Copyright (C) 1994 Ken Keys
#
#  TinyFugue (aka "tf") is protected under the terms of the GNU
#  General Public Licence.  See the file "COPYING" for details.
#
#  DO NOT EDIT THIS FILE.  To change the default configuration,
#  see "README" and edit "Config".
#
########################################################################

#
# Top level Makefile.
#

TFVER = 33b6
SHELL = /bin/sh
MAKE  = make

install all files: src/Makefile src/config.h _log
	cd ./src; { $(MAKE) $@ 2>&1; cat exitmsg; } | tee -a ../Build.log

reconfigure:
	rm src/Makefile src/config.h
	$(MAKE) install

src/Makefile src/config.h: Makefile Config src/autoconfig src/mf.tail
	@rm -f src/Makefile src/config.h Build.log src/autoconfig.log
#	Note: tee eats exit code of autoconfig.  src/config.h will always
#	be created, but src/Makefile will exist only if autoconfig succeeded.
#	Note: bash is buggy, can't handle using descriptor 3 below.
	@cd ./src; ./autoconfig $(TFVER) 2>&1 4>config.h 5>mf.vars | \
	    tee autoconfig.log
#	Note: autoconfig doesn't return a useful exit value, so we must
#	verify that the files were really built.
	@test -r src/Makefile && test -r src/config.h

_log:
	@{ cat src/autoconfig.log; echo; } > Build.log
	@{ cat src/mf.vars; echo; } >> Build.log
	@{ cat src/config.h; echo; } >> Build.log

clean:
	rm -f core* *.log
	cd ./src; rm -f *.o Makefile core* exitmsg config.h
	cd ./src; rm -f *.log libtest.* test.c a.out libc.list mf.vars
	cd ./src/regexp; make clean

distclean:  clean
	cd ./src; rm -f tf tf.connect makehelp tags
	cd ./src; rm -f tf.pixie* tf.Addrs* tf.Counts*

spotless cleanest veryclean:  distclean
	cd ./src; rm -f tf.1.catman tf.help.index

srcdist:
	cd ./src; $(MAKE) -f mf.tail dist

dist: srcdist distclean
	@wd=`pwd`; [ "`basename $$wd`" = "work" ] || \
	    echo 'WARNING: You are not in the "work" directory.'
	@echo 'Press return to archive tf.$(TFVER).'
	read foo
	rm -rf ../tf.$(TFVER)
	mkdir ../tf.$(TFVER)
	mkdir ../tf.$(TFVER)/src
	mkdir ../tf.$(TFVER)/src/regexp
	mkdir ../tf.$(TFVER)/tf.lib
	-cp * ../tf.$(TFVER)
	-cp src/* ../tf.$(TFVER)/src
	-cp src/regexp/* ../tf.$(TFVER)/src/regexp
	-cp tf.lib/* ../tf.$(TFVER)/tf.lib
	chmod ugo+r ../tf.$(TFVER)
	chmod ugo+r ../tf.$(TFVER)/*
	chmod ugo+r ../tf.$(TFVER)/src/*
	chmod ugo+r ../tf.$(TFVER)/tf.lib/*
	rm -f ../tf.$(TFVER)/src/dmalloc.c
	cd ..; tar -cf tf.$(TFVER).tar tf.$(TFVER)
	cd ..; gzip -c tf.$(TFVER).tar > tf.$(TFVER).tar.gz
	cd ..; compress tf.$(TFVER).tar
	cd ..; chmod ugo+r tf.$(TFVER).tar.gz tf.$(TFVER).tar.Z

