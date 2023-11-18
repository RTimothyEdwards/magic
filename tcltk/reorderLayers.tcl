
global Opts

global search_var
global current_toolbar
global search_var "" 

# Dialog window to reorder layers in the toolbar, as well as remove some of them
proc magic::reorderToolbar {framename} {
   global Opts
   global current_toolbar

   # Create the main window
   if {[catch {toplevel .reorder}]} {
      foreach child [winfo children .reorder] {
         destroy $child
      }
   }

   # Establish the geomtery of the window
   reorderToolbar_addWidgets 

   # Add all the functionalities to the placed widgets
   reorderToolbar_widgetBindings $framename

   # Clear the current listbox contents
   # .reorder.frame.listbox delete 0 end

   # Populate the listbox initially
   set all_layers_array [split $current_toolbar]
   updateTreeview $all_layers_array 
}

proc reorderToolbar_addWidgets {} {
   # Set the window title
   wm title .reorder "Reorder layers"

   # Add the container frame for the window
   frame .reorder.frame -borderwidth 1
   grid .reorder.frame -pady 10 -padx 10

   # Add the searchbar for layers
   # TODO fix without using a global variable. 
   global search_var "" 
   label .reorder.frame.search_label -text "Search layer: " 
   entry .reorder.frame.entry -textvariable search_var -width 30

   # Add the dropdown menu which allows to load a chosen toolbar
   label .reorder.frame.toolbar_search_label -text "Preset toolbar: " 
   ttk::combobox .reorder.frame.toolbar_preset_menu -textvariable selected_toolbar

   # Add the listbox, containing all the layers 
   # listbox .reorder.frame.listbox -width 40 -height 10
   ttk::treeview .reorder.frame.tree -columns {layer order} -show headings
   # TODO fix this to show... -show headings broke it somehow
   .reorder.frame.tree insert {} end -id 0 -tag treeAll \
    -text "All layers" -values {"All layers" -} -open true
   .reorder.frame.tree heading layer -text "Layer"
   .reorder.frame.tree heading order -text "Toolbar row"

   # Add buttons for editing layer ordering
   button .reorder.frame.move_layer_button -width 10 -text "Move layer"
   button .reorder.frame.group_layer_button -width 10 -text "Group selected"
   button .reorder.frame.delete_layer_button -width 10 -text "Delete layer"

   # Add the buttons at the bottom (Confirm, Load, and Save) the toolbar settings
   button .reorder.frame.button_confirm -width 10 -text "Confirm"
   button .reorder.frame.button_save -width 10 -text "Save"
   button .reorder.frame.button_load -width 10 -text "Load"

   # Organize widgets placement in the window
   grid .reorder.frame.search_label -row 0 -column 0 -sticky e -columnspan 1
   grid .reorder.frame.entry -row 0 -column 1 -sticky we -columnspan 2
   grid .reorder.frame.tree -pady 5 -row 1 -column 0 -columnspan 3 -rowspan 3 -sticky we
   grid .reorder.frame.move_layer_button -padx 5 -pady 5 -row 1 -column 3 -sticky sn
   grid .reorder.frame.delete_layer_button -padx 5 -pady 5 -row 2 -column 3 -sticky sn
   grid .reorder.frame.group_layer_button -padx 5 -pady 5 -row 3 -column 3 -sticky sn
   grid .reorder.frame.toolbar_search_label  -row 4 -column 0 \
   -columnspan 1 -sticky e
   grid .reorder.frame.toolbar_preset_menu  -row 4 -column 1 \
   -columnspan 2 -sticky nw
   grid .reorder.frame.button_confirm -pady 5 -padx 10 -row 5 -column 0 \
   -columnspan 1 -sticky w
   grid .reorder.frame.button_save -pady 5 -padx 10 -row 5 -column 1 \
   -columnspan 1 -sticky w
   grid .reorder.frame.button_load -pady 5 -padx 10 -row 5 -column 2 \
   -columnspan 1 -sticky w
}

