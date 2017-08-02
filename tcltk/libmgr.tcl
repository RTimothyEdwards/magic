#------------------------------------------------------
# Script for generating the "library manager" window.
#
# Written by Tim Edwards, July 2017
#------------------------------------------------------

global Opts

if {$::tk_version >= 8.5} {

set Opts(libmgr) 0

magic::tag addpath "magic::libmanager"
magic::tag path    "magic::libmanager"

# Callback to the library manager

proc magic::libcallback {command} {
   global Opts

   set rpath [ split [.libmgr.box.view focus] "/"]
   set rootdef [lindex $rpath 0]
   set cellpath [lrange $rpath 1 end]
   set celldef [lrange $rpath end end]

   if { $Opts(target) == "default" } {
      set winlist [magic::windownames layout]
      set winname [lindex $winlist 0]
   } else {
      set winname $Opts(target)
   }

   switch $command {
      load {$winname load $celldef}
      place {$winname getcell $celldef}
      pick {
	magic::tool pick
	$winname getcell $celldef
	magic::startselect $winname pick
      }
   }
}

#----------------------------------------------
# Create the library manager window
#----------------------------------------------

proc magic::makelibmanager { mgrpath } {

   toplevel ${mgrpath}
   wm withdraw ${mgrpath}
   frame ${mgrpath}.actionbar
   frame ${mgrpath}.box
   frame ${mgrpath}.target

   ttk::treeview ${mgrpath}.box.view -show tree -selectmode browse \
		-yscrollcommand "${mgrpath}.box.vert set" \
		-xscrollcommand "${mgrpath}.box.vert set" \
		-columns 1
   scrollbar ${mgrpath}.box.vert -orient vertical -command "${mgrpath}.box.view yview"

   pack ${mgrpath}.actionbar -side top -fill x
   pack ${mgrpath}.box.view -side left -fill both -expand true
   pack ${mgrpath}.box.vert -side right -fill y
   pack ${mgrpath}.box -side top -fill both -expand true
   pack ${mgrpath}.target -side top -fill x

   button ${mgrpath}.actionbar.load  -text "Load" -command {magic::libcallback load}
   button ${mgrpath}.actionbar.place -text "Place" -command {magic::libcallback place}
   button ${mgrpath}.actionbar.pick  -text "Pick" -command {magic::libcallback pick}

   pack ${mgrpath}.actionbar.load -side left
   pack ${mgrpath}.actionbar.place -side left
   pack ${mgrpath}.actionbar.pick -side left

   label ${mgrpath}.target.name -text "Target window:"
   menubutton ${mgrpath}.target.list -text "default" \
 	   -menu ${mgrpath}.target.list.winmenu

   pack ${mgrpath}.target.name -side left -padx 2
   pack ${mgrpath}.target.list -side left

   #.winmenu clone ${mgrpath}.target.list.winmenu

   #Withdraw the window when the close button is pressed
   wm protocol ${mgrpath} WM_DELETE_WINDOW  "set Opts(libmgr) 0 ; \
		wm withdraw ${mgrpath}"

   #-------------------------------------------------
   # Callback when a treeview item is opened
   #-------------------------------------------------

   bind .libmgr <<TreeviewOpen>> {
      set s [.libmgr.box.view selection]
      # puts stdout "open $s"
      foreach i [.libmgr.box.view children $s] {
	 magic::addtolibset $i
         .libmgr.box.view item $i -open false
      }
   }

   bind .libmgr <<TreeviewClose>> {
      set s [.libmgr.box.view selection]
      # puts stdout "close $s"
      foreach i [.libmgr.box.view children $s] {
         foreach j [.libmgr.box.view children $i] {
	    .libmgr.box.view delete $j
         }
      }
   }
}

proc magic::addlibentry {parent child tech} {
   if {$child != 0} {
      set hiername [join [list $parent $child] "/"]
      # puts stdout "libentry $hiername"
      if {[.libmgr.box.view exists $hiername] == 0} {
         .libmgr.box.view insert $parent end -id $hiername -text "$child"
         .libmgr.box.view set $hiername 0 "$tech"
      }
   }
}

# 
proc magic::addtolibset {item} {
   set pathname [.libmgr.box.view item $item -text]
   set pathfiles [glob -nocomplain -directory $pathname *.mag]

   # Sort files alphabetically
   
   foreach f [lsort $pathfiles] {
      set rootname [file tail [file root $f]]
      if {![catch {open $f r} fin]} {
         # Read first two lines, break on error
         if {[gets $fin line] < 0} {continue}	;# empty file error
         if {$line != "magic"} {continue}	;# not a magic file
         if {[gets $fin line] < 0} {continue}	;# truncated file
         set tokens [split $line]
         if {[llength $tokens] != 2} {continue}
         set keyword [lindex $tokens 0]
         if {$keyword != "tech"} {continue}
         set tech [lindex $tokens 1]
         close $fin

	 # filter here for compatible technology

         magic::addlibentry $item $rootname $tech
      }
   }
}

#--------------------------------------------------------------
# The cell manager window main callback function
#--------------------------------------------------------------

proc magic::libmanager {{option "update"}} {
   global editstack
   global CAD_ROOT

   # Use of command "path" is recursive, so break if level > 0
   if {[info level] > 1} {return}

   # Check for existence of the manager widget
   if {[catch {wm state .libmgr}]} {
      if {$option == "create"} {
	 magic::makelibmanager .libmgr
      } else {
	 return
      }
   } elseif { $option == "create"} {
      return
   }

   magic::suspendall

   # Get existing list of paths
   set curpaths [.libmgr.box.view children {}]

   # Find all library paths for cell searches
   # (Separated so that system default path can be viewed or ignored
   # by option selection (to be done).)
   set spath1 [magic::path search]	;# Rank 1 search
   set spath2 [magic::path cell]	;# Rank 2 search

   # If any component of curpaths is not in spath1 or spath2, remove it.
   set allpaths [concat $spath1 $spath2]
   foreach path $curpaths {
      if {[lsearch $allpaths $path] == -1} {
	  .libmgr.box.view delete $path
      }
   }

   foreach i $spath1 {
      if {[.libmgr.box.view exists $i] == 0} {
 	 .libmgr.box.view insert {} end -id $i -text $i
      }
      magic::addtolibset $i
      .libmgr.box.view item $i -open false
   }
   foreach i $spath2 {
      set expandname [subst $i]
      if {[.libmgr.box.view exists $expandname] == 0} {
 	 .libmgr.box.view insert {} end -id $expandname -text $expandname
      }
      magic::addtolibset $expandname
      .libmgr.box.view item $expandname -open false
   }
   magic::resumeall
}

}	;# (if Tk version 8.5)

