/*
 * Copyright (c) 2023 Georgios Alexopoulos
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
 * Minimal CUDA declarations needed to compile nvshare.
 */

#ifndef _CUDA_DEFS_H
#define _CUDA_DEFS_H

/*
 * Give the preprocessor a chance to replace a function name.
 * The CUDA Driver API header file (cuda.h) uses #define directives which map
 * common names (e.g. cuMemAlloc) to more specific ones (e.g. cuMemAlloc_v2)
 * depending on library version.
 *
 * More on stringification: https://gcc.gnu.org/onlinedocs/gcc-3.4.6/cpp/Stringification.html
 */
#define STRINGIFY(x)                #x
#define CUDA_SYMBOL_STRING(x)       STRINGIFY(x)

#define cuMemGetInfo                cuMemGetInfo_v2
#define cuMemAlloc                  cuMemAlloc_v2
#define cuMemFree                   cuMemFree_v2
#define cuMemcpyHtoD                cuMemcpyHtoD_v2
#define cuMemcpyDtoH                cuMemcpyDtoH_v2
#define cuMemcpyDtoD                cuMemcpyDtoD_v2
#define cuMemcpyHtoDAsync           cuMemcpyHtoDAsync_v2
#define cuMemcpyDtoHAsync           cuMemcpyDtoHAsync_v2
#define cuMemcpyDtoDAsync           cuMemcpyDtoDAsync_v2

#define nvmlInit                    nvmlInit_v2
#define nvmlDeviceGetHandleByIndex  nvmlDeviceGetHandleByIndex_v2

#include <stdint.h>

typedef uint64_t cuuint64_t;
typedef unsigned long long CUdeviceptr_v2;
typedef CUdeviceptr_v2 CUdeviceptr;
typedef int CUdevice;

/* Opaque pointers */
typedef struct CUctx_st *CUcontext;
typedef struct CUstream_st *CUstream;
typedef struct CUfunc_st *CUfunction;
typedef struct nvmlDevice_st* nvmlDevice_t;

typedef enum cuda_drv_error_enum {
	CUDA_SUCCESS               = 0,
	CUDA_ERROR_OUT_OF_MEMORY   = 2,
	CUDA_ERROR_NOT_INITIALIZED = 3,
	CUDA_ERROR_UNKNOWN         = 999
} CUresult;

typedef enum CUmemAttach_flags_enum {
	CU_MEM_ATTACH_GLOBAL = 0x1
} CUmemAttach_flags;

typedef enum nvmlReturn_t_enum {
	NVML_SUCCESS = 0,
	NVML_ERROR_UNKNOWN = 999
} nvmlReturn_t;

/*
 * Utilization information for a device.
 * Each sample period may be between 1 second and 1/6 second, depending on
 * the GPU being queried.
 */
typedef struct nvmlUtilization_st {
	/*
	 * Percent of time over the past sample period during which one or more
	 * kernels was executing on the GPU.
	 */
	unsigned int gpu;
	/*
	 * Percent of time over the past sample period during which global
	 * (device) memory was being read or written
	 */
	unsigned int memory;
} nvmlUtilization_t;

/* typedefs for CUDA functions, to make hooking code cleaner */
typedef CUresult (*cuGetProcAddress_func)(const char *symbol, void **pfn,
	int cudaVersion, cuuint64_t flags);
typedef CUresult (*cuMemAllocManaged_func)(CUdeviceptr *dptr, size_t bytesize,
	unsigned int flags);
typedef CUresult (*cuMemFree_func)(CUdeviceptr dptr);
typedef CUresult (*cuMemGetInfo_func)(size_t *free, size_t *total);
typedef CUresult (*cuGetErrorString_func)(CUresult error, const char **pStr);
typedef CUresult (*cuGetErrorName_func)(CUresult error, const char **pStr);
typedef CUresult (*cuCtxSetCurrent_func)(CUcontext ctx);
typedef CUresult (*cuCtxGetCurrent_func)(CUcontext *pctx);
typedef CUresult (*cuInit_func)(unsigned int flags);
typedef CUresult (*cuCtxSynchronize_func)(void);
typedef CUresult (*cuLaunchKernel_func)(CUfunction f, unsigned int gridDimX,
	unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX,
	unsigned int blockDimY, unsigned int blockDimZ,
	unsigned int sharedMemBytes, CUstream hStream, void **kernelParams,
	void **extra);
typedef CUresult (*cuMemcpy_func)(CUdeviceptr dst, CUdeviceptr src,
	size_t ByteCount);
typedef CUresult (*cuMemcpyAsync_func)(CUdeviceptr dst, CUdeviceptr src,
	size_t ByteCount, CUstream hStream);
