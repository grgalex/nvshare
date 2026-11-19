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
# cuLaunchKernelEx() and cuLaunchCooperativeKernel() go through the GPU lock
# like cuLaunchKernel().

. "$(dirname "$0")/../../lib.sh"

HERE=$(cd "$(dirname "$0")" && pwd)

build_nvshare
build_fake_libcuda
gcc -O2 -Wall -Wextra -o "$TEST_TMP/app" "$HERE/app.c" -L"$TEST_TMP" -lcuda
start_scheduler
"$NVSHARE_BIN/nvsharectl" -T 1

# run_app NAME API: run the app, return its status.
run_app() {
	LD_LIBRARY_PATH=$TEST_TMP LD_PRELOAD=$NVSHARE_BIN/libnvshare.so \
		timeout 30 "$TEST_TMP/app" "$2" 4 >"$TEST_TMP/app-$1.log" 2>&1
}

count() {
	grep -c -- "$1" "$SCHEDULER_LOG" || true
}

for api in ex coop; do
	requests=$(count 'Received REQ_LOCK')
	drops=$(count 'Sent DROP_LOCK')
	run_app "$api-1" "$api" &
	app1=$!
	run_app "$api-2" "$api" &
	app2=$!
	wait "$app1" "$app2"
	check "$api: clients finished" grep -qx PASS "$TEST_TMP/app-$api-1.log"
	check "$api: clients finished" grep -qx PASS "$TEST_TMP/app-$api-2.log"
	check "$api: launches request the lock" [ "$(count 'Received REQ_LOCK')" -gt "$requests" ]
	check "$api: the lock rotates" [ "$(count 'Sent DROP_LOCK')" -gt "$drops" ]
done

done_testing
