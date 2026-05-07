# extract.tcl
#
# Complete Magic extraction workflow:
#   1. Load technology
#   2. Load layout
#   3. Extract parasitic capacitances  (extract all  → __CELL__.ext)
#   4. Extract parasitic resistances   (extresist all → __CELL__.res.ext)
#   5. Write SPICE netlist             (ext2spice    → __CELL__.spice)
#
# __TECH__ and __CELL__ are substituted by extract.js before execution.

tech load __TECH__
load /work/__CELL__

# Write all intermediate files to /work/
extract path /work

# Enable resistance extraction
extract do resistance

# Generate __CELL__.ext
extract all

# extresist requires a valid selection/box
select top cell

# Generate __CELL__.res.ext
extresist all

# SPICE generation settings
ext2spice format ngspice
ext2spice extresist on
ext2spice cthresh 0

# Generate __CELL__.spice
ext2spice /work/__CELL__