-------------------------------------------------------------------
A Tcl-wrapped version of magic
-------------------------------------------------------------------

This file contains details on the compilation, installation, and
execution of the Tcl interpreter-based version of Magic.

-------------------------------------------------------------------
Compiling and Installation
-------------------------------------------------------------------

Compiling and installing have been simplified, but are not exactly the
same as the "standard" compile and install procedure, so be sure to
read these instructions first.

1) Software prerequisites:

   You will need Tcl and Tk versions 8.2+.  Testing has been done on Tcl
   and Tk versions 8.3 and 8.4.  Some commands are only compatible with
   versions 8.2 and newer; these should be #ifdef'd sometime in the near
   future to make the program compatible with Tcl versions at least back
   to 8.0.  For use with the wrapper GUI, it is preferable to have BLT
   version 2.4 or newer to get the tree view of the cell hierarchy,
   although the presence of BLT is not mandatory.  For digital simulation,
   you should have IRSIM version 9.6 revision 1 or newer.  Presence of
   IRSIM is not mandatory.  Note that unlike magic versions 7.1 and older,
   the IRSIM module is not selected at compile-time;  in Tcl it is a
   run-time loadable module.

2) At the top level, do "./configure" and any other options (such as
   "--prefix=DIR" for non-default installation location).  "prefix"
   determines the value $CAD_ROOT, which is set to ${prefix}/lib,
   unless "--libdir=DIR" is set, in which case $CAD_ROOT is set to
   this value.  The "configure" script will determine
   if Tcl is on the system, and enable it if it appears to be usable.
   Note that if Tcl is installed in a non-standard location, it will
   be necessary to pass the option "--with-tcl=<DIR>" to "configure",
   indicating the directory where the file "tclConfig.sh" can be found.
   Likewise, the "configure" script attempts to find OpenGL include
   files and libraries on the system, and enables the option if it can
   find them.  Again, if in a nonstandard location, it is necessary to
   pass the option "--with-opengl=<DIR>" indicating the directory where
   OpenGL include files may be found.  It is assumed that the libraries
   are in a standard directory;  if not, it may be necessary to make
   symbolic links to them from a standard directory like /usr/X11R6/lib/.

3) Do "make".  This should complete the compilation of the Tcl-compatible
   shared library file "magic/tcl/tclmagic.so".

4) Do "make install".  This should place all the necessary executables,
   library files, etc., into the proper places.  The main executable is
   "magic" (a script).  Normally, it runs Tcl from a console window
   ("tkcon", at least for now).  With a command-line argument "-noconsole",
   it runs Tcl from the terminal (runs "magicexec", a stand-in for "wish").
   Original executables "ext2spice" and "ext2sim" have been changed to magic
   command-line commands.  They are available as separate packages to Tcl
   and are loaded automatically by the startup Tcl script.

5) Any other ".tcl" files (packages) for magic go in $CAD_ROOT/magic/tcl/.
   Magic's Tcl startup appends this directory to Tcl's search directory for
   Tcl/Tk executables.

6) Make sure that the default .magic script and any user .magic scripts do
   not have embedded control characters (see below in "Known Bugs").  The
   one installed by the distribution is correct; the warning applies to
   any legacy files.

-------------------------------------------------------------------
Running Tcl-based magic
-------------------------------------------------------------------

1) The startup script works out namespaces so that all magic commands
   become directly available from the command line.  A number of
   commands have to be specially handled because of conflicts with
   existing Tcl/Tk commands.  These are:  "flush", "clockwise",
   "array", "load", "label", and "grid".  From magic version 7.2
   revision 31, all of these commands can be run as either the
   original Tcl/Tk command or as the magic command, and the
   correct function will be called (because the syntax of the two
   forms is mutually exclusive).

   In cases such as starting magic from another application like
   irsim or netgen, all magic commands are prefixed by the 
   namespace "::magic::" to differentiate them from command names
   in other applications.

   Note that some commands (ones which are not often used) have
   simply been renamed.  The command "list" has been replaced (and
   its functionality extended) by commands "cellname" and "intance".
   Command "split" has been changed to "splitpaint" (which matches
   the "spliterase" command).

   Commands "source" and "send" are superceded by Tcl methods and
   no longer exist as their former selves in magic.  In particular,
   "send" (sends a magic command to a window) is superceded by the
   syntax of the window name followed by the magic command (see item
   4, below).  Because magic extends Tcl, the "source" command is
   equivalent to Tcl's "source" command).

   Commands for separate packages (namely, ext2sim and ext2spice) share
   the magic namespace:  magic::ext2sim and magic::ext2spice.
   The magic startup script automatically loads these packages and makes
   the commands available from the root namespace, i.e., "ext2sim" and
   "ext2spice".

