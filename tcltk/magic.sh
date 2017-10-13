#!/bin/bash
#
# For installation, put this file (magic.sh) in a known executable path.
# Put startup script "magic.tcl", shared library "tclmagic.so", and
# "wish" replacement "magicexec" in ${CAD_ROOT}/magic/tcl/.
#
# This script starts magic under the Tcl interpreter,
# reading commands from a special startup script which
# launches magic and retains the Tcl interactive interpreter.

# Parse for the argument "-c[onsole]".  If it exists, run magic
# with the TkCon console.  Strip this argument from the argument list.

TKCON=true
DNULL=
MAGIC_WISH=/usr/bin/wish
export MAGIC_WISH

# Hacks for Cygwin
if [ "`uname | cut -d_ -f1`" = "CYGWIN" ]; then
   export PATH="$PATH:/usr/lib"
   export DISPLAY=${DISPLAY:=":0"}
fi

# Preserve quotes in arguments
arglist=''
for i in "$@" ; do
   case $i in
      -noc*) TKCON=;;
      -dnull) DNULL=true;;
      --version) TKCON=; DNULL=true;;
      --prefix) TKCON=; DNULL=true;;
      *) arglist="$arglist${arglist:+ }\"${i//\"/\\\"}\"";;
   esac
done

if [ $TKCON ]; then

   if [ $DNULL ]; then
      exec /usr/local/lib/magic/tcl/tkcon.tcl -eval "source /usr/local/lib/magic/tcl/console.tcl" \
	   -slave "set argc $#; set argv [list $*]; source /usr/local/lib/magic/tcl/magic.tcl"
   else
      exec /usr/local/lib/magic/tcl/tkcon.tcl -eval "source /usr/local/lib/magic/tcl/console.tcl" \
	   -slave "package require Tk; set argc $#; set argv [list $arglist]; \
	   source /usr/local/lib/magic/tcl/magic.tcl"
   fi

else

#
# Run the stand-in for wish (magicexec), which acts exactly like "wish"
# except that it replaces ~/.wishrc with magic.tcl.  This executable is
# *only* needed when running without the console; the console itself is
# capable of sourcing the startup script.
#
# With option "-dnull" we set up for operation without Tk (simple interpreter
# only, efficient for running in batch mode).
#
   if [ $DNULL ]; then
      exec /usr/local/lib/magic/tcl/magicdnull -nowrapper "$@"
   else
      exec /usr/local/lib/magic/tcl/magicexec -- "$@"
   fi
fi
