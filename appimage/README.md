This is an AppImage that runs on all GNU/Linux platforms with:

* FUSE
    * This excludes non-privileged Docker containers unfortunately, unless pre-extracted.
* GLIBC 2.17+
* Cairo 1.15+
* Supports all Linux x86_64 CPUs

This AppImage build is based on EL7 (via CentOS 7)

CentOS 7 was first released on 07 July 2014 and went end-of-life on 30 June 2024.

# Version Info

See the AppImage main binary file naming, release tag information and AppInfo metadata
for the exact magic version inside the archive.  When starting AppImage by default the
Tcl console banner can also provide version information, also using the `version` Tcl
command.

# Build Info

* Based on CentOS 7 (EL7)
* Tcl/Tk 8.6.16
* and Magic 8.x
* all default modules enabled, but without OpenGL (includes Display drivers cairo/X11)

# FAQ: How to use

Download the *.AppImage file relevant to your platform and run:

```
chmod +x Magic-x86_64.AppImage
./Magic-x86_64.AppImage
```

Example startup with command line options:

```
./Magic-x86_64.AppImage -d XR -T scmos
```


# Building Requirements

* A reasonably recent GNU/Linux host
* GNU make
* Docker 20+
* Git
* curl

The final build is then packaged into an AppImage using AppImageTool on the host machine.

# Build Instructions
`make`

# Installation Instructions
`make install`

# FAQ: Is my CPU supported ?

Supports all x86_64 CPUs.  The Linux ABI in use is the original x86-64 ABI (v1).

Use the command `/lib64/ld-linux-x86-64.so.2 --help` to see which CPUs your
Linux distribtion supports (and your CPU) look for the word "supported".

# FAQ: The DSO versioning link dependencies?

The information here provides an outline of what versions to expect from EL7
of the major dependencies, to assist you in a compatibility check with your
Linux distribution of choice.

The actual versions in our public releases can differ slightly inline with
the EL7 support compatibility and ABI versioning policies for the support
lifecycle of the distribution.

The most important items are the Direct Dependencies and the availabilty
of a suitable graphics DSO as per your '-d' choice.

Direct Runtime Dependencies (from /prefix/**):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.14          | glibc-2.17-326       |
| libz.so.1              | ZLIB_1.2.2          | zlib-1.2.7-21        |

Optional/Modular Runtime Dependencies (depending on graphics mode):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libcairo.so.2          |                     |                      |
| libcairo.so.2.11512.0  |                     | cairo-1.15.12-4      |
| libGL.so.1             |                     |                      |
| libglvnd-glx-1:1.0.1-0 |                     | mesa-libGL-18.3.4-12 |
| libGLU.so.1            |                     | mesa-libGLU-9.0.0-4  |

Transitive/Third-Party Runtime Dependencies (for information only):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.35          | glibc-2.17-326       |
| libstdc++.so.6         | GLIBCXX_3.4         | gcc-4.8.5-44         |
| libstdc++.so.6         | CXXABI_1.3.9        | gcc-4.8.5-44         |
| libgcc_s.so.1          |                     |                      |
| libgcc_s-4.8.5-20150702.so.1 | GCC_4.2.0     | libgcc-4.8.5-44      |
| libxml2.so.2           | LIBXML2_2.6.0       | libxml2-2.9.1-6      |
| libpng15.so.15         |                     |                      |
| libpng15.so.15.13.0    | PNG16_0             | libpng-1:1.5.13-8    |
| liblzma.so.5           | XZ_5.0              | xz-libs-5.2.2-2      |
| libz.so.1              | ZLIB_1.2.9          | zlib-1.2.7-21        |
