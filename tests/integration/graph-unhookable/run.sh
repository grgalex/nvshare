#!/bin/bash
#
# Copyright (c) 2026 Georgios Alexopoulos
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# nvshare warns when it can't hook a CUDA graph function it is handed.

. "$(dirname "$0")/../../lib.sh"

HERE=$(cd "$(dirname "$0")" && pwd)

build_nvshare
build_fake_libcuda
gcc -O2 -Wall -Wextra -o "$TEST_TMP/app" "$HERE/app.c" -L"$TEST_TMP" -lcuda
start_scheduler

rc=0
LD_LIBRARY_PATH=$TEST_TMP LD_PRELOAD=$NVSHARE_BIN/libnvshare.so \
	timeout 10 "$TEST_TMP/app" >"$TEST_TMP/app.log" 2>&1 || rc=$?
sed 's/^/#   /' "$TEST_TMP/app.log"
check "lookup succeeds (status $rc)" [ "$rc" -eq 0 ]
check "nvshare warns about the unhookable function" \
	grep -q "Can't hook cuGraphLaunch" "$TEST_TMP/app.log"

done_testing