proc reorderToolbar_widgetBindings {framename} {
   
   global current_toolbar

   set all_layers_array [split $current_toolbar]

   # When window is closed reset the search entry
   bind .reorder.frame <Destroy> {set search_var ""}

   # Bind the updateTreeview function to the entry widget
   bind .reorder.frame.entry <KeyRelease> [list updateTreeview $all_layers_array]

   # configure the combobox widget to include all the presets in toolbox.tcl
   .reorder.frame.toolbar_preset_menu configure -values [reorderToolbar_getToolbarPresets]

   # Bind the Move layer, Delete layer, and Group selection buttons

   bind .reorder.frame.move_layer_button <Button-1> {
      set selectedItems [.reorder.frame.tree selection]
      puts "$selectedItems"
   }

   bind .reorder.frame.delete_layer_button <Button-1> {
      # Get selected layers from the tree and delete them (from the tree)
      set selectedItems [.reorder.frame.tree selection]
      if {[llength $selectedItems] > 0} {

         global current_toolbar

         # Delete layers from the tree, and from the $current_toolbar, sourced from toolbar.tcl
         # in order to reuse updateTreeview to simply regenerate the tree. Note, this is doesn't
         # remember the changes the user made. The user needs to give a name to the preset and click
         # the Save button
         foreach item $selectedItems {
            # remove item from current_toolbar, inplace
            #set current_toolbar [lreplace $current_toolbar [set current_toolbar {}] \
            #$item $item ]

            set current_toolbar [lreplace $current_toolbar [expr {$item-1}] [expr {$item-1}] ]
         }
      .reorder.frame.tree delete $selectedItems
      # Regenerate the tree
      updateTreeview $current_toolbar
      }
   }

   bind .reorder.frame.group_layer_button <Button-1> {
      set selectedItems [.reorder.frame.tree selection]
      puts "$selectedItems"
   }

   # Bind the Confirm, Save and Load buttons 
   bind .reorder.frame.button_confirm <Button-1> [list apply {{framename} {
      magic::maketoolbar $framename
   }} $framename]


   bind .reorder.frame.button_load <Button-1> [list apply {{framename} {
      global search_var

      # clear the search var
      set search_var ""
      set selected_toolbar_name [.reorder.frame.toolbar_preset_menu get] 

      # If an improper string is written in the dropbox, throw an error
      if {[lsearch -exact [reorderToolbar_getToolbarPresets] $selected_toolbar_name] == -1} {
         after 5 [ list tk_messageBox -icon error -title "Error" \
         -message "Selected preset $selected_toolbar_name is \
         not in the list of toolbar configurations, inside [tech name]_toolbar.tcl. \
         Maybe you would like to save this new configuration?" -parent .reorder.frame]

          # Otherwise, set the listbox content to the toolbar preset
      } else {
      # Source the presets first. This is done due to scoping of variables
         source "${CAD_ROOT}/magic/tcl/toolbar.tcl"
         set selected_toolbar_layers [set $selected_toolbar_name]
         set layers_list [split $selected_toolbar_layers]
         updateTreeview $layers_list
         reorderToolbar_set-current_toolbar $layers_list
      }
   }} $framename]

   bind .reorder.frame.button_save <Button-1> {
      set toolbar_ordering [reorderToolbar_getListboxContents]
   }
   
}

proc reorderToolbar_set-current_toolbar { toolbar_preset } {
   global fileName
   global current_toolbar

   # if [tech name]_toolbars.tcl exists in the directory
   if {$fileName ne ""} {
      # Read the content of toolbar.tcl
      set scriptContent [exec cat $fileName]

      # Change the value in the script content
      set substitution [subst {set current_toolbar \{$toolbar_preset\} }]

      regsub -line -all {set current_toolbar \{.*\}} $scriptContent \
      $substitution newScriptContent

      # Write the updated content back to toolbar.tcl
      set scriptFile [open $fileName w]
      puts $scriptFile $newScriptContent
      close $scriptFile
   } else {
      set current_toolbar $toolbar_preset
   }
}

