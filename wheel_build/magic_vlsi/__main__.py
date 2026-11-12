import os
import sys
from pathlib import Path

__here__ = Path(__file__).resolve().parent
magic_bin = __here__ / "bin" / "magic"


def launch_magic():
    env = os.environ.copy()
    env["CAD_ROOT"] = str(__here__ / "lib")
    wish = __here__ / "bin" / "wish9.1"
    if wish.is_file():
        env["MAGIC_WISH"] = str(wish)
    env["TCL_LIBRARY"] = str(__here__ / "lib" / "tcl9.1")
    os.execle(magic_bin, "magic", *sys.argv[1:], env)


if __name__ == "__main__":
    launch_magic()
