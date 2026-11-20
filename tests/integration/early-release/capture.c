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
 * Fake capture functions for the early-release test: like the shared stubs,
 * but the second cuStreamBeginCapture_v2() takes 1 s.
 */

#include <stddef.h>
#include <unistd.h>

static void *capture_stream;
static int capture_invalidated;
static int begun;

int cuStreamBeginCapture_v2(void *stream, int mode)
{
	(void)mode;
	if (++begun == 2)
		sleep(1);
	capture_stream = stream;
	capture_invalidated = 0;
	return 0;
}

int cuStreamEndCapture(void *stream, void **graph)
{
	if (stream == NULL || stream != capture_stream)
		return 401; /* CUDA_ERROR_ILLEGAL_STATE */
	capture_stream = NULL;
	*graph = (void *)0x1;
	return capture_invalidated ? 901 : 0;
}

int cuStreamIsCapturing(void *stream, int *status)
{
	*status = (stream != NULL && stream == capture_stream) ?
		  (capture_invalidated ? 2 : 1) : 0;
	return 0;
}

int cuCtxSynchronize(void)
{
	if (capture_stream != NULL) {
		capture_invalidated = 1;
		return 900; /* CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED */
	}
	return 0;
}
