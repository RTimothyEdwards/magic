This is an AppImage that runs on all GNU/Linux platforms with:

* FUSE
    * This excludes non-privileged Docker containers unfortunately, unless pre-extracted.
* GLIBC 2.39+
* Cairo 1.18+
* May require runtime CPU matching Linux ABI x86-64-v3 and newer (CPUs with SSE4.2/AVX2/BMI2/FMA via `lscpu`).

This AppImage build is based on EL10 (via AlmaLinux 10).

AlmaLinux 10 was first released on 27 May 2025.

# Version Info

See the AppImage main binary file naming, release tag information and AppInfo metadata
for the exact magic version inside the archive.  When starting AppImage by default the
Tcl console banner can also provide version information, also using the `version` Tcl
command.

# Build Info

* Based on AlmaLinux 10 (EL10)
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
* Docker 20+
* Git
* curl

The final build is then packaged into an AppImage using AppImageTool on the host machine.

# Build Instructions
`make`

# Installation Instructions
`make install`

# FAQ: Is my CPU supported ?

This is built with the standard x86_64 Linux ABI version for AlmaLinux 10.

Use the command `/lib64/ld-linux-x86-64.so.2 --help` to see which CPUs your
Linux distribtion supports (and your CPU) look for the word "supported".

# FAQ: The DSO versioning link dependencies?

The information here provides an outline of what versions to expect from EL10
of the major dependencies, to assist you in a compatibility check with your
Linux distribution of choice.

The actual versions in our public releases can differ slightly inline with
the EL10 support compatibility and ABI versioning policies for the support
lifecycle of the distribution.

The most important items are the Direct Dependencies and the availabilty
of a suitable graphics DSO as per your '-d' choice.

Direct Runtime Dependencies (from /prefix/**):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libc.so.6              | GLIBC_2.38          | glibc-2.39-37        |
| libz.so.1              | ZLIB_1.2.2          | zlib-ng-2.2.3-1      |
|                        |                     | zlib-ng-compat-2.2.3-1 |

Optional/Modular Runtime Dependencies (depending on graphics mode):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libcairo.so.2          |                     |                      |
| libcairo.so.2.11802.0  |                     | cairo-1.18.2-2       |
| libGL.so.1             |                     | libglvnd-glx-1:1.7.0-7 |
|                        |                     | mesa-libGL-24.2.8-2  |
| libGLU.so.1            |                     | mesa-libGLU-9.0.3-7  |

Transitive/Third-Party Runtime Dependencies (for information only):

| DSO Filename           | DSO Symbol Version  | Related Packages     |
| :--------------------- | :------------------ | :------------------- |
| libstdc++.so.6         | CXXABI_1.3.9        | gcc-c++-14.2.1-7     |
| libstdc++.so.6         | GLIBCXX_3.4         | gcc-c++-14.2.1-7     |
| libgcc_s.so.1          | GCC_4.2.0           | libgcc_s-14-20250110 |
| libxml2.so.2           | LIBXML2_2.6.0       | libxml2-2.12.5-5     |
| libpng16.so.16         | PNG16_0             | libpng-2:1.6.40-8    |
| liblzma.so.5           | XZ_5.0              | xz-devel-1:5.6.2-4   |
| libz.so.1              | ZLIB_1.2.9          | zlib-ng-2.2.3-1      |
|                        |                     | zlib-ng-compat-2.2.3-1 |
