#------------------------------------------------------
# Script for generating the "DRC manager" window.
#
# Written by Tim Edwards, November 2019
#------------------------------------------------------

global Opts

if {$::tk_version >= 8.5} {

set Opts(drcmgr) 0

magic::tag addpath "magic::drcmanager"
magic::tag path    "magic::drcmanager"

# Callback to the DRC manager

proc magic::drccallback {command} {
   global Opts

   set fid [.drcmgr.box.view selection]
   if {[.drcmgr.box.view parent $fid] == {}} {
      set value {}
   } else {
      set value [.drcmgr.box.view item $fid -text]
   }

   if { $Opts(target) == "default" } {
      set winlist [magic::windownames layout]
      set winname [lindex $winlist 0]
   } else {
      set winname $Opts(target)
   }

   switch $command {
      update {
	 magic::drcmanager update
      }
      last {
	 .drcmgr.box.view selection set [.drcmgr.box.view prev $fid]
	 magic::drccallback zoom
      }
      next {
	 .drcmgr.box.view selection set [.drcmgr.box.view next $fid]
	 magic::drccallback zoom
      }
      zoom {
	 if {$value != {}} {
	    set snaptype [snap]
	    snap internal
	    box values {*}$value
	    magic::suspendall
	    magic::findbox zoom
	    magic::zoom 2
	    magic::resumeall
	    snap $snaptype
	 }
      }
   }
}

#----------------------------------------------
# Create the DRC manager window
#----------------------------------------------

proc magic::makedrcmanager { mgrpath } {
   global filtered

   set filtered 1
   toplevel ${mgrpath}
   wm withdraw ${mgrpath}
   frame ${mgrpath}.actionbar
   frame ${mgrpath}.box
   frame ${mgrpath}.target

   ttk::treeview ${mgrpath}.box.view -selectmode browse \
		-yscrollcommand "${mgrpath}.box.vert set" \
		-xscrollcommand "${mgrpath}.box.vert set" \
		-columns 0
   scrollbar ${mgrpath}.box.vert -orient vertical -command "${mgrpath}.box.view yview"
   ${mgrpath}.box.view heading #0 -text "DRC Rule"
   ${mgrpath}.box.view heading 0 -text "Error Number"
   ${mgrpath}.box.view column #0 -stretch true -anchor w -minwidth 350
   ${mgrpath}.box.view column 0 -stretch false -anchor center -minwidth 50

   grid columnconfigure ${mgrpath}.box 0 -weight 1 -minsize 500
   grid columnconfigure ${mgrpath}.box 1 -weight 0
   grid rowconfigure ${mgrpath}.box 0 -weight 1
   grid ${mgrpath}.box.view -row 0 -column 0 -sticky news
   grid ${mgrpath}.box.vert -row 0 -column 1 -sticky news

   grid rowconfigure ${mgrpath} 0 -weight 0
   grid rowconfigure ${mgrpath} 1 -weight 1
   grid rowconfigure ${mgrpath} 2 -weight 0
   grid columnconfigure ${mgrpath} 0 -weight 1
   grid ${mgrpath}.actionbar -row 0 -column 0 -sticky news
   grid ${mgrpath}.box -row 1 -column 0 -sticky news
   grid ${mgrpath}.target -row 2 -column 0 -sticky news

   button ${mgrpath}.actionbar.update -text "Update" \
	-command {magic::drccallback update}
   button ${mgrpath}.actionbar.last -text "Last" -command {magic::drccallback last}
   button ${mgrpath}.actionbar.next -text "Next" -command {magic::drccallback next}
   button ${mgrpath}.actionbar.save -text "Save" -command {magic::drc_save_report}
   button ${mgrpath}.actionbar.zoom  -text "Zoom" -command {magic::drccallback zoom}

   pack ${mgrpath}.actionbar.update -side left
   pack ${mgrpath}.actionbar.last -side left
   pack ${mgrpath}.actionbar.next -side left
   pack ${mgrpath}.actionbar.save -side left
   pack ${mgrpath}.actionbar.zoom -side right

   label ${mgrpath}.target.name -text "Target window:"
   menubutton ${mgrpath}.target.list -text "default" \
 	   -menu ${mgrpath}.target.list.winmenu

   pack ${mgrpath}.target.name -side left -padx 2
   pack ${mgrpath}.target.list -side left

   #Withdraw the window when the close button is pressed
   wm protocol ${mgrpath} WM_DELETE_WINDOW  "set Opts(drcmgr) 0 ; \
		wm withdraw ${mgrpath}"

   #-------------------------------------------------
   # Callback when a treeview item is opened
   #-------------------------------------------------

   bind .drcmgr <<TreeviewOpen>> {
      set s [.drcmgr.box.view selection]
      foreach i [.drcmgr.box.view children $s] {
         # NOTE:  not hierarchical
         .drcmgr.box.view item $i -open false
      }
   }

   bind .drcmgr <<TreeviewClose>> {
      set s [.drcmgr.box.view selection]
      foreach i [.drcmgr.box.view children $s] {
         foreach j [.drcmgr.box.view children $i] {
	    .drcmgr.box.view delete $j
         }
      }
   }
}

proc magic::adddrcentry {key valuelist} {
   set id [.drcmgr.box.view insert {} end -text ${key}]
   set i 0
   foreach value $valuelist {
      .drcmgr.box.view insert $id end -text "$value"
      incr i
   }
}

#--------------------------------------------------------------
# The cell manager window main callback function
#--------------------------------------------------------------

proc magic::drcmanager {{option "update"}} {
   global editstack
   global CAD_ROOT

   # Check for existence of the manager widget
   if {[catch {wm state .drcmgr}]} {
      if {$option == "create"} {
	 magic::makedrcmanager .drcmgr
      } else {
	 return
      }
   } elseif { $option == "create"} {
      return
   }

   magic::suspendall

   # Get existing list of error classes and remove them
   set currules [.drcmgr.box.view children {}]
   foreach rule $currules {
      .drcmgr.box.view delete ${rule}
   }

   # Run DRC
   select top cell
   set drcdict [dict create {*}[drc listall why]]

   # set first true
   dict for {key value} $drcdict {
      magic::adddrcentry ${key} ${value}
      # .drcmgr.box.view item ${key} -open $first
      # set first false
   }
   magic::resumeall
}

}	;# (if Tk version 8.5)

