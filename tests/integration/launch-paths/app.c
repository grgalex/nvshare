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
 * usage: app ex|coop [SECONDS]
 *
 * Launches kernels for SECONDS (default 4) seconds, only through
 * cuLaunchKernelEx() or cuLaunchCooperativeKernel().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	unsigned int gridDimX, gridDimY, gridDimZ;
	unsigned int blockDimX, blockDimY, blockDimZ;
	unsigned int sharedMemBytes;
	void *hStream;
	void *attrs;
	unsigned int numAttrs;
} launch_config;

int cuInit(unsigned int flags);
int cuLaunchKernelEx(const launch_config *config, void *f, void **params,
		     void **extra);
int cuLaunchCooperativeKernel(void *f, unsigned int gx, unsigned int gy,
			      unsigned int gz, unsigned int bx, unsigned int by,
			      unsigned int bz, unsigned int shm, void *stream,
			      void **params);

int main(int argc, char *argv[])
{
	launch_config config = {1, 1, 1, 1, 1, 1, 0, (void *)0x100, NULL, 0};
	int ex = argc > 1 && strcmp(argv[1], "ex") == 0;
	time_t end = time(NULL) + (argc > 2 ? atoi(argv[2]) : 4);
	int ret;

	if (cuInit(0) != 0)
		return 1;
	while (time(NULL) < end) {
		ret = ex ? cuLaunchKernelEx(&config, NULL, NULL, NULL) :
			   cuLaunchCooperativeKernel(NULL, 1, 1, 1, 1, 1, 1, 0,
						     (void *)0x100, NULL);
		if (ret != 0)
			return 1;
		usleep(10000);
	}
	printf("PASS\n");
	return 0;
}