2) Command entry works as follows:  Because the underlying Tcl interpreter
   is *supposed* to look and act like a normal Tcl interpreter, single-key
   macros are only allowed from inside windows.  The use of the ":" macro
   (long-command) is implemented as faithfully as allowed by the interpreter.
   When using the terminal as a console, it is not possible to enter a
   command partially in the terminal or window and complete it in the
   other.  Also, the status of the background DRC is not reflected in the
   cursor.  The "tkcon" console-based version has a cursor which reflects
   the DRC status, and commands may be entered partially or in full in
   either the layout or the console windows.  The cursor is "%" for normal
   operation, "]" for DRC-in-progress, and ":" for commands initiated by
   the long-command macro.  Note that due to inherent limitations of the 
   Tk "text" widget, the insert cursor is not displayed in the console when
   the cursor is in the layout window.  Queries to the user force focus
   into the console window;  this will probably be reimplemented in dialog
   boxes in the future.

3) Tcl-magic should work with "tclreadline".  The "tkcon" console-based
   version has its own working readline-like capability.  Note that
   internally defined completion functions like cellname completion are
   not available.

4) Window commands:  Note that in addition to the standard magic commands,
   the Tcl/Tk version makes it possible to direct commands to a particular
   window; e.g., ".magic2 paint m1" can be run from the command line in
   place of putting the cursor in window ".magic2" and executing the
   command "paint m1".  This allows any possible magic command to be run
   from a script.

   The deprecation of magic's "send" command has repercussions on window
   commands, particularly when using non-layout windows.  For example, the
   [non-TCL-based magic] command "send netlist shownet" is supposed to
   execute the "shownet" command in the context of the netlist window
   ("specialopen netlist").  However, under Tk, "netlist" is not a valid
   window path;  the correct command is to note the window name (usually
   something like ".magic2") and to use it to direct the command:
   ".magic2 shownet" will execute the command "shownet" in the context of
   window ".magic2".

5) Command tags:  A command called "tag" has been added with the following
   syntax:

	magic::tag <command_name> <tcl_procedure>

   This allows an arbitrary Tcl procedure to be associated with any
   magic command.  The usual reason to do this is to allow GUI functions
   to be updated in response to command-line entry or key macros in a
   window.  The method can also be used to re-organize Tcl output from
   a command, or send commands to another Tcl application in response
   to actions in magic.

   <command_name> is a simple command name without any namespace
   qualifier (e.g., "select" or "paint").  <tcl_procedure> is a string
   which may be either an inline procedure or a call to a defined Tcl
   procedure.  This string may contain certain embedded escapes which
   will be substituted prior to evaluation.  The escapes are:

	%W	  Full Tk pathname of the layout window
	%R	  Tcl result of <command_name>.  The result is
		  absorbed.
	%r	  Tcl result of <command_name>.  The result will be
		  returned to the calling function as usual.
	%0 .. %5  Arguments passed to <command_name>.
	%%	  A single percent character.

6) Windows can be generated as internal Tk windows instead of top-level
   windows by using the command

	magic::openwindow <cellname> <tk_window_path_name>.

   For example, the script:

	toplevel .layout
	magic::openwindow "(UNNAMED)" .layout.magic1
	pack .layout.magic1 -expand true -fill both
	bind .layout.magic11 <Enter> {focus %W}

   generates a new window inside a toplevel frame named "layout", which 
   can be customized.  In this case, the layout window does nothing except
   contain the magic window.  The fourth line ensures that the magic
   window can generate keyboard events.

   Generation of full-frame GUIs is facilitated by the "-n[owindow]"
   option, which brings magic up with the indicated graphics package but
   no actual window displayed.  It is up to the calling application to
   call "magic::openwindow" and arrange the geometry and packing of the
   windows.

