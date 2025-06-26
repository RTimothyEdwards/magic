This is an AppImage that runs on all GNU/Linux platforms with:

* FUSE
    * This excludes non-privileged Docker containers unfortunately, unless pre-extracted.
* GLIBC 2.28+
* Cairo 1.15+

This AppImage build is based on EL8 (via AlmaLinux 8)

AlmaLinux 8 was first released on 20 March 2021, active support ends 31 May 2024,
security support ends 31 May 2029 (please see AlmaLinux bulletins for
up-to-date information).

# Version Info

See the AppImage main binary file naming, release tag information and AppInfo metadata
for the exact magic version inside the archive.  When starting AppImage by default the
Tcl console banner can also provide version information, also using the `version` Tcl
command.

* Based on AlmaLinux 8 (EL8)
* Tcl/Tk 8.6.16
* and Magic 8.x
* all default modules enabled (including all Display drivers cairo/X11/OpenGL)

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
* Docker
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

The information here provides an outline of what versions to expect from EL8
of the major dependencies, to assist you with a compatibility check with your
Linux distribution of choice.

The actual versions in our public releases can differ slightly inline with
the EL8 support compatibility and ABI versioning policies for the support
lifecycle of the distribution.

The most important items are the Direct Dependencies and the availabilty
of a suitable graphics DSO as per your '-d' choice.

Direct Runtime Dependencies (from /prefix/**):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.14          | glibc-2.28-251       |
| libz.so.1              | ZLIB_1.2.2          | zlib-1.2.11-25       |

Optional/Modular Runtime Dependencies (depending on graphics mode):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libcairo.so.2          |                     |                      |
| libcairo.so.2.11512.0  |                     | cairo-1.15.12-6      |
| libGL.so.1             |                     | libglvnd-glx-1:1.3.4-2 |
|                        |                     | mesa-libGL-23.1.4-4  |
| libGLU.so.1            |                     | mesa-libGLU-9.0.0-15 |

Transitive/Third-Party Runtime Dependencies (for information only):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.35          | glibc-2.28-251       |
| libstdc++.so.6         | GLIBCXX_3.4         | gcc-c++-8.5.0        |
| libstdc++.so.6         | CXXABI_1.3.9        | gcc-c++-8.5.0        |
| libgcc_s.so.1          | GCC_4.2.0           | libgcc-8.5.0-26      |
| libxml2.so.2           |                     | libxml2-2.9.7-19     |
| libpng16.so.16         | PNG16_0             | libpng-2:1.6.34-5    |
| liblzma.so.5           |                     | xz-libs-5.2.4-4      |
| libz.so.1              | ZLIB_1.2.9          | zlib-1.2.11-25       |
