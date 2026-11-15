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
# A too-long NVSHARE_SOCK_DIR fails clearly and leaves nothing behind.

. "$(dirname "$0")/../../lib.sh"

build_nvshare

LONG_DIR=$SOCK_TMP/$(printf 'd%.0s' $(seq 120))
rc=0
NVSHARE_SOCK_DIR=$LONG_DIR "$NVSHARE_BIN/nvshare-scheduler" \
	>"$TEST_TMP/long.log" 2>&1 || rc=$?
cat "$TEST_TMP/long.log"

check "scheduler exits with an error (status $rc)" [ "$rc" -ne 0 ]
check "error says the path is too long" grep -q "is too long" "$TEST_TMP/long.log"
check "socket directory is not created" [ ! -e "$LONG_DIR" ]

done_testing
