![Continuous Integration](https://github.com/RTimothyEdwards/magic/actions/workflows/main.yml/badge.svg)

# MAGIC


1. General Information:
---------------------------------
   Use your World Wide Web browser to read:

	http://opencircuitdesign.com/magic/
	http://vlsi.csl.cornell.edu/magic/
	http://www.research.digital.com/wrl/magic/magic.html

   Primary documentation is on the opencircuitdesign.com website under
   the "Documentation" link.

   The current development versions of magic are maintained by Tim Edwards
   <tim@opencircuitdesign.com> and the current distribution version is
   maintained by Rajit Manohar <rajit@csl.cornell.edu>.  Please let us
   know of any problems/bugs you find.  Development of versions 7.2 and
   newer is generously funded by MultiGiG, Inc.

2. Compilation and Installation:
---------------------------------
   See the file "INSTALL" in this directory.

3. Version 8.3 Release Notes:
---------------------------------

   During the course of version 8.2, magic moved to a git-oriented
   development.  There are no longer "stable" and "distribution"
   versions.  There is only the git repo with various development
   branches.

   First release contains the "bplane" implementation for the cell
   plane pulled from the open source code for micromagic.  This is
   much more efficient than using the corner-stitched tile plane
   for cells, and speeds up a number of methods, such as extraction,
   by a factor of 3 to 5 or so, depending on the amount of hierarchy
   in the design.

4. Version 8.2 Release Notes:
---------------------------------

   As of the release of version 8.2, Version 8.1 is now the new stable
   distribution, and 8.2 is the development distribution.  First
   development push over 8.1 is to add a Cairo (2D hardware-accelerated
   graphics) interface ("magic -d CAIRO" or "magic -d XR").

   October, 2017:  DRC changes correctly handle DRC errors in a child
   cell that are effectively corrected by geometry in the parent cell.

   March, 2018: Added better version handling of subcells.  "Use"
   records contain a path (relative or absolute) to the library used
   by the subcell, and this path is honored as long as it can be
   found.

   April, 2018:  Added the "plow" function back to the level of
   capability it originally had.  This does not include handling of
   magic extensions since version 7, including non-manhattan geometry,
   stacked contacts, and DRC rule extensions.

   Extended the extraction method to allow multiple extracted device
   types per magic layer, depending on the surrounding context
   (connecting layers, substrate, identifier layers, etc.).

   Corrected the "extresist" method for non-FET devices, although it
   continues to need fundamental work to remove its dependence on the
   ".sim" format files.

5. Version 8.1 Release Notes:
---------------------------------

   As of the release of version 8.1, Version 8.0 is now the new stable
   distribution, and 8.1 is the development distribution.

   What's new in 8.1:
   ------------------
   1) Substantially revised substrate handling allows for "soft
      connectivity" detection, and resolves (finally) the problem of
      the hack handling of nMOS bulk connections using a default
      name without any understanding of a substrate node and
      connectivity.

6. Version 8.0 Release Notes:
---------------------------------

   As of the release of version 8.0, Version 7.5 is now the new stable
   distribution, and 8.0 is the development distribution.

   What's new in 8.0:
   ------------------
   1) Vector outline fonts with ability to control font, scale, rotation,
      and offset.  Public-license outline fonts provided by the freefont
      project.

   2) Hierarchical SPICE output for LVS.

   3) New cifoutput operators including "net" and "maxrect", specifically
      for using with the "cif paint" command.

   4) DRC method for handling via rules specifying overlap on two
      opposite sides but not on the others.

   5) Improved DRC method that ignores errors in subcells that are
      masked by the parent cell.

   6) Improved cell manager based on methods available in Tk 8.5

   7) New extraction method "msubcircuit" with methods for specifying
      parameter names for source/drain area and perimeter.

