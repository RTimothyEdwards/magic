# CI setup:

This document will cover:
- How to add more test cases.
- How to add more test scripts.
- The current existing tests.
- Required Configurations.

## How To Add Test Cases:

You need to follow a certain structure:

```
.travisCI/testcases/<PDK>/designs/
├── <design name>
│   ├── test
│   │   ├── config.tcl
│   │   └── <design name>.<target type>
│   ├── benchmark
│   │   ├── magic.drc
│   │   └── magic.antenna_violators.rpt
```

- `config.tcl` should contain the necessary information to get the design started on a test. Check [this](#current-required-configurations) for more.

- `<target type>` could be `def`, `gds`, or `mag`.

- `magic.drc` should be produced by this same [script](#drc-checks).

- `magic.antenna_violators.rpt` should be produced by this same [script](#antenna-violations-checks).

**NOTE**: make sure to go into the `.travisCI/testcases` directory and run `make` to compress all files larger than 10MB once you add the test case.

Now define what you want to be done with your test case:

- open [.travis.yml][11].
- Add a new job description under `job/include`:
```yml
    - name: "Name of the Test"
      env: DESIGN=<design name> PDK=<PDK>
      script:
        <list test scripts to run>
```
Check the file itself for examples.

## How To Add Test Scripts:

- Add bash script under testScripts. Define the necessary defines:
```bash
# exit when any command fails
set -e

export PDK_ROOT=$(pwd)/pdks
export RUN_ROOT=$(pwd)
echo $PDK_ROOT
echo $RUN_ROOT

export MAGIC_MAGICRC=$PDK_ROOT/$PDK/libs.tech/magic/sky130A.magicrc
```

- Call magic directly or through another script in the docker container. Make sure to pass your env vars. An example is:

```bash
docker run -it -v $RUN_ROOT:/magic_root \
    -v $PDK_ROOT:$PDK_ROOT -e PDK=$PDK \
    -e PDK_ROOT=$PDK_ROOT -e DESIGN=$DESIGN -e test_dir=$test_dir \
    -e OUT_DIR=$test_dir/drc1 \
    -u $(id -u $USER):$(id -g $USER) \
    magic:latest sh -c "magic \
        -noconsole \
        -dnull \
        -rcfile $MAGIC_MAGICRC \
        /magic_root/.travisCI/testScripts/magic_drc.tcl \
        </dev/null \
        |& tee $OUT_DIR/magic_drc.log"
```

- Check the results and compare them against whatever you want.

- Update the documentation.

- Use the script with the different test cases.

## The Current Tests:

### GDS Streaming Out:
This utilizes 3 scripts: [interface and setup bash script][0], [tcl for writing GDS-II][1], and [tcl for reading GDS-II][2].

The script requires a DEF file to be present in the test directory. Thus one of them must be present in the test directory. It also needs to be provided `STD_CELL_LIBRARY` through the `config.tcl` of the test case. Check the How to Add More Test Cases section for more about that.

Passing factors: The GDS-II is there and magic was able to read it.

### DRC Checks:
This utilizes 2 scripts: [interface and setup bash script][3] and [tcl script for running DRC][4].

The script can operate on DEF, GDS, or MAG. Thus one of them must be present in the test directory. The script needs a previous benchmark DRC result to be present in the benchmark directory.

It needs to be `STD_CELL_LIBRARY` and `TARGET_TYPE` through the `config.tcl` of the test case. Check the How to Add More Test Cases section for more about that.

Passing factors: The number of violations is the same as the one in the benchmark.

### Extractions:
This utilizes 2 scripts: [interface and setup bash script][5] and [tcl script for running DRC][6].

The script can operate on DEF, GDS, or MAG. Thus one of them must be present in the test directory.

It needs to be `STD_CELL_LIBRARY` and `TARGET_TYPE` through the `config.tcl` of the test case. Check the How to Add More Test Cases section for more about that.

Passing factors: The .ext is there.

### Antenna Violations Checks:
This utilizes 2 scripts: [interface and setup bash script][7] and [tcl script for running DRC][8].

The script can operate on DEF, GDS, or MAG. Thus one of them must be present in the test directory. The script needs a previous benchmark Antenna checks result to be present in the benchmark directory.

It needs to be `STD_CELL_LIBRARY` and `TARGET_TYPE` through the `config.tcl` of the test case. Check the How to Add More Test Cases section for more about that.

Passing factors: The number of violations is the same as the one in the benchmark.

## Current Required Configurations:

`STD_CELL_LIBRARY`: the standard cell library that the test case belongs to. i.e.: `sky130_fd_sc_hd`.

`TARGET_TYPE`: the type of the file you want to run the operations on. This could be auto detected later.. Current possible values: `gds`, `mag`, and `def`.

[Here][9] you can find a sample config.


[0]: .testScripts/gdsStreaming.sh
[1]: .testScripts/magic_gds_stream.tcl
[2]: .testScripts/gds_read.sh
[3]: .testScripts/drcChecks.sh
[4]: .testScripts/magic_drc.tcl
[5]: .testScripts/extraction.sh
[6]: .testScripts/extraction.tcl
[7]: .testScripts/antennaChecks.sh
[8]: .testScripts/antennaChecks.tcl
[9]: testcases/sky130A/sample_config.tcl
[10]: testcases/Makefile
[11]: ../.travis.yml