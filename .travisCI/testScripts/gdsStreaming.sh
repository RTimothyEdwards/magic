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
export OUT_DIR=$RUN_ROOT/.travisCI/testcases/$PDK/designs/$DESIGN/test/gds

if ! [[ -d "$OUT_DIR" ]]
then
    mkdir $OUT_DIR
fi

docker run -it -v $RUN_ROOT:/magic_root \
    -v $PDK_ROOT:$PDK_ROOT -e PDK=$PDK \
    -e PDK_ROOT=$PDK_ROOT -e DESIGN=$DESIGN -e test_dir=$test_dir \
    -e OUT_DIR=$test_dir/gds \
    -u $(id -u $USER):$(id -g $USER) \
    magic:latest sh -c "magic \
        -noconsole \
        -dnull \
        -rcfile $MAGIC_MAGICRC \
        /magic_root/.travisCI/testScripts/magic_gds_stream.tcl \
        </dev/null \
        |& tee $OUT_DIR/magic_gds_stream.log"

TEST=$OUT_DIR/$DESIGN.gds

docker run -it -v $RUN_ROOT:/magic_root \
    -v $PDK_ROOT:$PDK_ROOT -e PDK=$PDK \
    -e PDK_ROOT=$PDK_ROOT -e DESIGN=$DESIGN -e test_dir=$test_dir \
    -e OUT_DIR=$test_dir/gds \
    -u $(id -u $USER):$(id -g $USER) \
    magic:latest sh -c "magic \
        -noconsole \
        -dnull \
        -rcfile $MAGIC_MAGICRC \
        /magic_root/.travisCI/testScripts/gds_read.tcl \
        </dev/null \
        |& tee $OUT_DIR/magic_gds_read.log"

crashSignal=$(find $TEST)
if ! [[ $crashSignal ]]; then echo "GDS streaming failed."; exit -1; fi
echo "GDS successfully streamed."
exit 0
