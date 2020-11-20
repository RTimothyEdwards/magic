# Copyright 2020 Efabless Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

puts "Performing GDS Streaming Out..."
set ::env(MAGIC_PAD) 0
set ::env(MAGIC_ZEROIZE_ORIGIN) 1
set ::env(MAGTYPE) mag
source $::env(test_dir)/config.tcl
set ::env(TECH_LEF) "$::env(PDK_ROOT)/$::env(PDK)/libs.ref/$::env(STD_CELL_LIBRARY)/techlef/$::env(STD_CELL_LIBRARY).tlef"

drc off


lef read $::env(TECH_LEF)
if {  [info exist ::env(EXTRA_LEFS)] } {
	set lefs_in $::env(EXTRA_LEFS)
	foreach lef_file $lefs_in {
		lef read $lef_file
	}
}
def read  $::env(test_dir)/$::env(DESIGN).def


gds readonly true
gds rescale false
if {  [info exist ::env(EXTRA_GDS_FILES)] } {
       set gds_files_in $::env(EXTRA_GDS_FILES)
       foreach gds_file $gds_files_in {
               gds read $gds_file
       }
}


load $::env(DESIGN)
select top cell

# padding

if { $::env(MAGIC_PAD) } {
	puts "\[INFO\]: Padding LEFs"
	# assuming scalegrid 1 2
	# use um
	select top cell
	box grow right [expr 100*($::env(PLACE_SITE_WIDTH))]
	box grow left [expr 100*($::env(PLACE_SITE_WIDTH))]
	box grow up [expr 100*($::env(PLACE_SITE_HEIGHT))]
	box grow down [expr 100*($::env(PLACE_SITE_HEIGHT))]
	property FIXED_BBOX [box values]
}
if { $::env(MAGIC_ZEROIZE_ORIGIN) } {
	# assuming scalegrid 1 2
	# makes origin zero based on the selection
	puts "\[INFO\]: Zeroizing Origin"
	set bbox [box values]
	set offset_x [lindex $bbox 0]
	set offset_y [lindex $bbox 1]
	move origin [expr {$offset_x/2}] [expr {$offset_y/2}]
	puts "\[INFO\]: Current Box Values: [box values]"
	property FIXED_BBOX [box values]
}

puts "\[INFO\]: Saving .mag view With BBox Values: [box values]"
cellname filepath $::env(DESIGN) $::env(OUT_DIR)
save

select top cell

# Write gds
cif *hier write disable
gds write $::env(OUT_DIR)/$::env(DESIGN).gds
puts "\[INFO\]: GDS Write Complete"
puts "\[INFO\]: MAGIC TAPEOUT STEP DONE"
