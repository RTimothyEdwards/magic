This is an AppImage that runs on all GNU/Linux platforms with:

* FUSE
    * This excludes non-privileged Docker containers unfortunately, unless pre-extracted.
* GLIBC 2.17+
* Cairo 1.8+

That's most Linux distributions released in 2016 or later.

# Build Info
A Dockerfile on CentOS 7 (needed for older glibc) image builds Tcl, Tk and Magic.

The final build is then packaged into an AppImage using AppImageTool on the host machine.

# Building Requirements
* A reasonably recent GNU/Linux host
* Docker 20+
* curl

# Build Instructions
`make`

# Installation Instructions
`make install`
