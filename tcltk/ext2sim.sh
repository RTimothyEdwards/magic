#!/bin/sh
#
# Standalone script for ext2sim, for Tcl-based magic 8.0
#
# Parse arguments. "--" separates arguments to magic
# from arguments to ext2sim.
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
eval /home/tim/cad/lib/magic/tcl/magicdnull -dnull -noconsole -nowrapper $mgargs <<EOF
drc off
box 0 0 0 0
ext2sim $esargs
quit -noprompt
EOF
