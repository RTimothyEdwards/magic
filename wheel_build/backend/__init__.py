# Adapted from Silisizer Wheels
#
# Copyright (C) 2026 Silimate Inc.
#
# Written by Mohamed Gaber <me@donn.website>
#
# Adapted from Yosys
#
# Copyright (C) 2026 Catherine <whitequark@whitequark.org>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
import hashlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
import urllib.request
from collections.abc import Iterable
from email.message import EmailMessage
from email.policy import EmailPolicy

from wheel.wheelfile import WheelFile

PROJECT_NAME = "magic_vlsi"
with open("VERSION", encoding="utf8") as f:
    PROJECT_VERSION = f.read().strip()
DIST_NAME = f"{PROJECT_NAME}-{PROJECT_VERSION}"

PLATFORM_TAG_RAW = sysconfig.get_platform()
PLATFORM_TAG = (
    PLATFORM_TAG_RAW.lower().replace("-", "_").replace(".", "_").replace(" ", "_")
)
COMPAT_TAG = f"py3-none-{PLATFORM_TAG}"

# python uses ENTRY_POINTS in metadata to synthesize entries in ./venv/bin
ENTRY_POINTS = f"""
[console_scripts]
magic = {PROJECT_NAME}.__main__:launch_magic
"""

TCL_URL = "https://github.com/tcltk/tcl/archive/refs/tags/core-8-6-16.tar.gz"
TCL_SHA256 = "05c061cca79a17efc61ef7c1cd873227a02a7c1fdc114aa6a4ecdd33ecf167f4"
TK_URL = "https://github.com/tcltk/tk/archive/refs/tags/core-8-6-16.tar.gz"
TK_SHA256 = "8cef8214ce158ca62b05bc9e714d2db90aa37eb478679a22135b51386879fda6"


def build_sdist(sdist_dir, config_settings=None):
    sdist_filename = f"{DIST_NAME}.tar.gz"

    with tarfile.open(
        pathlib.Path(sdist_dir) / sdist_filename,
        "w:gz",
        format=tarfile.PAX_FORMAT,
    ) as sdist:
        version_bytes = PROJECT_VERSION.encode("ascii")
        version_tarinfo = tarfile.TarInfo(f"{DIST_NAME}/SDIST_VERSION")
        version_tarinfo.size = len(version_bytes)
        sdist.addfile(version_tarinfo, io.BytesIO(version_bytes))

        def exclude_build(entry):
            name = os.path.basename(entry.name)
            if name in (
                ".git",
                ".github",
                ".cache",
                "build",
                "dist",
                "venv",
                ".venv",
                "test",
                "__pycache__",
            ):
                return
            if name.endswith((".whl", ".tgz", ".tar.gz")):
                return
            return entry

        sdist.add(os.getcwd(), arcname=DIST_NAME, filter=exclude_build)

    return sdist_filename


def make_message(headers: Iterable[tuple[str, str]], payload: str | None = None):
    """
    converts a set of python tuples and an optional payload in a manner
    consistent with
    https://packaging.python.org/en/latest/specifications/core-metadata/#core-metadata
    """
    msg = EmailMessage(policy=EmailPolicy(max_line_length=0))
    for name, value in headers:
        if isinstance(value, list):
            for value_part in value:
                msg[name] = value_part
        else:
            msg[name] = value
    if payload:
        msg.set_payload(payload)
    return bytes(msg)


def get_metadata_files():
    """
    (see https://packaging.python.org/en/latest/specifications/recording-installed-packages/)
    """
    with open("README.md", "rb") as readme:
        long_description = readme.read()

    return {
        "WHEEL": make_message(
            [
                ("Wheel-Version", "1.0"),
                ("Generator", "Magic Python build backend"),
                ("Root-Is-Purelib", "false"),
                ("Tag", [COMPAT_TAG]),
            ]
        ),
        "METADATA": make_message(
            [
                ("Metadata-Version", "2.4"),
                ("Name", PROJECT_NAME),
                ("Version", PROJECT_VERSION),
                ("Summary", "A venerable VLSI layout tool"),
                ("Description-Content-Type", "text/markdown"),
                ("Classifier", "Programming Language :: Python :: 3"),
                ("Requires-Python", ">=3.10"),
                ("License", "MIT"),
            ],
            long_description,
        ),
        "entry_points.txt": ENTRY_POINTS.encode("utf8"),
    }


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    """
    top-level function (called by pip during wheel build)

    generates dist-info
    """
    os.mkdir(f"{metadata_directory}/{DIST_NAME}.dist-info")

    for filename, contents in get_metadata_files().items():
        with open(f"{metadata_directory}/{DIST_NAME}.dist-info/{filename}", "wb") as f:
            f.write(contents)

    return f"{DIST_NAME}.dist-info"


