/*
 * Copyright (c) 2026 Georgios Alexopoulos
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * Test app: launches a CUDA graph repeatedly for SECONDS (default 4) seconds.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int cuInit(unsigned int flags);
int cuGraphLaunch(void *exec, void *stream);

int main(int argc, char *argv[])
{
	time_t end = time(NULL) + (argc > 1 ? atoi(argv[1]) : 4);

	if (cuInit(0) != 0)
		return 1;
	while (time(NULL) < end) {
		if (cuGraphLaunch((void *)0x1, (void *)0x100) != 0)
			return 1;
		usleep(10000);
	}
	printf("PASS\n");
	return 0;
}
