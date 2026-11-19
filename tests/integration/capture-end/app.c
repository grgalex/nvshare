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
 * Test app: fails to end a capture from a joined stream, then keeps capturing.
 */

#include <stdio.h>

int cuInit(unsigned int flags);
int cuLaunchKernel(void *f, unsigned int gx, unsigned int gy, unsigned int gz,
		   unsigned int bx, unsigned int by, unsigned int bz,
		   unsigned int shm, void *stream, void **params, void **extra);
int cuStreamBeginCapture_v2(void *stream, int mode);
int cuStreamEndCapture(void *stream, void **graph);

int main(void)
{
	char *stream = (char *)0x100;
	void *graph = NULL;
	int ret;

	if (cuInit(0) != 0)
		return 1;
	if (cuStreamBeginCapture_v2(stream, 0) != 0)
		return 1;
	if (cuStreamEndCapture(stream + 1, &graph) == 0) {
		printf("ending the capture from a joined stream succeeded\n");
		return 1;
	}
	for (int i = 0; i < 300; i++)
		cuLaunchKernel(NULL, 1, 1, 1, 1, 1, 1, 0, stream, NULL, NULL);
	ret = cuStreamEndCapture(stream, &graph);

	printf("cuStreamEndCapture returned %d\n", ret);
	return ret;
}
