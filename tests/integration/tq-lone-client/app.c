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
 * Test app: launches kernels for SECONDS (default 5) seconds.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int cuInit(unsigned int flags);
int cuLaunchKernel(void *f, unsigned int gx, unsigned int gy, unsigned int gz,
		   unsigned int bx, unsigned int by, unsigned int bz,
		   unsigned int shm, void *stream, void **params, void **extra);

int main(int argc, char *argv[])
{
	time_t end = time(NULL) + (argc > 1 ? atoi(argv[1]) : 5);

	if (cuInit(0) != 0)
		return 1;
	while (time(NULL) < end) {
		if (cuLaunchKernel(NULL, 1, 1, 1, 1, 1, 1, 0, NULL, NULL, NULL) != 0)
			return 1;
		usleep(10000);
	}
	printf("PASS\n");
	return 0;
}
