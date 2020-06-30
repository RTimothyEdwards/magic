#-----------------------------------------------------------------
# readspice.tcl
#-----------------------------------------------------------------
# Defines procedure "readspice <netlist>", requiring a SPICE
# netlist as an argument.  Each .SUBCKT line in the netlist
# is used to force the port ordering in the layout of the cell
# with the same subcircuit name.
#
# NOTE:  This is NOT a schematic-to-layout function!  Its purpose
# is to annotate cells with information in a netlist.  The cell
# layout must exist before reading the corresponding netlist.
#-----------------------------------------------------------------

global Opts

proc readspice {netfile} {
   if {[file ext $netfile] == ".cdl"} {
      set is_cdl true
   } else {
      set is_cdl false
   }

   if [catch {open  $netfile r} fnet] {
      set netname [file rootname $netfile]

      # Check for standard extensions (.spi, .spc, .spice, .sp, .ckt, .cdl)

      set testnetfile ${netname}.spi
      if [catch {open  ${testnetfile} r} fnet] {
         set testnetfile ${netname}.spc
         if [catch {open  ${testnetfile} r} fnet] {
            set testnetfile ${netname}.spice
            if [catch {open  ${testnetfile} r} fnet] {
               set testnetfile ${netname}.sp
               if [catch {open  ${testnetfile} r} fnet] {
                  set testnetfile ${netname}.ckt
                  if [catch {open  ${testnetfile} r} fnet] {
                     set testnetfile ${netname}.cdl
                     if [catch {open  ${testnetfile} r} fnet] {
                        puts stderr "Error:  Can't read netlist file $netfile"
	                return 1;
		     } else {
			set is_cdl true
		     }
		  }
	       }
	    }
	 }
      }
      set netfile $testnetfile
   }

   # Read data from file.  Remove comment lines and concatenate
   # continuation lines.

   puts stderr "Annotating port orders from $netfile"
   set fdata {}
   set lastline ""
   while {[gets $fnet line] >= 0} {
       # Handle CDL format *.PININFO (convert to .PININFO ...)
       if {$is_cdl && ([string range $line 0 1] == "*.")} {
	   if {[string tolower [string range $line 2 8]] == "pininfo"} {
	       set line [string range $line 1 end]
	   }
       }
       if {[string index $line 0] != "*"} {
           if {[string index $line 0] == "+"} {
               if {[string range $line end end] != " "} {
                  append lastline " "
	       }
               append lastline [string range $line 1 end]
	   } else {
	       lappend fdata $lastline
               set lastline $line
	   }
       }
   }
   lappend fdata $lastline
   close $fnet

   # Now look for all ".subckt" lines

   set cell ""
   set status 0

   suspendall
   foreach line $fdata {
       set ftokens [split $line]
       set keyword [string tolower [lindex $ftokens 0]]

       # Handle SPECTRE model format
       if {$keyword == "inline"} {
	   if {[string tolower [lindex $ftokens 1]] == "subckt"} {
	       set ftokens [lrange [split $line " \t()"] 1 end]
	       set keyword ".subckt"
	   }
       }

       if {$keyword == ".subckt"} {
	   set cell [lindex $ftokens 1]
	   set status [cellname list exists $cell]
	   set pindict [dict create]
	   if {$status != 0} {
	       load $cell
	       box values 0 0 0 0
	       set n 1
	       set changed false
	       foreach pin [lrange $ftokens 2 end] {
		  # Tcl "split" will not group spaces and tabs but leaves
		  # empty strings.
		  if {$pin == {}} {continue}

		  # NOTE:  Should probably check for CDL-isms, global bang
		  # characters, case insensitive matches, etc.  This routine
		  # currently expects a 1:1 match between netlist and layout.

		  # This routine will also make ports out of labels in the
		  # layout if they have not been read in or created as ports.
		  # However, if there are multiple labels with the same port
		  # name, only the one triggered by "goto" will be made into
		  # a port.

		  set testpin $pin
		  set pinidx [port $testpin index]

		  # Test a few common delimiter translations.  This list
		  # is by no means exhaustive.

		  if {$pinidx == ""} {
		      set testpin [string map {\[ < \] >]} $pin]
		      set pinidx [port $testpin index]
		  }
		  if {$pinidx == ""} {
		      set testpin [string map {< \[ > \]} $pin]
		      set pinidx [port $testpin index]
		  }

		  # Also test some case sensitivity issues (also not exhaustive)

		  if {$pinidx == ""} {
		      set testpin [string tolower $pin]
		      set pinidx [port $testpin index]
		  }
		  if {$pinidx == ""} {
		      set testpin [string toupper $pin]
		      set pinidx [port $testpin index]
		  }

                  if {$pinidx != ""} {
		      port $testpin index $n
		      if {$pinidx != $n} {
			  set changed true
		      }
		      incr n
		      # Record the original and modified pin names
		      dict set pindict $pin $testpin
		  } else {
		      set layer [goto $pin]
		      if {$layer != ""} {
		         port make $n
			 incr n
			 set changed true
		      }
		      # Record the pin name as unmodified
		      dict set pindict $pin $pin
		  }
	       }
	       if {$changed} {
		   puts stdout "Cell $cell port order was modified."
	       }
	   } else {
	       puts stdout "Cell $cell in netlist has not been loaded."
	   }
       } elseif {$keyword == ".pininfo"} {
	   if {($cell != "") && ($status != 0)} {
	       foreach pininfo [lrange $ftokens 1 end] {
		   set infopair [split $pininfo :]
		   set pinname [lindex $infopair 0]
		   set pindir [lindex $infopair 1]
		   set pin [dict get $pindict $pinname]
		   case $pindir {
		      B {port $pin class inout}
		      I {port $pin class input}
		      O {port $pin class output}
		   }
	       }
	   }
       } elseif {$keyword == ".ends"} {
	   set cell ""
	   set status 0
       }
   }
   resumeall
}


#-----------------------------------------------------------------

