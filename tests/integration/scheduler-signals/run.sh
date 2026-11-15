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
# The scheduler exits cleanly on SIGTERM and SIGINT and removes its socket.

. "$(dirname "$0")/../../lib.sh"

# exited PID: true once PID has exited (reaped by bash, or a zombie).
exited() {
	[ ! -e "/proc/$1" ] || [ "$(cut -d ' ' -f 3 "/proc/$1/stat")" = Z ]
}

build_nvshare

for sig in TERM INT; do
	start_scheduler
	kill -"$sig" "$SCHEDULER_PID"
	for i in $(seq 50); do
		exited "$SCHEDULER_PID" && break
		sleep 0.1
	done
	if ! exited "$SCHEDULER_PID"; then
		echo "# SIG$sig: no exit within 5s, killing it"
		kill -KILL "$SCHEDULER_PID"
	fi
	rc=0
	wait "$SCHEDULER_PID" || rc=$?
	SCHEDULER_PID=
	check "SIG$sig: exits with status 0 (got $rc)" [ "$rc" -eq 0 ]
	check "SIG$sig: logs the signal" grep -q "Received .*, exiting" "$SCHEDULER_LOG"
	check "SIG$sig: removes its socket" [ ! -e "$NVSHARE_SOCK_DIR/scheduler.sock" ]
done

done_testing