7. Version 7.5 Release Notes:
---------------------------------

   Version 7.5 is the development branch.  Version 7.5.0 is the same as
   7.4.2, which is essentially the same as 7.3.123, plus some
   documentation updates.  Intended development is as follows:

   What's new in 7.5:
   ------------------
   1) Use a finely spaced grid for the database, but keep the concept
      of "lambda" for layout.  Keep backwards compatibility, and resolve
      issues with layout that does not work well on the lambda grid.
      Implemented in 7.5.1 by allowing a DRC "scalefactor" line,
      which declares that all DRC rules are in units of (lambda /
      scalefactor).  Rules "in use" are scaled to lambda and rounded to
      the nearest integer.  The original value is retained, however, so
      that any call to "scalegrid" will recompute the DRC distances based
      on the current internal grid.  Thus, we can define DRC rules in
      fractional lambda and therefore match vendor DRC rule distances
      while still maintaining magic's concept of "lambda".  This means
      that users working entirely within magic have scalable CMOS rules,
      but if a "vendor cell" (3rd party GDS or CIF) is loaded, the DRC
      rules will be correct with respect to it.
   2) Multiple DRC styles allowed in the technology file.
   3) Memory-mapped tile allocation using the mmap() function.
   4) Layer and cell instance locking
   5) Euclidean-distance measure on "cif grow" operator.
   6) "cif paint" command to automatically manipulate the database
      paint using "cifoutput" rulesets.
   7) New contact-cut generation algorithm.
   8) Added the ability to define and extract MOS devices with
      asymmetric source and drain.
   9) Added extraction devices "rsubcircuit" and "subcircuit" to
      produce subcircuit records in SPICE output, with a method to
      define parameters to be passed to the subcircuit.
  10) Added resistor corner scaling (i.e., the resistance of a
      material at a corner can be set as a fraction of the resistance
      of the same material on a straight path).
  11) Updated the interactive maze router, fixing many bugs, and adding
      many enhancements, including a maze router GUI that can be used
      to aid in interactively routing an entire netlist, or performing
      a verification of a netlist against the layout.
  12) "gridlimit" keyword in the cifoutput section to prevent magic
      from generating geometry beyond a specific resolution.
  13) Added the ability to specify all units in the extract section in
      microns, and added a simplified method for specifying standard
      parasitic capacitance extraction rules.
  14) "gds merge true" option to generate polygons in the GDS output
      instead of tiles.  This creates *much* smaller output files at
      the expense of processing time.
  15) New "contact" function to automatically contact two layers at
      an intersection.

   See the online release notes for a more thorough list of features.

8. Version 7.4 Release Notes:
---------------------------------

   Version 7.4 is the new stable distribution version of magic.
   Apart from changes to the release notes, it is identical to
   the last revision (123) of development version 7.3.  Revisions
   of 7.4 will be made as necessary to fix bugs in the code.  All
   new additions and major changes will be done to the new
   development distribution, version 7.5.  Therefore there will
   not be a "What's new in 7.4" section, as there is not supposed
   to be anything new in version 7.4.

9. Version 7.3 Release Notes:
---------------------------------

   Magic release 7.3 incorporates a stacked contact model which is,
   for the most part, backwardly compatible with Magic releases
   7.2 and earlier.  Information about this developmental release
   can be found at:

	http://opencircuitdesign.com/magic/magic7_3.html


   What's new in 7.3:
   ------------------
   Provided by Tim Edwards (MultiGiG, Inc.):
         1) Stacked contact model allowing arbitrary stacking of
	    contact types.
         2) A separate "undo/redo" method for network selection, to
	    remove the memory overhead associated with selecting and
	    unselecting large networks.  Also removes some time overhead
	    as well, especially when unselecting networks.
         3) Much improved "plot pnm" function.
         4) Improved transistor and resistor extraction.
	 5) LEF format reader; improved LEF/DEF input/output handling
	 6) New style and colormap file formats
	 7) Vendor GDS read/write capability
         8) "wire segment" drawing function
	 9) Handling of path records in CIF and GDS input
	10) Handling of cell scaling in GDS input
	11) Pi-network device extraction for resistors
	12) Option to write contacts as cell arrays in GDS output
	13) New "widespacing" and "maxwidth" DRC algorithms.
	14) "polygon" command
	15) New cifoutput operator "bloat-all"
	16) Backing-store for 24-bit and OpenGL graphics
	17) New "pick" tool for interactive selection move and copy
	18) New interactive "wire" tool
	19) Crosshair
	20) New cifoutput operator "slots"
	21) New fcntl-based file locking mechanism
	22) "angstroms" units supported in cifinput/cifoutput
	23) Non-Manhattan device extraction support
	24) New "feedback" mechanism
	25) Proper support for > 32 planes (up to 64)
	26) Fixed array interaction CIF/GDS generation
	27) Added executable "magicdnull" for streamlined batch-mode use
	28) New method for crash backups, including restore with "magic -r"
        29) A number of other technology file additions and enhancements

