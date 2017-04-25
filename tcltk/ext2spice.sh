#!/bin/sh
#
# Standalone script for ext2spice, for Tcl-based magic 8.0
#
# Parse arguments. "--" separates arguments to magic
# from arguments to ext2spice.
#
mgargs=""
esargs=""
for i in $@; do
   case $i in
      --) mgargs="$esargs"
	  esargs="" ;;
      *) esargs="$esargs $i" ;;
   esac
done
#
eval /usr/local/lib/magic/tcl/magicdnull -dnull -noconsole -nowrapper $mgargs <<EOF
drc off
box 0 0 0 0
ext2spice $esargs
quit -noprompt
EOF
