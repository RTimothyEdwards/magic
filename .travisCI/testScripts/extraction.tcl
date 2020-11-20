

#!/usr/bin/tclsh
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

puts "Performing Spice Extractions..."
source $::env(test_dir)/config.tcl
set ::env(TECH_LEF) "$::env(PDK_ROOT)/$::env(PDK)/libs.ref/$::env(STD_CELL_LIBRARY)/techlef/$::env(STD_CELL_LIBRARY).tlef"

set ::env(OUT_DIR)  $::env(test_dir)/ext

if { ![file isdirectory $::env(OUT_DIR)] } {
	exec mkdir $::env(OUT_DIR)/
}

set magic_export $::env(OUT_DIR)/magic_spice.tcl
set commands \
"

if { \$::env(TARGET_TYPE) == \"gds\"} {
	gds read $::env(test_dir)/$::env(DESIGN).gds
} else {
	if { \$::env(TARGET_TYPE) == \"mag\" } {
		load $::env(test_dir)/$::env(DESIGN).mag
	} else {
		lef read $::env(TECH_LEF)
		if {  \[info exist ::env(EXTRA_LEFS)\] } {
			set lefs_in \$::env(EXTRA_LEFS)
			foreach lef_file \$lefs_in {
				lef read \$lef_file
			}
		}
		def read  $::env(test_dir)/$::env(DESIGN).def
	}
}

load \$::env(DESIGN) -dereference
cd \$::env(OUT_DIR)/
extract do local
extract no capacitance
extract no coupling
extract no resistance
extract no adjust
# extract warn all
extract
ext2spice lvs
ext2spice $::env(DESIGN).ext
feedback save $::env(OUT_DIR)/magic_extraction_feedback.txt
"
	set magic_export_file [open $magic_export w]
		puts $magic_export_file $commands
	close $magic_export_file
	set ::env(PDKPATH) "$::env(PDK_ROOT)/$::env(PDK)/"
	# the following MAGTYPE has to be maglef for the purpose of LVS
	# otherwise underlying device circuits would be considered
	set ::env(MAGTYPE) maglef
	exec magic \
		-noconsole \
		-dnull \
		-rcfile $::env(MAGIC_MAGICRC) \
		$magic_export \
		</dev/null \
		|& tee /dev/tty $::env(OUT_DIR)/magic_spice.log


puts "Done!"
