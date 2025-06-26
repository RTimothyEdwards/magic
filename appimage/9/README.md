This is an AppImage that runs on all GNU/Linux platforms with:

* FUSE
    * This excludes non-privileged Docker containers unfortunately, unless pre-extracted.
* GLIBC 2.34+
* Cairo 1.17+
* May require runtime CPU matching Linux ABI x86-64-v2 and newer (CPUs with SSE4.2/CX16 via `lscpu`).

This AppImage build is based on EL9 (via AlmaLinux 9)

AlmaLinux 9 was first released on 26 May 2022, full support ends 31 May 2027,
maintenance support ends 31 May 2032 (please see AlmaLinux bulletins for
up-to-date information).

# Version Info

See the AppImage main binary file naming, release tag information and AppInfo metadata
for the exact magic version inside the archive.  When starting AppImage by default the
Tcl console banner can also provide version information, also using the `version` Tcl
command.

# Build Info

* Based on AlmaLinux 9 (EL9)
* Tcl/Tk 9.0.1
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

This is built with the standard x86_64 Linux ABI version for AlmaLinux 9.

Use the command `/lib64/ld-linux-x86-64.so.2 --help` to see which CPUs your
Linux distribtion supports (and your CPU) look for the word "supported".

# FAQ: The DSO versioning link dependencies?

The information here provides an outline of what versions to expect from EL9
of the major dependencies, to assist you in a compatibility check with your
Linux distribution of choice.

The actual versions in our public releases can differ slightly inline with
the EL9 support compatibility and ABI versioning policies for the support
lifecycle of the distribution.

The most important items are the Direct Dependencies and the availabilty
of a suitable graphics DSO as per your '-d' choice.

Direct Runtime Dependencies (from /prefix/**):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.34          | glibc-2.34-168       |
| libz.so.1              | ZLIB_1.2.2          | zlib-1.2.11-40       |

Optional/Modular Runtime Dependencies (depending on graphics mode):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libcairo.so.2          |                     |                      |
| libcairo.so.2.11704.0  |                     | cairo-1.17.4-7       |
| libGL.so.1             |                     | libglvnd-glx-1:1.3.4 |
|                        |                     | mesa-libGL-24.2.8-2  |
| libGLU.so.1            |                     | mesa-libGLU-9.0.1    |

Transitive/Third-Party Runtime Dependencies (for information only):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.35          | glibc-2.34-168       |
| libstdc++.so.6         | CXXABI_1.3.9        | gcc-c++-11.5.0-5     |
| libstdc++.so.6         | GLIBCXX_3.4         | gcc-c++-11.5.0-5     |
| libgcc_s.so.1          | GCC_4.2.0           | libgcc-11.5.0-2      |
| libxml2.so.2           | LIBXML2_2.6.0       | libxml2-2.9.13-9     |
| libpng16.so.16         | PNG16_0             | ibpng-2:1.6.37-12    |
| liblzma.so.5           | XZ_5.0              | xz-libs-5.2.5-8      |
| libz.so.1              | ZLIB_1.2.9          | zlib-1.2.11-40       |
