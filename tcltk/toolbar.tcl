global Opts
global Winopts

global current_toolbar
global fileName

# Generate the toolbar for the wrapper
proc magic::maketoolbar {framename} {

   global Opts
   global Winopts
   # Don't do anything if in suspend mode
   set topname [winfo toplevel $framename]
   if {[info exists Winopts(${topname},suspend)]} {
      if { $Winopts(${topname},suspend) > 0} { return }
   }

   if {$Opts(toolbar) == 0} {
      magic::maketoolimages
      set Opts(toolbar) 1
   }

   # Destroy any existing toolbar before starting
   set alltools [winfo children ${framename}.toolbar]
   foreach i $alltools { destroy $i }

   # All toolbar commands will be passed to the appropriate window
   set win ${framename}.magic

   # Generate layer images and buttons for toolbar
   if {$Opts(hidespecial) == 0} {
      set special_layers {errors labels subcell}
   } else {
      set special_layers {}
   }

   if {$Opts(hidelocked) == 0} {
      set all_layers [concat $special_layers [magic::tech layer "*"]]
   } else {
      set all_layers [concat $special_layers [magic::tech unlocked]]
   }

   # Create a canvas for the toolbar
   if {![winfo exists ${framename}.toolbar.canvas]} {
      canvas ${framename}.toolbar.canvas
   }
   grid ${framename}.toolbar.canvas -row 0 -column 0 -sticky "news"

   # Add a frame to the canvas, on which the layer buttons and
   # labels are placed
   frame ${framename}.toolbar.canvas.frame
   ${framename}.toolbar.canvas create window 0 0 -anchor nw \
      -window ${framename}.toolbar.canvas.frame

   # Read layers from a tcl file [tech name]_toolbar.tcl

   global current_toolbar
   global fileName

   # Check if the file exists, and if not, create it and populate with a default_toolbar
   # and a current_toolbar. current_toolbar is a placeholder
   if {![file exists "./[tech name]_toolbars.tcl"]} {
      # If the file doesn't exist, just set the global variable current_toolba
      # which is a default toolbar ordering
      set current_toolbar $all_layers
      set fileName ""
   } else {
      # otherwise, use the current_toolbar from the file
      set fileName "./[tech name]_toolbars.tcl"
      source $fileName
      # this will set current_toolbar to the one from the file
   }


   # Place layers on the toolbar
   set i 0
   foreach layername $current_toolbar {
      createLayerFrame $framename $layername $i 
      incr i
   }

   # Add mouswheel functionlity
   # Bind Button-4 (scroll up) and Button-5 (scroll down) to the custom procedure
   bind ${framename}.toolbar.canvas <Button-4> [subst { ${framename}.toolbar.canvas \
      yview scroll -1 units}]
   bind ${framename}.toolbar.canvas <Button-5> [subst { ${framename}.toolbar.canvas \
      yview scroll 1 units}]

   # Create a vertical scrollbar for the canvas
   scrollbar ${framename}.toolbar.vscroll -orient "vertical" \
   -command [list ${framename}.toolbar.canvas yview]

   grid ${framename}.toolbar.vscroll -row 0 -column 1 -sticky "nws"

   # Configure the canvas to use the scrollbar
   ${framename}.toolbar.canvas configure -yscrollcommand \
   [list ${framename}.toolbar.vscroll set]

   # Define the canvas scroll region (as an event callback)
   bind ${framename} <Configure> "updateCanvasScrollRegion ${framename}"
}

