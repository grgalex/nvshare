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
 * usage: app idle SECONDS | app race
 *
 * idle: launch a kernel, print how long getting the GPU lock took, then do
 *       nothing for SECONDS seconds.
 * race: keep a capture open for ~4.7 s, end it and begin another one, then
 *       make a gated call inside it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int cuInit(unsigned int flags);
int cuLaunchKernel(void *f, unsigned int gx, unsigned int gy, unsigned int gz,
		   unsigned int bx, unsigned int by, unsigned int bz,
		   unsigned int shm, void *stream, void **params, void **extra);
int cuStreamBeginCapture_v2(void *stream, int mode);
int cuStreamEndCapture(void *stream, void **graph);

static void launch(void *stream)
{
	cuLaunchKernel(NULL, 1, 1, 1, 1, 1, 1, 0, stream, NULL, NULL);
}

int main(int argc, char *argv[])
{
	void *first = (void *)0x100, *second = (void *)0x200, *graph;
	struct timespec start, end;

	if (argc < 2 || cuInit(0) != 0)
		return 1;

	if (strcmp(argv[1], "idle") == 0) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		launch(NULL);
		clock_gettime(CLOCK_MONOTONIC, &end);
		printf("lock after %ld s\n", (long)(end.tv_sec - start.tv_sec));
		fflush(stdout);
		sleep(argc > 2 ? atoi(argv[2]) : 0);
		return 0;
	}

	launch(NULL);
	cuStreamBeginCapture_v2(first, 0);
	launch(first);
	usleep(4700000);
	if (cuStreamEndCapture(first, &graph) != 0)
		return 2;
	cuStreamBeginCapture_v2(second, 0);
	launch(NULL); /* gated */
	launch(second);
	if (cuStreamEndCapture(second, &graph) != 0)
		return 3;
	printf("PASS\n");
	return 0;
}