7) Another method for generating windows is to use the
   "image create layer" method.  For example, the command
   'openwindow "(UNNAMED)" .magic2' creates a new Tk window which
   starts off unmapped.  Followed by "image create layer img_layout
   -name .magic2 -width 300 -height 300", an internal (offscreen)
   pixmap is created in place of the Tk window, and will be updated
   with changes to the layout just as any window would be.  The
   image "img_layout" can be used to drop the layout image into any Tk
   widget which accepts images (such as the canvas and button widgets).
   The syntax is "image create layer <image_name> -name <tk_window_path>
   -width <width> -height <height>".  Note that the Tk window is
   destroyed when the image is instanced;  this does not occur with the
   "image create" command, but when the image is first mapped to the
   screen.  Here's an example which generates a button containing the
   magic layout:

	openwindow "(UNNAMED)" .magic2
	image create layer img_layout -name .magic2
	toplevel .new
	button .new.b1 -image img_layout
	pack .new.b1

   Layout may be painted in the window using command-line commands.
   Entering the button with the cursor will refresh the layout view.

   IMPORTANT NOTE:  When magic is run with OpenGL graphics ("-d OGL" 
   command-line option), the implementation uses the off-screen
   rendering functions to draw layout onto pixmaps for the button
   image feature mentioned above.  There are a number of video card
   drivers that do not correctly implement these functions, and will
   crash magic if the toolbar is used.

8) Some commands have been altered in the Tcl environment to return
   results to the interpreter rather than simply print them as
   messages on stdout.  One example is "getnode", which returns the
   string value of the selected node:

	set n [.magic1 getnode]

   Others implemented:  "path sys|cell|search" returns a string
   containing the indicated path.  "box values" returns the box
   coordinates.  "windowcaption" returns the window title caption.
   "cellname" and "instance", with the option "list", return their
   results as Tcl lists.

9) Environment variables and Tcl global variables:  A few environment
   variables are queried for backward compatibility;  eventually all of
   these should be either replaced or duplicated by Tcl global variables.
   Currently, "$CAD_ROOT" is a duplicated variable.  It can be set in the
   UNIX shell environment prior to running Tcl-magic, and the Tcl variable
   "$CAD_ROOT" will be duplicated from it.  Otherwise, the Tcl variable
   "$CAD_ROOT" will be set to the value determined by the "make config"
   process.  The use of "~cad" is completely deprecated.

   The new "make" process encourages the use of standard directory
   trees such as "/usr/local/lib" for CAD_ROOT, rather than the oddball
   "/home/cad/lib".  The new system works well with user-installed versions
   having no access to root filesystem directories for installation.

10) Modes of operation:  Tcl-based magic runs in all graphics modes,
   including 8-bit PseudoColor, 16- and 24-bit TrueColor, and OpenGL.
   When using a console and/or GUI wrapper in PseudoColor mode, magic
   redefines all of the Tk colors in the console and the wrapper frame
   so that they match the magic color scheme.  The console gets the
   magic layout colormap so that one can move between the layout
   windows and the console without altering the colormap of either one.

11) A "wrapper" GUI is in "wrapper.tcl" and is normally present;
   it can be disabled with the command "magic -nowrapper" (or
   "magic -now").  This wrapper removes magic's own title bar and
   frame and replaces them with a frame managed by Tcl/Tk.

   The features of the wrapper are:

	A) Scrollbars are replaced by Tk scrollbars, but the functions
	   are essentially the same.
	B) The caption is replaced by a Tk widget, with the same
	   function.
	C) The box coordinates are displayed at the right-hand side
	   of the title bar.
	D) A menubar is provided at the top left-hand corner.  This
	   has entries "File" and "Options".  "File" takes
	   care of many read and write functions (including CIF and
	   GDS).  Options are:

	       a) "Toolbar" enables a toolbar to the right of the
		layout.  This toolbar contains all of the layer types
		in the technology file.  Entering the button displays
		the name of the layer type in the title bar.  Clicking
		with mouse button 2 or keystroke "p" paints the layer
		type in the box on the layout window.  Keystroke "e"
		erases the layer type in the box.  Clicking with mouse
		button 3 hides the layer type in the layout window, and
		clicking mouse button 1 restores it.  Keystroke "s"
		selects all paint of the layer type within the cursor
		box.
		
	      b) "Cell Manager" pops up a display showing the hierarchy
		of layout cells in memory.  This option is only available
		if the BLT package for Tcl/Tk has been installed on the
		system.  When BLT is installed, the option will simply
		appear.

	      c) "Tech Manager" pops up a display showing various
		properties of the currently loaded technology and provides
		a convenient way to change technologies or CIF read/write
		and extraction styles.

	      d) "Pinout List" is a simple list widget that can search
		a layout for layer type "pad", and enumerates all of
		the pad instances found in the layout.  This aids in
		the task of generating the pin names in the final
		stages of a chip design.

