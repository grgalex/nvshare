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
 * Fake libcuda for tests that need no GPU: every symbol libnvshare resolves,
 * as weak no-ops. A test overrides a symbol by defining it in its own file.
 */

#include <stddef.h>

#define WEAK __attribute__((weak))

WEAK int cuInit(unsigned int flags) { (void)flags; return 0; }

WEAK int cuGetErrorString(int error, const char **str)
{
	(void)error;
	*str = "fake-libcuda error";
	return 0;
}

WEAK int cuGetErrorName(int error, const char **str)
{
	(void)error;
	*str = "FAKE_LIBCUDA_ERROR";
	return 0;
}

WEAK int cuMemGetInfo_v2(size_t *free_bytes, size_t *total_bytes)
{
	*free_bytes = (size_t)8 << 30;
	*total_bytes = (size_t)8 << 30;
	return 0;
}

WEAK int cuMemAllocManaged(void) { return 0; }
WEAK int cuMemFree_v2(void) { return 0; }
WEAK int cuCtxSetCurrent(void) { return 0; }
WEAK int cuCtxGetCurrent(void) { return 0; }
WEAK int cuLaunchKernel(void) { return 0; }
WEAK int cuMemcpy(void) { return 0; }
WEAK int cuMemcpyAsync(void) { return 0; }
WEAK int cuMemcpyDtoH_v2(void) { return 0; }
WEAK int cuMemcpyDtoHAsync_v2(void) { return 0; }
WEAK int cuMemcpyHtoD_v2(void) { return 0; }
WEAK int cuMemcpyHtoDAsync_v2(void) { return 0; }
WEAK int cuMemcpyDtoD_v2(void) { return 0; }
WEAK int cuMemcpyDtoDAsync_v2(void) { return 0; }

/* One capture at a time. Like the real driver, a sync during it fails (900)
 * and invalidates it. The stream right after the origin stream has joined
 * the capture: it is capturing, but can't end it.
 */
static void *capture_stream;
static int capture_invalidated;

WEAK int cuStreamBeginCapture_v2(void *stream, int mode)
{
	(void)mode;
	capture_stream = stream;
	capture_invalidated = 0;
	return 0;
}

WEAK int cuStreamEndCapture(void *stream, void **graph)
{
	if (capture_stream != NULL && stream == (char *)capture_stream + 1)
		return 903; /* CUDA_ERROR_STREAM_CAPTURE_UNMATCHED */
	if (stream == NULL || stream != capture_stream)
		return 401; /* CUDA_ERROR_ILLEGAL_STATE */
	capture_stream = NULL;
	*graph = (void *)0x1;
	return capture_invalidated ? 901 : 0; /* CUDA_ERROR_STREAM_CAPTURE_INVALIDATED */
}

WEAK int cuStreamIsCapturing(void *stream, int *status)
{
	*status = (capture_stream != NULL && (stream == capture_stream ||
		   stream == (char *)capture_stream + 1)) ?
		  (capture_invalidated ? 2 : 1) : 0;
	return 0;
}

WEAK int cuCtxSynchronize(void)
{
	if (capture_stream != NULL) {
		capture_invalidated = 1;
		return 900; /* CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED */
	}
	return 0;
}

WEAK int cuGraphLaunch(void *exec, void *stream) { (void)exec; (void)stream; return 0; }
