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

export PDK_ROOT=$(pwd)/pdks
export RUN_ROOT=$(pwd)
echo $PDK_ROOT
echo $RUN_ROOT
export MAGIC_MAGICRC=$PDK_ROOT/$PDK/libs.tech/magic/sky130A.magicrc
export test_dir=/magic_root/.travisCI/testcases/$PDK/designs/$DESIGN/test
export OUT_DIR=$RUN_ROOT/.travisCI/testcases/$PDK/designs/$DESIGN/test/drc1

if ! [[ -d "$OUT_DIR" ]]
then
    mkdir $OUT_DIR
fi

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

TEST=$OUT_DIR/magic.drc
BENCHMARK=$RUN_ROOT/.travisCI/testcases/$PDK/designs/$DESIGN/benchmark/magic.drc



crashSignal=$(find $TEST)
if ! [[ $crashSignal ]]; then echo "DRC Check FAILED"; exit -1; fi


Test_Magic_violations=$(grep "^ [0-9]" $TEST | wc -l)
if ! [[ $Test_Magic_violations ]]; then Test_Magic_violations=-1; fi
if [ $Test_Magic_violations -ne -1 ]; then Test_Magic_violations=$(((Test_Magic_violations+3)/4)); fi

Benchmark_Magic_violations=$(grep "^ [0-9]" $BENCHMARK | wc -l)
if ! [[ $Benchmark_Magic_violations ]]; then Benchmark_Magic_violations=-1; fi
if [ $Benchmark_Magic_violations -ne -1 ]; then Benchmark_Magic_violations=$(((Benchmark_Magic_violations+3)/4)); fi

echo "Test # of DRC Violations:"
echo $Test_Magic_violations

echo "Benchmark # of DRC Violations:"
echo $Benchmark_Magic_violations

if [ $Benchmark_Magic_violations -ne $Test_Magic_violations ]; then exit -1; fi

exit 0
