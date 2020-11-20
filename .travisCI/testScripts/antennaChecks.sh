#!/bin/bash
# Copyright 2020 Efabless Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# exit when any command fails
set -e

export PDK_ROOT=$(pwd)/pdks
export RUN_ROOT=$(pwd)
echo $PDK_ROOT
echo $RUN_ROOT

export MAGIC_MAGICRC=$PDK_ROOT/$PDK/libs.tech/magic/sky130A.magicrc
export test_dir=/magic_root/.travisCI/testcases/$PDK/designs/$DESIGN/test

docker run -it -v $RUN_ROOT:/magic_root -v $PDK_ROOT:$PDK_ROOT -e PDK=$PDK -e test_dir=$test_dir -e MAGIC_MAGICRC=$MAGIC_MAGICRC -e PDK_ROOT=$PDK_ROOT -e DESIGN=$DESIGN -u $(id -u $USER):$(id -g $USER) magic:latest bash -c "tclsh ./.travisCI/testScripts/antennaChecks.tcl"


TEST=$RUN_ROOT/.travisCI/testcases/$PDK/designs/$DESIGN/test/antenna/magic.antenna_violators.rpt
BENCHMARK=$RUN_ROOT/.travisCI/testcases/$PDK/designs/$DESIGN/benchmark/magic.antenna_violators.rpt


crashSignal=$(find $TEST)
if ! [[ $crashSignal ]]; then echo "antenna check failed"; exit -1; fi


test_antenna_violations=$(wc $TEST -l | cut -d ' ' -f 1)
if ! [[ $test_antenna_violations ]]; then test_antenna_violations=-1; fi

benchmark_antenna_violations=$(wc $BENCHMARK -l | cut -d ' ' -f 1)
if ! [[ $benchmark_antenna_violations ]]; then benchmark_antenna_violations=-1; fi

echo "Extraction Feedback:"
cat $RUN_ROOT/.travisCI/testcases/$PDK/designs/$DESIGN/test/antenna/magic_ext2spice.antenna.feedback.txt

echo "Test # of Antenna Violations:"
echo $test_antenna_violations

echo "Benchmark # of Antenna Violations:"
echo $benchmark_antenna_violations


if [ $benchmark_antenna_violations -ne $test_antenna_violations ]; then exit -1; fi

exit 0
