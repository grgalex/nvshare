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
 * Fake libcuda that only provides the symbols libnvshare resolves.
 */

#include <stddef.h>

int cuInit(unsigned int flags) { (void)flags; return 0; }
int cuGetErrorString(int error, const char **str) { (void)error; *str = "fake"; return 0; }
int cuGetErrorName(int error, const char **str) { (void)error; *str = "FAKE"; return 0; }
int cuMemGetInfo_v2(size_t *free_bytes, size_t *total_bytes)
{
	*free_bytes = (size_t)8 << 30;
	*total_bytes = (size_t)8 << 30;
	return 0;
}
int cuMemAllocManaged(void) { return 0; }
int cuMemFree_v2(void) { return 0; }
int cuCtxSetCurrent(void) { return 0; }
int cuCtxGetCurrent(void) { return 0; }
int cuCtxSynchronize(void) { return 0; }
int cuLaunchKernel(void) { return 0; }
int cuMemcpy(void) { return 0; }
int cuMemcpyAsync(void) { return 0; }
int cuMemcpyDtoH_v2(void) { return 0; }
int cuMemcpyDtoHAsync_v2(void) { return 0; }
int cuMemcpyHtoD_v2(void) { return 0; }
int cuMemcpyHtoDAsync_v2(void) { return 0; }
int cuMemcpyDtoD_v2(void) { return 0; }
int cuMemcpyDtoDAsync_v2(void) { return 0; }