10. Version 7.2 Release Notes:
---------------------------------

   Magic release 7.2 incorporates the capability to run magic from the Tcl
   interpreter with graphics handled by Tk.  Instructions for compiling
   and installing this version are in README.Tcl.  Information about
   this release can be found at:

	http://opencircuitdesign.com/magic/magic7_2.html

   What's new in 7.2:
   ------------------
   Provided by Tim Edwards (MultiGiG, Inc., and JHU Applied Physics Lab):

       1) Tcl interpreter extension option
       2) Cygwin compile option
       3) Memory resources cleaned up
       4) GUI interface to Tcl version of Magic
       5) Readline update to version 4.3
       6) OpenGL fixes and refinements
       7) Nonmanhattan geometry fixes and extensions
       8) Threaded graphics in non-Tcl environments
       9) Inductance extraction
      10) CIF and GDS input/output support for deep submicron technologies
      11) Different internal and lambda grids, including automatic or
	  induced ("scalegrid" command) grid subdivision and expansion.
	  "snap" and "grid" functions and extensions aid layout when
	  lambda and internal units differ.
      12) Removed commands "list", "listall", "parent", and "child",
	  replacing them with the more general-purpose "cellname"
	  and "instance" commands.
      13) Added command "tech", and re-loadable technologies.
      14) Revamped the "dstyle" files and updated the dstyle version
      15) Added "element" types for layout annotation.
      16) Extended extract section of techfile to include "device"
	  keyword and devices "mosfet", "bjt", "capacitor", and "resistor".
	  New model resistor and mosfet use width/length instead of area/
	  perimeter.
      17) Added 3D rendering window invoked by command "specialopen wind3d",
	  for the Tcl version compiled with OpenGL graphics.
      18) Added "height" keyword to tech file for height/thickness values
      19) Added "windowname" command for managing multiple wrapper windows
	  under Tcl.
      20) Added extraction extension for annular (ring) MOSFETs.
      21) Added "widespacing" DRC rule.
      22) Added GNU autoconf compile
      23) New command "property" for setting key:value pair properties
	  in cell definitions that can be interpreted by other routines
	  (such as LEF/DEF).
      24) General-purpose subcircuit method using the "port" command to
	  declare a cell to be a subcircuit and to mark the position and
	  orientation of connections into the subcell.  This replaces a
	  method previously built into version 7.2 using a "subcircuit"
	  layer; that method is now considered obsolete.
      25) LEF and DEF format readers, LEF format writer.
      26) Improved techfile format with asterisk-notation and DRC
	  "surround", "overhang", and "rect_only" statements.

