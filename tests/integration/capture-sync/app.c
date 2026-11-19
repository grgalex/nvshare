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
 * usage: app [HOLD [KERNELS [COPY]]]
 *
 * Launches KERNELS (default 300) kernels into a stream capture, then ends it.
 * Keeps the capture open for HOLD (default 0) seconds first. With COPY=1,
 * also captures a memory copy.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int cuInit(unsigned int flags);
int cuLaunchKernel(void *f, unsigned int gx, unsigned int gy, unsigned int gz,
		   unsigned int bx, unsigned int by, unsigned int bz,
		   unsigned int shm, void *stream, void **params, void **extra);
int cuStreamBeginCapture_v2(void *stream, int mode);
int cuStreamEndCapture(void *stream, void **graph);
int cuMemcpyDtoDAsync_v2(unsigned long long dst, unsigned long long src,
			 size_t bytes, void *stream);

int main(int argc, char *argv[])
{
	int hold = argc > 1 ? atoi(argv[1]) : 0;
	int kernels = argc > 2 ? atoi(argv[2]) : 300;
	int copy = argc > 3 ? atoi(argv[3]) : 0;
	void *stream = (void *)0x100;
	void *graph = NULL;
	int ret;

	if (cuInit(0) != 0)
		return 1;
	for (int i = 0; i < 10; i++)
		cuLaunchKernel(NULL, 1, 1, 1, 1, 1, 1, 0, NULL, NULL, NULL);

	if (cuStreamBeginCapture_v2(stream, 0) != 0)
		return 1;
	sleep(hold);
	for (int i = 0; i < kernels; i++)
		cuLaunchKernel(NULL, 1, 1, 1, 1, 1, 1, 0, stream, NULL, NULL);
	if (copy && cuMemcpyDtoDAsync_v2(0x1000, 0x2000, 64, stream) != 0)
		return 1;
	ret = cuStreamEndCapture(stream, &graph);

	printf("cuStreamEndCapture returned %d\n", ret);
	return ret;
}
