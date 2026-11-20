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
# An idle client gives the GPU lock back early, and that never races with a
# graph capture that is about to start.

. "$(dirname "$0")/../../lib.sh"

HERE=$(cd "$(dirname "$0")" && pwd)

build_nvshare
build_fake_libcuda "$HERE/capture.c"
build_fake_nvml
gcc -O2 -Wall -Wextra -o "$TEST_TMP/app" "$HERE/app.c" -L"$TEST_TMP" -lcuda
mv "$TEST_TMP/libnvidia-ml.so.1" "$TEST_TMP/nvml.so"
start_scheduler

# run_app NAME ARGS...: run the app, return its status.
run_app() {
	LD_LIBRARY_PATH=$TEST_TMP LD_PRELOAD=$NVSHARE_BIN/libnvshare.so \
		timeout 30 "$TEST_TMP/app" "${@:2}" >"$TEST_TMP/$1.log" 2>&1
}

# A long TQ: the waiting client gets the lock quickly only through early release.
"$NVSHARE_BIN/nvsharectl" -T 60
for path in nvml fallback; do
	if [ "$path" = nvml ]; then
		cp "$TEST_TMP/nvml.so" "$TEST_TMP/libnvidia-ml.so.1"
	else
		rm "$TEST_TMP/libnvidia-ml.so.1"
	fi
	run_app "idle-$path" idle 10 &
	idle=$!
	sleep 1
	run_app "waiter-$path" idle 0
	wait "$idle"
	cat "$TEST_TMP/waiter-$path.log"
	secs=$(sed -n 's/^lock after \([0-9]*\) s$/\1/p' "$TEST_TMP/waiter-$path.log")
	check "$path: an idle client releases the lock early (${secs}s)" [ "${secs:-99}" -lt 9 ]
done

cp "$TEST_TMP/nvml.so" "$TEST_TMP/libnvidia-ml.so.1"
"$NVSHARE_BIN/nvsharectl" -T 1
rc=0
run_app race race &
race=$!
sleep 1
run_app waiter-race idle 0
wait "$race" || rc=$?
sed 's/^/#   /' "$TEST_TMP/race.log"
check "early release doesn't race with a new capture (status $rc)" [ "$rc" -eq 0 ]

done_testing