11. Version 7.1 Release Notes:
---------------------------------

   Magic release 7.1 consolidates all known patches/features
   to magic version 6.5.x, and contains additional features that have
   been added since the consolidation.  Information about this release
   is available at the magic web site:

	http://vlsi.cornell.edu/magic/


   What's new in 7.1:
   ------------------
   Provided by Rajit Manohar (Cornell University) (formerly version 7.0):
       1) Implementation of "scheme" (a subset of lisp), a powerful method
	  of generating complex functions.
       2) Using CVS to facilitate source code development from multiple sites.
       3) New commands using scheme:  Too many to mention here;  see the
	  tutorials in doc/tutscm* for explanations.  Functions themselves
	  are defined in ${CAD_ROOT}/magic/scm/*.scm
       4) Overhauled the readline interface. See doc/textfiles/readline.txt for
          details.
       5) Changed tons of stuff about the build environment:
            - the include paths in all files is now canonical.
            - redid the make process and rewrote all Makefiles.
            - tons of other small things that hopefully make the build process
              nicer.

12. Releases prior to version 7:
---------------------------------

   What's new in 6.5.2:
   --------------------
   Provided by R. Timothy Edwards (Johns Hopkins Applied Physics Laboratory):
        1) Support for OpenGL 
           Look at doc/open_gl.txt
        2) Minor update to :config for selection of multiple graphics
           interfaces.
        3) Updates to dstyle and cmap files
        4) Always do a check to see if there is only one active layout
           window:  There's no need to annoy the user with complaints
           of "Put the cursor in a layout window" when there is no  
           possibility of confusion about the matter.
    
   Provided by Philippe Pouliquen (Johns Hopkins University):
        5) "readline" command line editing capability
        6) Macro extensions for X11 (see doc/macro_extension.txt)
        7) Better handling of filenames on the UNIX command-line (multiple
           filenames allowed, ".mag" extension not required).
        8) New commands:  "child", "parent", "down", "xload", "list", "listall",
           "listtop", "shell", "imacro".
        9) Command alterations: "box [+|-][dir]", "select visible", area of box
           included in "box" command.
       10) Updated .magic macro file (source in magic/proto.magic, install in
           ${CAD_ROOT}/magic/sys/.magic) (see doc/default_macros.txt).

   What's new in 6.5.1:
   --------------------
	1) Support for true-color displays (courtesy of Michael Godfrey)
	   Look into doc/hires-color.txt
	2) Minor updates in ext2sim, ext2spice

   What's new in 6.5:
   -----------------
        1) Bug fixes in the extractor propagation of attributes (SU)

        2) New version of ext2sim ext2spice with support for hspice, spice2, 
           and spice3 (SU)

        3) Integration of the port to SUN's Solaris OS (MIT)

        4) Port to FreeBSD2.x. Thanks to John Wehle (john@jwlab.feith.com)

	5) Integration of part of the DEC/WRL code fragments into the drc
	   code. Since the code is not completely trustworthy the fragments 
	   are ifdef'd so if need be the drc will behave exactly as the old one.
	   (you just need to change the #define DRC_EXTENSIONS in drc/drc.h
	    to do that).  For a description of the extensions look into 
	    doc/tutwrl1.ps (DEC/WRL)
	
	6) Integration of some patches in to the CIF code that introduce:
	    (i)  A new cif operation squares-grid which generates contacts 
		 on grid.
            (ii) A new cif layer command min-width is added so that generated
		 layers have the correct min drc width when magic expands
		 layers in the hierarchy (like it does with wells).
		 

   Magic-6.5 crashes if compiled with gcc in Solaris2.x/SunOS5.x (curiously enough 
   it does not have that problem if compiled with Sun's cc compiler). To get 
   around that you need to set the flag -DUSE_SYSTEM_MALLOC flag in misc/DFLAGS 
   after you run make :config. The error has to do with allignment of doubles 
   and an alternative way to get rid of it is to change extract/extractInt.h 
   so that CapValue is float instead of double.  Nevertheless the first method 
   is recomended.

   What's new in 6.4:
   ------------------
   This release, magic 6.4, contains the following modifications:

	1) A number of bug fixes from the 6.3 notes.

	2) A version numbering facility for tech files.  Please add a new 
	   section to each tech file after the "tech" section, following 
	   this example:

		version
		    version 2.0.3
		    description "MOSIS CMOS 0.13u Nano-nano technology."
		end
		    
	   Older versions of magic will complain about the new section, but
	   no harm will be done.

	4) Various comments describing dates and versions, including the
	   above tech file information, are not written to the CIF file.

	3) Support for patches and versioning:  A new command called "version"
	   lists out the version number and patches that are installed.
	   A header file called patchlevel.h keeps track of a PATCHLEVEL 
	   integer and a string in patchlevel.c keeps track of the names of
	   each installed patch.  When posting patches to the net please be
	   sure your patch updates variables.  See the files for details.

	4) Ports to Alpha AXP OSF/1, SGI/IRIX (courtesy of Stefanos 
	   Sidiropoulos) and Linux (courtesy of Harold Levy).

	5) A change in the extractor algorithm to provide shielding for
	   perimeter capacitances. Also a change in ext2sim to maintain
	   information about the area and perimeter of diffusion and
	   the bulk connection of the fets (written by Stefanos
	   Sidiropoulos).