# Function to place layer frame with a button and label
# on the toolbar, at a specific row $i
proc createLayerFrame {framename layername i} {

   # All toolbar commands will be passed to the appropriate window
   set win ${framename}.magic

   # Frame to group together the layer button and label
   frame ${framename}.toolbar.canvas.frame.f$layername
   set layer_frame ${framename}.toolbar.canvas.frame.f$layername
   grid $layer_frame -row $i -column 0 -sticky "news"

   # Set short naming for buttons and labels
   set toolbar_label ${layer_frame}.l
   # Place label of layer next to the layer button
   label $toolbar_label -text $layername
   grid $toolbar_label -row $i -column 1 -sticky "w"

   # Place the layer button, checking if it is locked or not
   set locklist [tech locked]

   if {[lsearch $locklist $layername] != -1} {
      # Locked button bindings

      set toolbar_button ${layer_frame}.p
      button $toolbar_button -image pale_$layername

      # Bind keypresses when mouse if over layer frame
      bind $layer_frame <KeyPress-u> \
		"$win tech unlock $layername ; \
		grid forget $toolbar_button ; \
		grid ${layer_frame}.b -row $i -column 0 -sticky w"

      # Bindings for painiting, erasing and seeing layers,
      # which are bound both to the layer button, as well
      # as the layer label
      set childrenList [winfo children $layer_frame]

      foreach child $childrenList {
	 # 3rd mouse button makes layer invisible; 1st mouse button restores it.
	 # 2nd mouse button paints the layer color.  Key "p" also does paint, esp.
	 # for users with 2-button mice.  Key "e" erases, as does Shift-Button-2.
	 bind $child <ButtonPress-1> "$win see $layername"
	 bind $child <ButtonPress-3> "$win see no $layername"

	 # Intercept mousewheel on the layer/button as well
	 bind $child <Button-4> \
		[subst { event generate ${framename}.toolbar.canvas <Button-4> }]
	 bind $child <Button-5> \
		[subst { event generate ${framename}.toolbar.canvas <Button-5> }]
      }

      # Bind the mouse enter event to highlight the label
      bind $toolbar_label <Enter> "$toolbar_label configure -background yellow"

      bind $layer_frame <Enter> \
	 	[subst {focus %W ; ${framename}.titlebar.message configure \
		-text "$layername (locked)"}]

   } else {
      # Unlocked button bindings
   
      set toolbar_button ${layer_frame}.b
      button $toolbar_button -image img_$layername

      # Bind keypresses when mouse if over layer frame
      bind $layer_frame <KeyPress-p> "$win paint $layername"
      bind $layer_frame <KeyPress-s> "$win select more area $layername"
      bind $layer_frame <KeyPress-S> "$win select less area $layername"
      bind $layer_frame <KeyPress-e> "$win erase $layername"

      bind $layer_frame <KeyPress-l> \
         "puts $i; \
         $win tech lock $layername ; \
         grid forget $toolbar_button ; \
         grid ${layer_frame}.p -row $i -column 0 -sticky w"

      # Bindings for painiting, erasing and seeing layers,
      # which are bound both to the layer button, as well
      # as the layer label
      set childrenList [winfo children $layer_frame]

      foreach child $childrenList {
	 # 3rd mouse button makes layer invisible; 1st mouse button restores it.
	 # 2nd mouse button paints the layer color.  Key "p" also does paint, esp.
	 # for users with 2-button mice.  Key "e" erases, as does Shift-Button-2.
	 bind $child <ButtonPress-1> "$win see $layername"
	 bind $child <ButtonPress-2> "$win paint $layername"
	 bind $child <Shift-ButtonPress-2> "$win erase $layername"
	 bind $child <ButtonPress-3> "$win see no $layername"

	 # Intercept mousewheel on the layer/button as well
	 bind $child <Button-4> \
		[subst { event generate ${framename}.toolbar.canvas <Button-4> }]
	 bind $child <Button-5> \
		[subst { event generate ${framename}.toolbar.canvas <Button-5> }]
      }

      # Bind the mouse enter event to highlight the label
      bind $toolbar_label <Enter> "$toolbar_label configure -background yellow"

      bind $layer_frame <Enter> \
         [subst {focus %W ; ${framename}.titlebar.message configure -text "$layername"}]

   }

   # Common bindings

   grid $toolbar_button -row $i -column 0 -sticky "w"

   # Bindings:  Leaving the layer row clears titlbar message
   bind $layer_frame <Leave> \
      [subst {${framename}.titlebar.message configure -text ""}]

   # Intercept mousewheel and redirect command to the canvas
   bind $layer_frame <Button-4> \
      [subst { event generate ${framename}.toolbar.canvas <Button-4> }]
   bind $layer_frame <Button-5> \
      [subst { event generate ${framename}.toolbar.canvas <Button-5> }]

   # Bind the mouse leave event to reset the label
   set bgColor [${framename}.toolbar cget -background]
   bind $toolbar_label <Leave> "$toolbar_label configure -background $bgColor"

}

# Function to update the canvas scrolling region after a resize
proc updateCanvasScrollRegion {framename} {
   set bbox [${framename}.toolbar.canvas bbox all]
   set minwidth [expr [lindex $bbox 2] - [lindex $bbox 0]]
   if {[llength $bbox] == 4} {
      ${framename}.toolbar.canvas configure -scrollregion $bbox
   }
   ${framename}.toolbar.canvas configure -width $minwidth
   set winheight [expr [winfo height ${framename}] \
   - [winfo height ${framename}.titlebar]]

   ${framename}.toolbar.canvas configure -height $winheight
}
