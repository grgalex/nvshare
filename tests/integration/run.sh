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
# Run integration tests: all of them, or those named as arguments.
# Set V=1 to show the output of passing tests too.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)

if [ $# -eq 0 ]; then
	set -- $(cd "$HERE" && ls -d */ | tr -d /)
fi

echo "1..$#"
n=0
failed=0
for name in "$@"; do
	n=$((n + 1))
	if [ ! -x "$HERE/$name/run.sh" ]; then
		echo "not ok $n - $name # no such test"
		failed=1
		continue
	fi
	out=$("$HERE/$name/run.sh" 2>&1)
	rc=$?
	skip=$(echo "$out" | grep -m 1 '^1\.\.0 # SKIP' | sed 's/^1\.\.0 //')
	if [ "$rc" -eq 0 ] && [ -n "$skip" ]; then
		echo "ok $n - $name $skip"
	elif [ "$rc" -eq 0 ]; then
		echo "ok $n - $name"
	else
		echo "not ok $n - $name"
		failed=1
	fi
	if [ "$rc" -ne 0 ] || [ "${V:-0}" = 1 ]; then
		echo "$out" | sed 's/^/#   /'
	fi
done
exit "$failed"
