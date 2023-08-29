# Installing Magic on macOS (Tested on Big Sur)
## With Brew
Get [Homebrew](https://brew.sh).

```sh
brew install cairo tcl-tk python3
brew install --cask xquartz
./scripts/configure_mac
make database/database.h
make -j$(sysctl -n hw.ncpu)
make install # may need sudo depending on your setup
```

## Without Brew
Get [XQuartz](https://github.com/XQuartz/XQuartz)

### Build Tcl for X11

We are following the instructions from xschem (https://github.com/StefanSchippers/xschem/blob/master/README_MacOS.md). 

* Download Tcl from https://prdownloads.sourceforge.net/tcl/tcl8.6.10-src.tar.gz

We are using not `opt` but `opt2` so that this Tcl does not interfere with `tcl-tk` from HomeBrew.

```
./configure --prefix=/usr/local/opt2/tcl-tk  
make
make install
```

### Build Tk for X11

* Download Tk from https://prdownloads.sourceforge.net/tcl/tk8.6.10-src.tar.gz

```
./configure --prefix=/usr/local/opt2/tcl-tk \
--with-tcl=/usr/local/opt2/tcl-tk/lib --with-x \
--x-includes=/opt/X11/include --x-libraries=/opt/X11/lib  
make
make install
```

### Build magic

We need to provide this `tcl-tk` and suppress compilation errors.

```
./configure --with-tcl=/usr/local/opt2/tcl-tk/lib \
--with-tk=/usr/local/opt2/tcl-tk/lib \
--x-includes=/opt/X11/include \
--x-libraries=/opt/X11/lib \
CFLAGS=-Wno-error=implicit-function-declaration
make
make install
```
