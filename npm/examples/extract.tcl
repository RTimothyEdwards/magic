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

# Write all intermediate files to /work/ so ext2spice can find __CELL__.res.ext
extract path /work
extract all

# extresist requires a valid box cursor; span the full layout to be safe
box 0 0 100000 100000
extresist all
ext2spice format ngspice
ext2spice extresist on
ext2spice cthresh 0
ext2spice rthresh 0

ext2spice /work/__CELL__
