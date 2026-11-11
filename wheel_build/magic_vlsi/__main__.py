import os
import sys
from pathlib import Path

__here__ = Path(__file__).resolve().parent
magic_bin = __here__ / "bin" / "magic"


def launch_magic():
    env = os.environ.copy()
    env["CAD_ROOT"] = __here__ / "lib"
    env["TCL_LIBRARY"] = __here__ / "lib" / "tcl8.6"
    os.execle(magic_bin, "magic", *sys.argv[1:], env)


if __name__ == "__main__":
    launch_magic()
