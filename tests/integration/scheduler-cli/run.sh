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

"$NVSHARE_BIN/nvsharectl" -S off
check "anti-thrashing off" wait_for_log "Scheduler turned OFF"

"$NVSHARE_BIN/nvsharectl" -S on
check "anti-thrashing on" wait_for_log "Scheduler turned ON"

"$NVSHARE_BIN/nvsharectl" -T 5
check "set TQ" wait_for_log "New TQ = 5"

done_testing
