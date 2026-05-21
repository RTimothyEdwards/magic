magic::tech load __TECH__
magic::load /work/__CELL__
magic::extract path /work
magic::extract do resistance
magic::extract all
magic::select top cell
magic::extresist all
magic::ext2spice format ngspice
magic::ext2spice extresist on
magic::ext2spice cthresh 0
magic::ext2spice /work/__CELL__
