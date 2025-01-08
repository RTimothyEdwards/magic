# Installing Magic on macOS (Tested on Big Sur)
## With Brew
Get [Homebrew](https://brew.sh).

```sh
brew install cairo tcl-tk@8 python3 gnu-sed
brew install --cask xquartz
./scripts/configure_mac
# If you have both TCL8 and TCL9 installed you may need to verify which was selected.
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

Extract the Tcl sources and then go to the unix folder and execute the following commands::
```
./configure --prefix=/usr/local/opt2/tcl-tk  
make
make install
```

### Build Tk for X11

* Download Tk from https://prdownloads.sourceforge.net/tcl/tk8.6.10-src.tar.gz

Extract Tk source and then go to the unix folder:

NOTE: before running 'make' inspect the Makefile and ensure the LIB_RUNTIME_DIR is set as follows. Make the correction if not:
```
LIB_RUNTIME_DIR         = $(libdir)
```

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

## If facing issue with layout window not opening / XQuartz:
Make sure that the output of the following command is ```:0```.
```
echo $DISPLAY
```
if the above command doesn't display ```:0``` then add the following line in ```.zshrc```.
```
export PATH="/opt/X11/bin:$PATH"
```
Close & reopen terminal to load the path. Then set display manually to ```0``` by using the following command.
```
export DISPLAY=:0
```
Now  ```echo DISPLAY``` should give ```:0``` as output.