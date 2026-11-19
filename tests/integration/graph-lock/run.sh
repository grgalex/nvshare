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
# Graph launches go through the GPU lock like kernel launches (#28).

. "$(dirname "$0")/../../lib.sh"

HERE=$(cd "$(dirname "$0")" && pwd)

build_nvshare
build_fake_libcuda
gcc -O2 -Wall -Wextra -o "$TEST_TMP/app" "$HERE/app.c" -L"$TEST_TMP" -lcuda
start_scheduler
"$NVSHARE_BIN/nvsharectl" -T 1

run_app() {
	LD_LIBRARY_PATH=$TEST_TMP LD_PRELOAD=$NVSHARE_BIN/libnvshare.so \
		timeout 30 "$TEST_TMP/app" 4 >"$TEST_TMP/app-$1.log" 2>&1
}

count() {
	grep -c -- "$1" "$SCHEDULER_LOG" || true
}

run_app alone
check "graph-only client finished" grep -qx PASS "$TEST_TMP/app-alone.log"
check "graph launches request the lock ($(count 'Received REQ_LOCK') requests)" [ "$(count 'Received REQ_LOCK')" -gt 0 ]

run_app 1 &
app1=$!
run_app 2 &
app2=$!
wait "$app1" "$app2"
check "co-located graph clients finished" grep -qx PASS "$TEST_TMP/app-1.log"
check "co-located graph clients finished" grep -qx PASS "$TEST_TMP/app-2.log"
check "the lock rotates between graph clients ($(count 'Sent DROP_LOCK') drops)" [ "$(count 'Sent DROP_LOCK')" -gt 0 ]

done_testing
