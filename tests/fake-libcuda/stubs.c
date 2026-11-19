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
WEAK int cuCtxSynchronize(void) { return 0; }
WEAK int cuLaunchKernel(void) { return 0; }
WEAK int cuMemcpy(void) { return 0; }
WEAK int cuMemcpyAsync(void) { return 0; }
WEAK int cuMemcpyDtoH_v2(void) { return 0; }
WEAK int cuMemcpyDtoHAsync_v2(void) { return 0; }
WEAK int cuMemcpyHtoD_v2(void) { return 0; }
WEAK int cuMemcpyHtoDAsync_v2(void) { return 0; }
WEAK int cuMemcpyDtoD_v2(void) { return 0; }
WEAK int cuMemcpyDtoDAsync_v2(void) { return 0; }