typedef CUresult (*cuMemcpyDtoH_func)(void *dstHost, CUdeviceptr srcDevice,
	size_t ByteCount);
typedef CUresult (*cuMemcpyHtoD_func)(CUdeviceptr dstDevice,
	const void* srcHost, size_t ByteCount);
typedef CUresult (*cuMemcpyDtoD_func)(CUdeviceptr dstDevice,
	CUdeviceptr srcDevice, size_t ByteCount);
typedef CUresult (*cuMemcpyDtoHAsync_func)(void* dstHost,
	CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);
typedef CUresult (*cuMemcpyHtoDAsync_func)(CUdeviceptr dstDevice,
	const void* srcHost, size_t ByteCount, CUstream hStream);
typedef CUresult (*cuMemcpyDtoDAsync_func)(CUdeviceptr dstDevice,
	CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);

typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_func)(nvmlDevice_t device,
	nvmlUtilization_t *utilization);
typedef nvmlReturn_t (*nvmlInit_func)(void);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_func)(unsigned int index,
	nvmlDevice_t *device);


/* Hooked CUDA functions */
extern CUresult cuGetProcAddress(const char *symbol, void **pfn,
	int cudaVersion, cuuint64_t flags);
extern CUresult cuMemGetInfo(size_t *free, size_t *total);
extern CUresult cuMemAlloc(CUdeviceptr *dptr, size_t bytesize);
extern CUresult cuMemFree(CUdeviceptr dptr);
extern CUresult cuInit(unsigned int flags);
extern CUresult cuLaunchKernel(CUfunction f, unsigned int gridDimX,
	unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX,
	unsigned int blockDimY, unsigned int blockDimZ,
	unsigned int sharedMemBytes, CUstream hStream, void **kernelParams,
	void **extra);
extern CUresult cuMemcpy(CUdeviceptr dst, CUdeviceptr src,
	size_t ByteCount);
extern CUresult cuMemcpyAsync(CUdeviceptr dst, CUdeviceptr src,
	size_t ByteCount, CUstream hStream);
extern CUresult cuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice,
	size_t ByteCount);
extern CUresult cuMemcpyHtoD(CUdeviceptr dstDevice,
	const void* srcHost, size_t ByteCount);
extern CUresult cuMemcpyDtoD(CUdeviceptr dstDevice,
	CUdeviceptr srcDevice, size_t ByteCount);
extern CUresult cuMemcpyDtoHAsync(void* dstHost,
	CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);
extern CUresult cuMemcpyHtoDAsync(CUdeviceptr dstDevice,
	const void* srcHost, size_t ByteCount, CUstream hStream);
extern CUresult cuMemcpyDtoDAsync(CUdeviceptr dstDevice,
	CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);

/* Real CUDA functions */
extern cuGetProcAddress_func real_cuGetProcAddress;
extern cuMemAllocManaged_func real_cuMemAllocManaged;
extern cuMemFree_func real_cuMemFree;
extern cuMemGetInfo_func real_cuMemGetInfo;
extern cuGetErrorString_func real_cuGetErrorString;
extern cuGetErrorName_func real_cuGetErrorName;
extern cuCtxSetCurrent_func real_cuCtxSetCurrent;
extern cuCtxGetCurrent_func real_cuCtxGetCurrent;
extern cuInit_func real_cuInit;
extern cuCtxSynchronize_func real_cuCtxSynchronize;
extern cuLaunchKernel_func real_cuLaunchKernel;
extern cuMemcpy_func real_cuMemcpy;
extern cuMemcpyAsync_func real_cuMemcpyAsync;
extern cuMemcpyDtoH_func real_cuMemcpyDtoH;
extern cuMemcpyDtoHAsync_func real_cuMemcpyDtoHAsync;
extern cuMemcpyHtoD_func real_cuMemcpyHtoD;
extern cuMemcpyHtoDAsync_func real_cuMemcpyHtoDAsync;
extern cuMemcpyDtoD_func real_cuMemcpyDtoD;
extern cuMemcpyDtoDAsync_func real_cuMemcpyDtoDAsync;

extern void cuda_driver_check_error(CUresult err, const char *func_name);

/* NVML functions used to monitor the GPU utilization rate. */
extern nvmlDeviceGetUtilizationRates_func real_nvmlDeviceGetUtilizationRates;
extern nvmlInit_func real_nvmlInit;
extern nvmlDeviceGetHandleByIndex_func real_nvmlDeviceGetHandleByIndex;
extern CUcontext cuda_ctx;

extern int nvml_ok;

#endif /* _CUDA_DEFS_H */

