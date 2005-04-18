# $Id: vars.mak,v 1.54 2005/04/18 03:15:36 kkeys Exp $
########################################################################
#  TinyFugue - programmable mud client
#  Copyright (C) 1998, 1999, 2002, 2003, 2004, 2005 Ken Keys
#
#  TinyFugue (aka "tf") is protected under the terms of the GNU
#  General Public License.  See the file "COPYING" for details.
#
#  DO NOT EDIT THIS FILE.
#  For instructions on changing configuration, see the README file in the
#  directory for your operating system.
########################################################################

# Makefile variables common to all systems.
# This file should be included or concatenated into a system Makefile.
# Predefined variables:
#   O - object file suffix (e.g., "o" or "obj")

TFVER=50b7

SOURCE = attr.c command.c dstring.c expand.c expr.c help.c history.c \
  keyboard.c macro.c main.c malloc.c output.c process.c search.c \
  signals.c socket.c tfio.c tty.c util.c variable.c world.c

OBJS = attr.$O command.$O dstring.$O expand.$O expr.$O help.$O history.$O \
  keyboard.$O macro.$O main.$O malloc.$O output.$O pattern.$O process.$O \
  search.$O signals.$O socket.$O tfio.$O tty.$O util.$O variable.$O world.$O \
  $(OTHER_OBJS)

