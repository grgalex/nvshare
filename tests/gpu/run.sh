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
# usage: run.sh WORKLOAD [COPIES]
#
# Run COPIES (default 2) co-located copies of a Python WORKLOAD under
# nvshare. Needs a GPU and the workload's framework. Set PYTHON to pick the
# interpreter and TIMEOUT (seconds) to bound the run.

. "$(dirname "$0")/../lib.sh"

[ $# -ge 1 ] || die "usage: $0 WORKLOAD [COPIES]"
WORKLOAD=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
COPIES=${2:-2}
PYTHON=${PYTHON:-python3}
TIMEOUT=${TIMEOUT:-900}

[ -e /dev/nvidiactl ] || skip_all "no NVIDIA GPU found"
command -v "$PYTHON" >/dev/null || skip_all "$PYTHON not found"

build_nvshare
start_scheduler

pids=()
for i in $(seq "$COPIES"); do
	LD_PRELOAD=$NVSHARE_BIN/libnvshare.so \
		timeout "$TIMEOUT" "$PYTHON" -u "$WORKLOAD" \
		>"$TEST_TMP/workload-$i.log" 2>&1 &
	pids+=($!)
done

for i in $(seq "$COPIES"); do
	rc=0
	wait "${pids[$((i - 1))]}" || rc=$?
	check "copy $i exited successfully (status $rc)" [ "$rc" -eq 0 ]
	check "copy $i printed PASS" grep -qx PASS "$TEST_TMP/workload-$i.log"
	if [ "$rc" -ne 0 ]; then
		tail -n 20 "$TEST_TMP/workload-$i.log" | sed 's/^/#   /'
	fi
done

check "all copies registered" \
	[ "$(grep -c 'Registered client' "$SCHEDULER_LOG")" -eq "$COPIES" ]
check "every copy got the GPU lock" \
	[ "$(grep 'Sent LOCK_OK' "$SCHEDULER_LOG" | sort -u | wc -l)" -eq "$COPIES" ]

done_testing