# Get the layers ordering from the listbox in reorderToolbar
proc reorderToolbar_getListboxContents {} {
   set listboxLayers {}
   set listbox .reorder.frame.listbox

   # Regular expression to match "word" followed by one or more spaces and one or more digits
   set regex {^(\w+)\s+\d+} ;
   for {set i 0} {$i < [$listbox size]} {incr i} {
      set item [$listbox get $i]
      if {[regexp $regex $item match layer]} {
         lappend listboxLayers $layer
      }
   }
   return $listboxLayers
}

# return the names of the toolbar presets defined in toolbar.tcl
# TODO: find a better way to implement this
proc reorderToolbar_getToolbarPresets {} {

   global fileName
   global current_toolbar 
   if {$fileName ne ""} {
      set toolbarPresets [list]
      # Get the variable names defined so far
      set oldVariables [info vars]
      # [info vars] does not take into account the new variable
      lappend oldVariables "oldVariables"

      source $fileName

      # Find the new variables
      foreach item [info vars] {
         if {[lsearch -exact $oldVariables $item] == -1} {
            lappend toolbarPresets $item
         }
      }
   } else {
      lappend toolbarPresets "default_toolbar"
   }

   # Return the toolbar preset names
   return $toolbarPresets
}


# update the listbox in the Reorder layers window
proc updateTreeview {data} {
   global search_var
   set treeAll_id [.reorder.frame.tree tag has "treeAll"]

   set search [string tolower $search_var]
   set layers_containing_string [lsearch -inline -all $data *$search*]
   set layers_not_containing_string [lsearch -inline -all -not $data *$search*]

   # destroy the treeview, so when we update with a new preset, we can clean everything
   foreach child [.reorder.frame.tree children $treeAll_id ] {
      .reorder.frame.tree delete $child
   }
   # 1) populate the tree: if items already exist, dont insert, if not insert (check using tag)
   # 2) if layer is in layers_containing string: move it to proper position
   # 3) if item is in layers_not_containing, delete it
   set i $treeAll_id 
   foreach item $data {
      incr i
      # if the layer is not in the treeview, insert it
      if { [string equal [.reorder.frame.tree tag has $item] ""] } {
         .reorder.frame.tree insert $treeAll_id end -id $i -tags $item -values "$item $i"
      }

      if { [lsearch -inline -all $layers_containing_string $item] != ""} {
         .reorder.frame.tree move [.reorder.frame.tree tag has $item] $treeAll_id end
      }

      if { [lsearch -inline -all $layers_not_containing_string $item] != ""} {
         .reorder.frame.tree delete [.reorder.frame.tree tag has $item] 
      }
   }
}


# Function to handle input submission
proc handleInput {entry listbox selectedLayer selectedIndex} {
   set inputNumber [$entry get]
   if {$inputNumber ne ""} {
      set formattedText [format "%-20s %5s" $selectedLayer $inputNumber]
      $listbox delete $selectedIndex
      $listbox insert $inputNumber $formattedText

      # Renumber the layers, between where the selected layer was removed and inserted
      renumberListbox $listbox $selectedIndex $inputNumber
      destroy .inputWindow
   }
}

proc renumberListbox {listbox initialIndex finalIndex} {
   # if layer was inserted above where it was originally
   if {$initialIndex > $finalIndex } {
      for { set ind [expr {$finalIndex + 1 }] } { $ind <= $initialIndex } { incr ind 1 } {
         set selectedLayer [lindex [$listbox get $ind] 0]
         set formattedText [format "%-20s %5s" $selectedLayer $ind]
         $listbox delete $ind
         $listbox insert $ind $formattedText
      }
   } 
   # if layer was inserted below where it was originally
   if {$initialIndex < $finalIndex } {
      for { set ind $initialIndex } { $ind < $finalIndex } { incr ind 1 } {
         set selectedLayer [lindex [$listbox get $ind] 0]
         set formattedText [format "%-20s %5s" $selectedLayer $ind]
         $listbox delete $ind
         $listbox insert $ind $formattedText
      }
   }
}