def _download_hashed(url, dest, sha256_digest):
    urllib.request.urlretrieve(url, dest)
    with open(dest, "rb") as f:
        buffer = f.read()
        sha256 = hashlib.sha256()
        sha256.update(buffer)
        got = sha256.hexdigest()
        if sha256_digest != got:
            raise RuntimeError(f"upstream hash for {url} source changed: {got}")


def _ensure_tcltk(d):
    _download_hashed(TCL_URL, d / "tcl.tar.gz", TCL_SHA256)
    _download_hashed(TK_URL, d / "tk.tar.gz", TK_SHA256)

    for current in ("tcl", "tk"):
        with tarfile.open(d / f"{current}.tar.gz", mode="r:gz") as tf:
            tf.extractall(d / f"{current}-src")

        src_dir = next((d / f"{current}-src").glob("*"))
        subprocess.check_call(
            ["./unix/configure", f"--prefix={d}", "--without-keysymdefdir"],
            cwd=src_dir,
        )
        subprocess.check_call(
            ["make", f"-j{os.cpu_count()}"],
            cwd=src_dir,
        )
        subprocess.check_call(
            ["make", "install"],
            cwd=src_dir,
        )
        if current == "tk":
            subprocess.check_call(
                ["make", "install-private-headers"],
                cwd=src_dir,
            )


def build_wheel(wheel_dir, config_settings=None, metadata_directory=None):
    """
    top-level function (called by wheel build)

    builds silisizer and creates python version-agnostic wheel
    """
    wheel_filename = f"{DIST_NAME}-{COMPAT_TAG}.whl"

    with WheelFile(pathlib.Path(wheel_dir) / wheel_filename, "w") as wheel:
        # write metadata
        for filename, contents in get_metadata_files().items():
            wheel.writestr(f"{DIST_NAME}.dist-info/{filename}", contents)

        # build in temporary directory
        with tempfile.TemporaryDirectory(f".{PROJECT_NAME}-build", "w") as d_str:
            d = pathlib.Path(d_str)

            _ensure_tcltk(d)

            subprocess.check_call(
                [
                    "./configure",
                    f"--prefix={d}",
                    f"--with-tcl={d}/lib",
                    f"--with-tk={d}/lib",
                    "--without-cairo",
                    "--disable-readline",
                    "CFLAGS=-O0 -g -std=gnu99 -Wno-deprecated-non-prototype -Wno-implicit-function-declaration -Wno-parentheses -Wno-macro-redefined -Wdeprecated-declarations",
                ]
            )
            subprocess.check_call(["make", f"-j{os.cpu_count()}"])
            subprocess.check_call(["make", "install"])

            # patch magicdnull
            for binary_to_patch in (
                d / "bin" / "wish8.6",
                d / "lib" / "magic" / "tcl" / "magicdnull",
            ):
                for lib in ("tcl8.6", "tk8.6"):
                    if sys.platform == "darwin":
                        lib_path = d / "lib" / f"lib{lib}.dylib"
                        lib_relpath = lib_path.relative_to(
                            binary_to_patch.parent, walk_up=True
                        )
                        subprocess.check_call(
                            [
                                "install_name_tool",
                                "-change",
                                lib_path,
                                f"@executable_path/{lib_relpath}",
                                binary_to_patch,
                            ]
                        )

            for root, _, files in os.walk(d / "bin"):
                for file in files:
                    resolved = os.path.join(root, file)
                    rel = os.path.relpath(resolved, d)
                    wheel.write(resolved, f"{PROJECT_NAME}/{rel}")
            for root, _, files in os.walk(d / "lib"):
                for file in files:
                    resolved = os.path.join(root, file)
                    rel = os.path.relpath(resolved, d)
                    wheel.write(resolved, f"{PROJECT_NAME}/{rel}")

            # copy python files
            wheel.write(
                f"wheel_build/{PROJECT_NAME}/__main__.py",
                f"{PROJECT_NAME}/__main__.py",
            )

    return wheel_filename