#---------------------------------------------------
# Alternative way to view/save DRC errors in magic.
# Dump a text file of errors and positions.  Note
# that the dump, using "drc listall why", enumerates
# every single edge check and is therefore more
# detailed than the areas declared by "drc count"
# or enumerated in "drc find".
#---------------------------------------------------

proc magic::drc_save_report {{cellname ""} {outfile ""}} {

    if {$outfile == ""} {set outfile "drc.out"}

    set fout [open $outfile w]
    set oscale [cif scale out]

    # magic::suspendall

    if {$cellname == ""} {
	select top cell
        set cellname [cellname list self]
	set origname ""
    } else {
	set origname [cellname list self]
	puts stdout "loading $cellname\n"
	flush stdout

	load $cellname
	select top cell
    }

    drc check
    set count [drc list count]

    puts $fout "$cellname $count"
    puts $fout "----------------------------------------"
    set drcresult [drc listall why]
    foreach {errtype coordlist} $drcresult {
	puts $fout $errtype
	puts $fout "----------------------------------------"
	foreach coord $coordlist {
	    set bllx [expr {$oscale * [lindex $coord 0]}]
	    set blly [expr {$oscale * [lindex $coord 1]}]
	    set burx [expr {$oscale * [lindex $coord 2]}]
	    set bury [expr {$oscale * [lindex $coord 3]}]
	    set coords [format " %.3f %.3f %.3f %.3f" $bllx $blly $burx $bury]
	    puts $fout "$coords"
	}
	puts $fout "----------------------------------------"
    }
    puts $fout ""

    if {$origname != ""} {
	load $origname
    }

    # magic::resumeall

    close $fout
    puts stdout "done with $outfile\n"
    flush stdout
}

#---------------------------------------------------
# Read back a dumped file of DRC errors.  This is of
# limited use, as any layout should have DRC errors
# already marked.  This routine loads errors into
# "feedback" areas, which is redundant to the error
# tiles.  However, feedback areas are more precise,
# as they mark the error area at each checked edge
#---------------------------------------------------

proc magic::drc_load_report {{drc_file ""}} {

    if {$drc_file == ""} {
	set drc_file "drc.out"
    }
    set fin [open $drc_file r]

    puts stdout "Reading $drc_file\n"
    flush stdout

    magic::suspendall

    set error_text ""
    while {[gets $fin line] >= 0} {
	if {[string first " " $line] == 0} {
	    if [regexp { ([^ ]+) ([^ ]+) ([^ ]+) ([^ ]+)} $line lmatch llx lly urx ury] {
		feedback add "$error_text" vert_highlights ${llx}um ${lly}um \
			${llx}um ${ury}um ${urx}um ${ury}um ${urx}um ${lly}um
	    }
	} elseif {[string first "-" $line] != 0} {
	    set error_text $line
	}
    }
    magic::resumeall

    close $fin

    puts stdout "Done.\n"
    puts stdout "Use \"feedback find\" to enumerate errors.\n"
    flush stdout
}

