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
# nvshare must not synchronize while the application captures a CUDA graph,
# not even when the scheduler asks for the GPU lock back (#28).

. "$(dirname "$0")/../../lib.sh"

HERE=$(cd "$(dirname "$0")" && pwd)

build_nvshare
build_fake_libcuda
gcc -O2 -Wall -Wextra -o "$TEST_TMP/app" "$HERE/app.c" -L"$TEST_TMP" -lcuda
start_scheduler

# run_app NAME ARGS...: run the app, return its status.
run_app() {
	LD_LIBRARY_PATH=$TEST_TMP LD_PRELOAD=$NVSHARE_BIN/libnvshare.so \
		timeout 20 "$TEST_TMP/app" "${@:2}" >"$TEST_TMP/app-$1.log" 2>&1
}

rc=0
run_app alone 0 || rc=$?
sed 's/^/#   /' "$TEST_TMP/app-alone.log"
check "capture survives the periodic sync (status $rc)" [ "$rc" -eq 0 ]

"$NVSHARE_BIN/nvsharectl" -T 1
rc_a=0; rc_b=0
# Few captured kernels: only the lock handover could break this capture
run_app holder 3 1 &
holder=$!
sleep 1
run_app waiter 0 &
waiter=$!
wait "$holder" || rc_a=$?
wait "$waiter" || rc_b=$?
sed 's/^/#   /' "$TEST_TMP/app-holder.log" "$TEST_TMP/app-waiter.log"
check "capture survives a lock handover (status $rc_a)" [ "$rc_a" -eq 0 ]
check "the waiting client ran too (status $rc_b)" [ "$rc_b" -eq 0 ]
check "the scheduler asked for the lock back" grep -q "Sent DROP_LOCK" "$SCHEDULER_LOG"

done_testing
