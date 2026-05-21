magic::tech load __TECH__

proc make_rect {name width height} {
    magic::cellname create $name
    magic::box 0 0 $width $height
    magic::paint m1
    magic::save /work/$name
    magic::gds write /work/$name
}

make_rect pcell_4x8  4  8
make_rect pcell_8x4  8  4
