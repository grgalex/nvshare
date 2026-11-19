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
# Shared helpers for nvshare tests. Source this from a test script.
#
# Set NVSHARE_BIN to a directory with prebuilt libnvshare.so,
# nvshare-scheduler and nvsharectl to skip building from source.

set -eu

TESTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NVSHARE_ROOT=$(cd "$TESTS_DIR/.." && pwd)

TEST_TMP=$(mktemp -d)
# Socket paths are limited to 107 bytes, so keep this one short.
SOCK_TMP=$(mktemp -d /tmp/nvshare.XXXXXX)
export NVSHARE_SOCK_DIR=$SOCK_TMP/sock
SCHEDULER_LOG=$TEST_TMP/scheduler.log
SCHEDULER_PID=

_test_count=0
_test_failed=0

_cleanup() {
	local rc=$?

	if [ -n "$SCHEDULER_PID" ]; then
		if kill -0 "$SCHEDULER_PID"; then
			kill "$SCHEDULER_PID"
			# 143: terminated by our own kill
			wait "$SCHEDULER_PID" || true
		else
			echo "Bail out! nvshare-scheduler exited unexpectedly"
			rc=1
		fi
	fi
	if [ "$rc" -ne 0 ] && [ -s "$SCHEDULER_LOG" ]; then
		echo "# nvshare-scheduler log:"
		sed 's/^/# /' "$SCHEDULER_LOG"
	fi
	rm -rf "$TEST_TMP" "$SOCK_TMP"
	exit "$rc"
}
trap _cleanup EXIT

die() {
	echo "Bail out! $*"
	exit 1
}

skip_all() {
	echo "1..0 # SKIP $*"
	exit 0
}

# require COMMAND...: bail out if a command is missing.
require() {
	local cmd

	for cmd in "$@"; do
		command -v "$cmd" >/dev/null || die "$cmd not found"
	done
}

build_nvshare() {
	if [ -n "${NVSHARE_BIN:-}" ]; then
		return
	fi
	require gcc make
	cp -r "$NVSHARE_ROOT/src" "$TEST_TMP/src"
	if ! make -C "$TEST_TMP/src" libnvshare.so nvshare-scheduler \
	     nvsharectl >"$TEST_TMP/build.log" 2>&1; then
		cat "$TEST_TMP/build.log"
		die "build failed"
	fi
	NVSHARE_BIN=$TEST_TMP/src
}

# build_fake_libcuda [FILE.c...]: build $TEST_TMP/libcuda.so{,.1} from the
# shared stubs plus the given files, which override stubs they redefine.
build_fake_libcuda() {
	gcc -O2 -Wall -Wextra -shared -fPIC -Wl,-soname,libcuda.so.1 \
		-o "$TEST_TMP/libcuda.so.1" "$TESTS_DIR/fake-libcuda/stubs.c" "$@" \
		-ldl -lpthread
	ln -s libcuda.so.1 "$TEST_TMP/libcuda.so"
}

# build_fake_nvml: build $TEST_TMP/libnvidia-ml.so.1, an idle GPU.
build_fake_nvml() {
	gcc -O2 -Wall -Wextra -shared -fPIC -Wl,-soname,libnvidia-ml.so.1 \
		-o "$TEST_TMP/libnvidia-ml.so.1" "$TESTS_DIR/fake-libcuda/nvml.c"
}

start_scheduler() {
	local i

	NVSHARE_DEBUG=1 "$NVSHARE_BIN/nvshare-scheduler" >"$SCHEDULER_LOG" 2>&1 &
	SCHEDULER_PID=$!
	for i in $(seq 50); do
		[ -S "$NVSHARE_SOCK_DIR/scheduler.sock" ] && return
		sleep 0.1
	done
	cat "$SCHEDULER_LOG"
	die "nvshare-scheduler did not start"
}

# wait_for_log PATTERN [SECONDS]: wait until the scheduler log matches PATTERN.
wait_for_log() {
	local i

	for i in $(seq $((${2:-5} * 10))); do
		grep -q -- "$1" "$SCHEDULER_LOG" && return 0
		sleep 0.1
	done
	return 1
}

# not_grep PATTERN FILE: true if FILE doesn't match PATTERN.
not_grep() {
	! grep -q -- "$1" "$2"
}

# check DESCRIPTION COMMAND...: run COMMAND and report it in TAP format.
check() {
	local desc=$1
	shift
	_test_count=$((_test_count + 1))
	if "$@"; then
		echo "ok $_test_count - $desc"
	else
		echo "not ok $_test_count - $desc"
		_test_failed=1
	fi
}

done_testing() {
	echo "1..$_test_count"
	exit "$_test_failed"
}