12) Extraction:

   The Tcl version of magic makes use of the interpreter variable
   space to work around some former deficiencies of the extractor.
   Power and ground nodes can be explicitly defined by setting
   variables $VDD and $GND in the Tcl global variable space
   (e.g, "::set VDD VDD" and "::set GND GND").  If these are not
   defined, then magic reverts to its original behavior of looking
   for node names ending with "!".  The Tcl variable names (or any
   Tcl variable names) can be added to the techfile definition of
   a device's default substrate node name (include the "$" in front
   of the variable).  Normally, this would use the two built-in
   variables $GND and $VDD.  These variables should be set in the
   ".magic" file of each project directory, so that the default	
   device substrate connections will match the power and ground
   definitions in a layout.  If $GND and $VDD are defined but do
   not match any node names in the layout, a warning is flagged.

13) IRSIM simulation:

   Starting with revision 14, magic 7.2 supports interactive digital
   simulation with IRSIM.  It is necessary to compile IRSIM version
   9.6 or better with Tcl enabled, and it is necessary to have the Tcl
   package "BLT" for the graphing window to work.  IRSIM version
   9.6 has a "make" procedure which is exactly like magic's.  It is
   preferable to set CAD_ROOT to the same value for both programs
   when doing "make config", so magic can find and automatically
   load the IRSIM package when it needs to.

   The Tcl procedure "startirsim" loads and runs IRSIM.  Since both
   programs get loaded into the same interpreter, commands for both
   magic and IRSIM can be executed on the command line.  When IRSIM
   is started with magic running, a number of commands are added to
   the interpreter which communicate between the two packages.
   Examples are "watchnode", "watchtime", and "drawnode".  See the
   "README" file in the IRSIM source for more information on running
   IRSIM under magic and an explanation of the full set of commands
   available.

------------------------------------------------------------------
Tcl-specific bugs and annoyances:
-------------------------------------------------------------------

1) The Tcl prompt ("% ") appears at all times when the terminal is used
   for command entry.  The desired behavior is that the magic prompt
   ">" (or "]" while DRC is pending) should appear when the cursor is
   in a window, indicating that the input is accepting keyboard macros.
   When outside of the window or after using the macro ":" to get to the
   command-line entry from magic, the Tcl "%" prompt appears, indicating
   that all valid Tcl commands may be entered at the prompt.

   Because it is not possible to detect if a partial Tcl command is on
   the command line, it is only possible to enter part of a command in
   the terminal window and complete the command in the magic window if
   ":" is typed in the magic window to redirect keyboard input to the
   terminal.

   The "tkcon" console-based version does not have this limitation and
   operates more or less like the original magic prompt.

2) Magic's original ".magic" file has control characters embedded.  This
   is the only magic command syntax which is illegal Tcl syntax.  However,
   magic 7.1 allows control characters to be written either as "^X" (two
   characters, with a caret) or "Control_X".  If the original .magic is
   used (i.e., Tcl-magic is compiled and run, but not installed),
   the embedded controls should be changed to printable ASCII format.

3) There are routines which handle 8-bit pseudocolor mode by mapping
   tk colors to magic colors by the magic "short name" as listed in
   the dstyle file.  This generally works well, except: 1) if the
   cursor is inside a layout window when it comes up, the colormap
   gets installed permanently; the window manager no longer reverts
   to the original colormap when leaving the window.  2) there is
   currently no routine to revert the console back to its original
   colors and revert to the original colormap.  3) the colors could
   use a bit of tweaking.

------------------------------------------------------------------
