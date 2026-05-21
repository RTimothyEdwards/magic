# pcell.tcl — PCell generation test.
#
# Defines a parameterized cell proc and instantiates it with two
# different sizes to verify that Tcl proc definitions, Magic drawing
# commands, and GDS output all work end-to-end in the TCL variant.
#
# __TECH__ is substituted by pcell.js before execution.

tech load __TECH__

# PCell definition: a labelled metal1 rectangle of variable size.
proc make_rect {name width height} {
    cellname create $name
    box 0 0 $width $height
    paint m1
    save /work/$name
    gds write /work/$name
}

# Instantiate with two different sizes.
make_rect pcell_4x8  4  8
make_rect pcell_8x4  8  4
