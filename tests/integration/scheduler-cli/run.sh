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
# nvsharectl commands reach the scheduler.

. "$(dirname "$0")/../../lib.sh"

build_nvshare
start_scheduler

# ctl ARGS...: run nvsharectl, show its output and keep it in $CTL_OUT.
ctl() {
	CTL_OUT=$("$NVSHARE_BIN/nvsharectl" "$@" 2>&1)
	echo "$CTL_OUT"
}

no_usage() {
	! grep -q "^usage:" <<<"$CTL_OUT"
}

ctl -S off
check "anti-thrashing off" wait_for_log "Scheduler turned OFF"
check "-S off prints no usage" no_usage

ctl -S on
check "anti-thrashing on" wait_for_log "Scheduler turned ON"
check "-S on prints no usage" no_usage

ctl -T 5
check "set TQ" wait_for_log "New TQ = 5"
check "-T prints no usage" no_usage

ctl -h
check "-h prints usage" grep -q "^usage:" <<<"$CTL_OUT"

done_testing
