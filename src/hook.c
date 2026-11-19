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
 *
 * Hook CUDA function calls.
 */

/*
 * Defining _GNU_SOURCE allows us to call dlvsym().
 *
 * More on _GNU_SOURCE: https://stackoverflow.com/a/5583764
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>

#include "comm.h"
#include "common.h"
#include "cuda_defs.h"
#include "client.h"
#include "utlist.h"

#define ENV_NVSHARE_ENABLE_SINGLE_OVERSUB  "NVSHARE_ENABLE_SINGLE_OVERSUB"

#define MEMINFO_RESERVE_MIB 1536           /* MiB */
#define KERN_SYNC_DURATION_BIG 10          /* seconds */
#define KERN_SYNC_WINDOW_STEPDOWN_THRESH 1 /* seconds */
#define KERN_SYNC_WINDOW_MAX 2048          /* Pending Kernels */

static void *real_dlsym_225(void *handle, const char *symbol);
static void *get_variant_hook(const char *symbol);
static void *hook_for_real(const char *symbol, void *fn);

cuCtxSynchronize_func real_cuCtxSynchronize = NULL;
cuLaunchKernel_func real_cuLaunchKernel = NULL;
cuMemcpy_func real_cuMemcpy = NULL;
cuMemcpyAsync_func real_cuMemcpyAsync = NULL;
cuMemcpyDtoH_func real_cuMemcpyDtoH = NULL;
cuMemcpyDtoHAsync_func real_cuMemcpyDtoHAsync = NULL;
cuMemcpyHtoD_func real_cuMemcpyHtoD = NULL;
cuMemcpyHtoDAsync_func real_cuMemcpyHtoDAsync = NULL;
cuMemcpyDtoD_func real_cuMemcpyDtoD = NULL;
cuMemcpyDtoDAsync_func real_cuMemcpyDtoDAsync = NULL;
cuGetProcAddress_func real_cuGetProcAddress = NULL;
cuGetProcAddress_v2_func real_cuGetProcAddress_v2 = NULL;
cuMemAllocManaged_func real_cuMemAllocManaged = NULL;
cuMemFree_func real_cuMemFree = NULL;
cuMemGetInfo_func real_cuMemGetInfo = NULL;
cuGetErrorString_func real_cuGetErrorString = NULL;
cuGetErrorName_func real_cuGetErrorName = NULL;
cuCtxSetCurrent_func real_cuCtxSetCurrent = NULL;
cuCtxGetCurrent_func real_cuCtxGetCurrent = NULL;
cuInit_func real_cuInit = NULL;
cuLaunchKernelEx_func real_cuLaunchKernelEx = NULL;
cuLaunchKernelEx_func real_cuLaunchKernelEx_ptsz = NULL;
cuLaunchCooperativeKernel_func real_cuLaunchCooperativeKernel = NULL;
cuLaunchCooperativeKernel_func real_cuLaunchCooperativeKernel_ptsz = NULL;
cuGraphLaunch_func real_cuGraphLaunch = NULL;
cuGraphLaunch_func real_cuGraphLaunch_ptsz = NULL;
cuStreamBeginCapture_func real_cuStreamBeginCapture = NULL;
cuStreamBeginCapture_func real_cuStreamBeginCapture_ptsz = NULL;
cuStreamBeginCapture_v2_func real_cuStreamBeginCapture_v2 = NULL;
cuStreamBeginCapture_v2_func real_cuStreamBeginCapture_v2_ptsz = NULL;
cuStreamBeginCaptureToGraph_func real_cuStreamBeginCaptureToGraph = NULL;
cuStreamBeginCaptureToGraph_func real_cuStreamBeginCaptureToGraph_ptsz = NULL;
cuStreamEndCapture_func real_cuStreamEndCapture = NULL;
cuStreamEndCapture_func real_cuStreamEndCapture_ptsz = NULL;
cuStreamIsCapturing_func real_cuStreamIsCapturing = NULL;

/* Number of open stream captures. Syncing while one is open breaks it (#28). */
pthread_mutex_t capture_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t capture_cv = PTHREAD_COND_INITIALIZER;
int active_captures = 0;

nvmlDeviceGetUtilizationRates_func real_nvmlDeviceGetUtilizationRates = NULL;
nvmlInit_func real_nvmlInit = NULL;
nvmlDeviceGetHandleByIndex_func real_nvmlDeviceGetHandleByIndex = NULL;

size_t nvshare_size_mem_allocatable = 0;
size_t sum_allocated = 0;

int kern_since_sync = 0;
int pending_kernel_window = 1;
pthread_mutex_t kcount_mutex;

int enable_single_oversub = 0;
int nvml_ok = 1;

static void *libcuda_handle = NULL;
static void *libcuda_base = NULL;

/* Representation of a CUDA memory allocation */
struct cuda_mem_allocation {
	CUdeviceptr ptr;
	size_t size;
	struct cuda_mem_allocation *next;
};

/* Linked list that holds all memory allocations of current application. */
struct cuda_mem_allocation *cuda_allocation_list = NULL;

/* Initializaters will be executed only once per client application */
static pthread_once_t init_libnvshare_done = PTHREAD_ONCE_INIT;
static pthread_once_t init_done = PTHREAD_ONCE_INIT;

static void *optional_sym(void *handle, const char *symbol)
{
	void *sym;
	char *error;

	dlerror();
	sym = real_dlsym_225(handle, symbol);
	error = dlerror();
	if (error != NULL) {
		log_debug("%s", error);
		return NULL;
	}
	return sym;
}

/* Load real CUDA {Driver API, NVML} functions and bootstrap auxiliary stuff. */
static void bootstrap_cuda(void)
{
	char *error;
	void *cuda_handle;
	void *nvml_handle;


	true_or_exit(pthread_mutex_init(&kcount_mutex, NULL) == 0);

	nvml_handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
	if (!nvml_handle) {
		error = dlerror();
		log_debug("%s", error);
		nvml_ok = 0;
	} else {
		dlerror();
		real_nvmlDeviceGetUtilizationRates =
			(nvmlDeviceGetUtilizationRates_func)real_dlsym_225(nvml_handle,
			CUDA_SYMBOL_STRING(nvmlDeviceGetUtilizationRates));
		error = dlerror();
		if (error != NULL) {
			log_debug("%s", error);
			nvml_ok = 0;
		}
		real_nvmlInit = (nvmlInit_func)
		real_dlsym_225(nvml_handle,CUDA_SYMBOL_STRING(nvmlInit));
		error = dlerror();
		if (error != NULL) {
			log_debug("%s", error);
			nvml_ok = 0;
		}
		real_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_func)
		real_dlsym_225(nvml_handle,
			CUDA_SYMBOL_STRING(nvmlDeviceGetHandleByIndex));
		error = dlerror();
		if (error != NULL) {
			log_debug("%s", error);
			nvml_ok = 0;
		}
	}
	if (nvml_ok) log_debug("Found NVML");
	else log_debug("Could not find NVML");

	cuda_handle = dlopen("libcuda.so", RTLD_LAZY);
	if (!cuda_handle) {
		error = dlerror();
		log_fatal("%s", error);
	}
	/*
	 * For dlsym(), a return value of NULL does not necessarily indicate
	 * an error. Therefore, we must:
	 *  1. clear the previous error state by calling dlerror()
	 *  2. call dlsym()
	 *  3. call dlerror()
	 * If the value which dlerror() returns is not NULL, an error occured.
	 */
	dlerror();
	real_cuMemAllocManaged = (cuMemAllocManaged_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemAllocManaged));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemFree = (cuMemFree_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemFree));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuGetProcAddress = (cuGetProcAddress_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuGetProcAddress));
	error = dlerror();
	if (error != NULL)
		/*
		 * Print a debug message instead of failing immediately, since
		 * this symbol may not be used. This may be the case for CUDA
		 * Runtime <11.3.
		 */
		log_debug("%s", error);
	real_cuGetProcAddress_v2 = (cuGetProcAddress_v2_func)
		real_dlsym_225(cuda_handle, CUDA_SYMBOL_STRING(cuGetProcAddress_v2));
	error = dlerror();
	if (error != NULL)
		/*
		 * Print a debug message instead of failing immediately, since
		 * this symbol may not be used. This may be the case for CUDA
		 * Runtime <12.0.
		 */
		log_debug("%s", error);
	real_cuMemGetInfo = (cuMemGetInfo_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemGetInfo));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuGetErrorString = (cuGetErrorString_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuGetErrorString));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuGetErrorName = (cuGetErrorString_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuGetErrorName));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuCtxSetCurrent = (cuCtxSetCurrent_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuCtxSetCurrent));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuCtxGetCurrent = (cuCtxGetCurrent_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuCtxGetCurrent));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuInit = (cuInit_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuInit));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuCtxSynchronize = (cuCtxSynchronize_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuCtxSynchronize));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuLaunchKernel = (cuLaunchKernel_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuLaunchKernel));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpy = (cuMemcpy_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpy));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyAsync = (cuMemcpyAsync_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyAsync));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyDtoH = (cuMemcpyDtoH_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyDtoH));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyDtoHAsync = (cuMemcpyDtoHAsync_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyDtoHAsync));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyHtoD = (cuMemcpyHtoD_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyHtoD));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyHtoDAsync = (cuMemcpyHtoDAsync_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyHtoDAsync));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyDtoD = (cuMemcpyDtoD_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyDtoD));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);
	real_cuMemcpyDtoDAsync = (cuMemcpyDtoDAsync_func)
		real_dlsym_225(cuda_handle,CUDA_SYMBOL_STRING(cuMemcpyDtoDAsync));
	error = dlerror();
	if (error != NULL)
		log_fatal("%s", error);

	/* Optional: older drivers lack some of these. */
	real_cuLaunchKernelEx = (cuLaunchKernelEx_func)optional_sym(cuda_handle, "cuLaunchKernelEx");
	real_cuLaunchKernelEx_ptsz = (cuLaunchKernelEx_func)optional_sym(cuda_handle, "cuLaunchKernelEx_ptsz");
	real_cuLaunchCooperativeKernel = (cuLaunchCooperativeKernel_func)optional_sym(cuda_handle, "cuLaunchCooperativeKernel");
	real_cuLaunchCooperativeKernel_ptsz = (cuLaunchCooperativeKernel_func)optional_sym(cuda_handle, "cuLaunchCooperativeKernel_ptsz");
	real_cuGraphLaunch = (cuGraphLaunch_func)optional_sym(cuda_handle, "cuGraphLaunch");
	real_cuGraphLaunch_ptsz = (cuGraphLaunch_func)optional_sym(cuda_handle, "cuGraphLaunch_ptsz");
	real_cuStreamBeginCapture = (cuStreamBeginCapture_func)optional_sym(cuda_handle, "cuStreamBeginCapture");
	real_cuStreamBeginCapture_ptsz = (cuStreamBeginCapture_func)optional_sym(cuda_handle, "cuStreamBeginCapture_ptsz");
	real_cuStreamBeginCapture_v2 = (cuStreamBeginCapture_v2_func)optional_sym(cuda_handle, "cuStreamBeginCapture_v2");
	real_cuStreamBeginCapture_v2_ptsz = (cuStreamBeginCapture_v2_func)optional_sym(cuda_handle, "cuStreamBeginCapture_v2_ptsz");
	real_cuStreamBeginCaptureToGraph = (cuStreamBeginCaptureToGraph_func)optional_sym(cuda_handle, "cuStreamBeginCaptureToGraph");
	real_cuStreamBeginCaptureToGraph_ptsz = (cuStreamBeginCaptureToGraph_func)optional_sym(cuda_handle, "cuStreamBeginCaptureToGraph_ptsz");
	real_cuStreamEndCapture = (cuStreamEndCapture_func)optional_sym(cuda_handle, "cuStreamEndCapture");
	real_cuStreamEndCapture_ptsz = (cuStreamEndCapture_func)optional_sym(cuda_handle, "cuStreamEndCapture_ptsz");
	real_cuStreamIsCapturing = (cuStreamIsCapturing_func)optional_sym(cuda_handle, "cuStreamIsCapturing");

	Dl_info cuda_info;
	if (dladdr((void *)real_cuInit, &cuda_info) == 0)
		log_fatal("dladdr() failed for the real cuInit");
	libcuda_handle = cuda_handle;
	libcuda_base = cuda_info.dli_fbase;
}


/* Append a new CUDA memory allocation at the end of the list. */
static void insert_cuda_allocation(CUdeviceptr dptr, size_t bytesize)
{
	struct cuda_mem_allocation *allocation;


	sum_allocated += bytesize;
	log_debug("Total allocated memory on GPU is %.2f MiB",
		  toMiB(sum_allocated));

	true_or_exit(allocation = malloc(sizeof(*allocation)));

	allocation->ptr = dptr;
	allocation->size = bytesize;
	allocation->next = NULL;
	LL_APPEND(cuda_allocation_list, allocation);
}

/* Remove a CUDA memory allocation given the pointer it starts at */
static void remove_cuda_allocation(CUdeviceptr rm_ptr)
{
	struct cuda_mem_allocation *tmp, *a;


	LL_FOREACH_SAFE(cuda_allocation_list, a, tmp) {
		if (a->ptr == rm_ptr) {
			sum_allocated -= a->size;
			log_debug("Total allocated memory on GPU is %.2f MiB",
				  toMiB(sum_allocated));
			LL_DELETE(cuda_allocation_list, a);
			free(a);
		}
	}
}


/* Toggle debug mode and single process oversubscription based on envvars */
static void initialize_libnvshare(void)
{
	char *value;
	value = getenv(ENV_NVSHARE_DEBUG);
	if (value != NULL)
		__debug = 1;
	value = getenv(ENV_NVSHARE_ENABLE_SINGLE_OVERSUB);
	if (value != NULL) {
		enable_single_oversub = 1;
		log_warn("Enabling GPU memory oversubscription for this"
		         " application");
	}

	bootstrap_cuda();
}


/*
 * Check the return value of a CUDA Driver API function call for errors.
 *
 * Interpret using the Driver API functions:
 * - cuGetErrorString
 * - cuGetErrorName
 */
void cuda_driver_check_error(CUresult err, const char *func_name)
{
	if (err != CUDA_SUCCESS) {
		const char *err_string = "unknown";
		const char *err_name = "unknown";
		/* These fail and leave the strings unset during CUDA teardown */
		if (real_cuGetErrorString(err, &err_string) != CUDA_SUCCESS)
			err_string = "unknown";
		if (real_cuGetErrorName(err, &err_name) != CUDA_SUCCESS)
			err_name = "unknown";
		log_warn("%s returned %s: %s",
	                 func_name, err_name, err_string);
	}
}


/*
 * Since we're interposing dlsym() in libnvshare, we use dlvsym() to obtain the
 * address of the real dlsym function.
 *
 * Depending on glibc version, we look for the appropriate symbol.
 *
 * Some context on the implementation:
 *
 * glibc 2.34 remove the internal __libc_dlsym() symbol that NVIDIA uses in
 * their cuHook example:
 * https://github.com/phrb/intro-cuda/blob/d38323b81cd799dc09179e2ef27aa8f81b6dac40/src/cuda-samples/7_CUDALibraries/cuHook/libcuhook.cpp#L43
 *
 * One solution, discussed in apitrace's repo is to use dlvsym(), which also
 * takes a version string as a 3rd argument, in order to obtain the real
 * dlsym().
 *
 * This is what user 'manisandro' suggested 8 years ago, when warning about
 * using the private __libc_dlsym():
 * https://github.com/apitrace/apitrace/issues/258
 *
 * The maintainer of the repo didn't heed the warning back then, it came back
 * 8 years later and bit them.
 *
 * This is also what user "derhass" suggests:
 * https://stackoverflow.com/a/18825060
 * (See section "UPDATE FOR 2021/glibc-2.34").
 *
 * Given all the above, we obtain the real `dlsym()` as such:
 * real_dlsym=dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
 *
 * Since we have to explicitly use a version argument in dlvsym(), we also have
 * to define and export two versions of dlsym (hence the linker script.), one
 * for each distinct glibc symbol version.
 *
 */
static void *real_dlsym_225(void *handle, const char *symbol)
{
	typedef void *(dlsym_t)(void *, const char *);
	static dlsym_t *r_dlsym;
	char *err;


	if (!r_dlsym) {
		dlerror();
		r_dlsym = (dlsym_t*)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
		err = dlerror();
		if (err != NULL)
			log_fatal("%s", err);
	}

	return (*r_dlsym)(handle, symbol);
}

static void *real_dlsym_234(void *handle, const char *symbol)
{
	typedef void *(dlsym_t)(void *, const char *);
	static dlsym_t *r_dlsym;
	char *err;


	if (!r_dlsym) {
		dlerror();
		r_dlsym = (dlsym_t*)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
		err = dlerror();
		if (err != NULL)
			log_fatal("%s", err);
	}

	return (*r_dlsym)(handle, symbol);
}


/*
 * CUDA Runtime API uses dlopen()/dlsym() to obtain addresses of the Driver API
 * functions.
 *
 * [spoiler: from CUDA 11.3 onwards, it only uses dlsym() to get the address
 *  of cuGetProcAddress() and then uses the latter to obtain the addresses
 *  of all other Driver API functions/symbols.]
 *
 * When the user program calls dlsym() requesting a Driver API symbol, return
 * our interposed version.
 *
 * In all other cases, call the real dlsym() from glibc and pass on the
 * requested symbol string.
 */
static void *get_hook(const char *symbol)
{
	if (strncmp(symbol, "cu", 2) != 0) {
		return NULL;
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemAlloc)) == 0) {
		return (void *)(&cuMemAlloc);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemFree)) == 0) {
		return (void *)(&cuMemFree);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemGetInfo)) == 0) {
		return (void *)(&cuMemGetInfo);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuGetProcAddress)) == 0) {
		return (void *)(&cuGetProcAddress);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuGetProcAddress_v2)) == 0) {
		return (void *)(&cuGetProcAddress_v2);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuInit)) == 0) {
		return (void *)(&cuInit);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuLaunchKernel)) == 0) {
		return (void *)(&cuLaunchKernel);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpy)) == 0) {
		return (void *)(&cuMemcpy);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyAsync)) == 0) {
		return (void *)(&cuMemcpyAsync);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoH)) == 0) {
		return (void *)(&cuMemcpyDtoH);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoHAsync)) == 0) {
		return (void *)(&cuMemcpyDtoHAsync);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyHtoD)) == 0) {
		return (void *)(&cuMemcpyHtoD);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyHtoDAsync)) == 0) {
		return (void *)(&cuMemcpyHtoDAsync);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoD)) == 0) {
		return (void *)(&cuMemcpyDtoD);
	} else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoDAsync)) == 0) {
		return (void *)(&cuMemcpyDtoDAsync);
	}

	return get_variant_hook(symbol);
}

/* Some drivers dlsym() their own functions inside cuInit() (#27). */
static int called_from_libcuda(const void *caller)
{
	Dl_info info;

	if (libcuda_base == NULL)
		return 0;
	if (dladdr(caller, &info) == 0)
		return 0;
	return info.dli_fbase == libcuda_base;
}

/* Use our libcuda handle: RTLD_DEFAULT would find our preloaded wrapper. */
void *dlsym_225(void *handle, const char *symbol)
{
	void *hook = get_hook(symbol);

	if (hook == NULL)
		return real_dlsym_225(handle, symbol);
	if (called_from_libcuda(__builtin_return_address(0)))
		return real_dlsym_225(libcuda_handle, symbol);
	return hook;
}

void *dlsym_234(void *handle, const char *symbol)
{
	void *hook = get_hook(symbol);

	if (hook == NULL)
		return real_dlsym_234(handle, symbol);
	if (called_from_libcuda(__builtin_return_address(0)))
		return real_dlsym_234(libcuda_handle, symbol);
	return hook;
}


/*
 * Older CUDA Runtime API (version <=11.2) does the following during internal
 * initialization (when the user program calls it for the first time):
 * 1. Calls dlopen("libcuda.so.1")
 * 2. Calls dlsym() for each function in the Driver API
 *
 * Newer CUDA Runtime API (version >=11.3) works like this:
 * 1. Calls dlopen("libcuda.so.1") and then dlsym("cuGetProcAddress")
 * 2. Calls cuGetProcAddress("cuGetProcAddress")
 *     1. If the pointer to "cuGetProcAddress" is NULL, it falls back to using
 *        dlsym() to get the Driver API function pointers
 *     2. If the pointer to "cuGetProcAddress" is not NULL, it uses
 *        cuGetProcAddress to get the Driver API function pointers.
 *
 * Interpose both, to cover all cases.
 *
 * The logic is the same as when interposing dlsym().
 */
CUresult cuGetProcAddress(const char *symbol, void **pfn, int cudaVersion,
	cuuint64_t flags)
{
	/*
	* cuGetProcAddress() will be called before cuInit() in CUDA
	* Runtime API (version >=11.3), so cuGetProcAddress() should also
	* serve as an entrypoint.
	* Otherwise, real_cuGetProcAddress may be a NULL pointer
	* when it is called.
	*/
	true_or_exit(pthread_once(&init_libnvshare_done, initialize_libnvshare) == 0);
	true_or_exit(pthread_once(&init_done, initialize_client) == 0);
	CUresult result = CUDA_SUCCESS;

	if (real_cuGetProcAddress == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	if (strcmp(symbol, "cuMemAlloc") == 0) {
		*pfn = (void *)(&cuMemAlloc);
	} else if (strcmp(symbol, "cuMemFree") == 0) {
		*pfn = (void *)(&cuMemFree);
	} else if (strcmp(symbol, "cuMemGetInfo") == 0) {
		*pfn = (void *)(&cuMemGetInfo);
	} else if (strcmp(symbol, "cuGetProcAddress") == 0) {
		*pfn = (void *)(&cuGetProcAddress);
	} else if (strcmp(symbol, "cuGetProcAddress_v2") == 0) {
		*pfn = (void *)(&cuGetProcAddress_v2);
	} else if (strcmp(symbol, "cuInit") == 0) {
		*pfn = (void *)(&cuInit);
	} else if (strcmp(symbol, "cuLaunchKernel") == 0) {
		*pfn = (void *)(&cuLaunchKernel);
	} else if (strcmp(symbol, "cuMemcpy") == 0) {
		*pfn = (void *)(&cuMemcpy);
	} else if (strcmp(symbol, "cuMemcpyAsync") == 0) {
		*pfn = (void *)(&cuMemcpyAsync);
	} else if (strcmp(symbol, "cuMemcpyDtoH") == 0) {
		*pfn = (void *)(&cuMemcpyDtoH);
	} else if (strcmp(symbol, "cuMemcpyDtoHAsync") == 0) {
		*pfn = (void *)(&cuMemcpyDtoHAsync);
	} else if (strcmp(symbol, "cuMemcpyHtoD") == 0) {
		*pfn = (void *)(&cuMemcpyHtoD);
	} else if (strcmp(symbol, "cuMemcpyHtoDAsync") == 0) {
		*pfn = (void *)(&cuMemcpyHtoDAsync);
	} else if (strcmp(symbol, "cuMemcpyDtoD") == 0) {
		*pfn = (void *)(&cuMemcpyDtoD);
	} else if (strcmp(symbol, "cuMemcpyDtoDAsync") == 0) {
		*pfn = (void *)(&cuMemcpyDtoDAsync);
	} else {
		result = real_cuGetProcAddress(symbol, pfn, cudaVersion, flags);
		if (result == CUDA_SUCCESS)
			*pfn = hook_for_real(symbol, *pfn);
	}

	return result;
}


CUresult cuGetProcAddress_v2(const char *symbol, void **pfn, int cudaVersion,
	cuuint64_t flags, CUdriverProcAddressQueryResult *symbolStatus)
{
	/*
	* cuGetProcAddress_v2() will be called before cuInit() in CUDA
	* Runtime API (version >=12.0), so cuGetProcAddress_v2()
	* should also serve as an entrypoint.
	*
	* Otherwise, real_cuGetProcAddress_v2 may be a
	* NULL pointer when it is called.
	*/
	true_or_exit(pthread_once(&init_libnvshare_done, initialize_libnvshare) == 0);
	true_or_exit(pthread_once(&init_done, initialize_client) == 0);
	CUresult result = CUDA_SUCCESS;

	if (real_cuGetProcAddress_v2 == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	/* This covers our custom "if" conditions.
	 * If we end up calling the real cuGetProcAddress_v2,
	 * it will overwrite this value.
	 */
	if (symbolStatus != NULL)
		*symbolStatus = CU_GET_PROC_ADDRESS_SUCCESS;

	if (strcmp(symbol, "cuMemAlloc") == 0) {
		*pfn = (void *)(&cuMemAlloc);
	} else if (strcmp(symbol, "cuMemFree") == 0) {
		*pfn = (void *)(&cuMemFree);
	} else if (strcmp(symbol, "cuMemGetInfo") == 0) {
		*pfn = (void *)(&cuMemGetInfo);
	} else if (strcmp(symbol, "cuGetProcAddress") == 0) {
		*pfn = (void *)(&cuGetProcAddress);
	} else if (strcmp(symbol, "cuGetProcAddress_v2") == 0) {
		*pfn = (void *)(&cuGetProcAddress_v2);
	} else if (strcmp(symbol, "cuInit") == 0) {
		*pfn = (void *)(&cuInit);
	} else if (strcmp(symbol, "cuLaunchKernel") == 0) {
		*pfn = (void *)(&cuLaunchKernel);
	} else if (strcmp(symbol, "cuMemcpy") == 0) {
		*pfn = (void *)(&cuMemcpy);
	} else if (strcmp(symbol, "cuMemcpyAsync") == 0) {
		*pfn = (void *)(&cuMemcpyAsync);
	} else if (strcmp(symbol, "cuMemcpyDtoH") == 0) {
		*pfn = (void *)(&cuMemcpyDtoH);
	} else if (strcmp(symbol, "cuMemcpyDtoHAsync") == 0) {
		*pfn = (void *)(&cuMemcpyDtoHAsync);
	} else if (strcmp(symbol, "cuMemcpyHtoD") == 0) {
		*pfn = (void *)(&cuMemcpyHtoD);
	} else if (strcmp(symbol, "cuMemcpyHtoDAsync") == 0) {
		*pfn = (void *)(&cuMemcpyHtoDAsync);
	} else if (strcmp(symbol, "cuMemcpyDtoD") == 0) {
		*pfn = (void *)(&cuMemcpyDtoD);
	} else if (strcmp(symbol, "cuMemcpyDtoDAsync") == 0) {
		*pfn = (void *)(&cuMemcpyDtoDAsync);
	} else {
		result = real_cuGetProcAddress_v2(symbol, pfn, cudaVersion,
				                  flags, symbolStatus);
		if (result == CUDA_SUCCESS)
			*pfn = hook_for_real(symbol, *pfn);
	}

	return result;
}


CUresult cuMemAlloc(CUdeviceptr *dptr, size_t bytesize)
{
	static int got_max_mem_size = 0;
	size_t junk;
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemAllocManaged == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	if (got_max_mem_size == 0) {
		result = cuMemGetInfo(&nvshare_size_mem_allocatable, &junk);
		cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemGetInfo));
		got_max_mem_size = 1;
	}

	if ((sum_allocated + bytesize) > nvshare_size_mem_allocatable) {
		if (enable_single_oversub == 0) {
			return CUDA_ERROR_OUT_OF_MEMORY;
		} else {
			log_warn("Memory allocations exceeded physical GPU"
				 " memory capacity. This can cause extreme"
				 " performance degradation!");
		}
	}

	log_debug("cuMemAlloc requested %zu bytes", bytesize);
	result = real_cuMemAllocManaged(dptr, bytesize, CU_MEM_ATTACH_GLOBAL);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemAllocManaged));
	log_debug("cuMemAllocManaged allocated %zu bytes at 0x%llx",
		bytesize, *dptr);
	if (result == CUDA_SUCCESS) {
		insert_cuda_allocation(*dptr, bytesize);
	}

	return result;
}


CUresult cuMemFree(CUdeviceptr dptr)
{
	CUresult result = CUDA_SUCCESS;


	if (real_cuMemFree == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	result = real_cuMemFree(dptr);
	if (result == CUDA_SUCCESS) remove_cuda_allocation(dptr);

	return result;
}


CUresult cuMemGetInfo(size_t *free, size_t *total)
{
	long long reserve_mib;
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemGetInfo == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	result = real_cuMemGetInfo(free, total);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemGetInfo));

	log_debug("real_cuMemGetInfo returned free=%.2f MiB, total=%.2f MiB",
		 toMiB(*free), toMiB(*total));

        /*
	 * Hide a static amount of GPU memory from the applications. CUDA uses
	 * this memory to store context information and it is not pageable.
	 *
	 * In practice, this amount of memory is not static and depends on
	 * the number of colocated applications. Each one has its own context,
	 * which eats away some physical, non-pageable GPU memory.
	 *
	 * The first application that runs theoretically has (TOTAL_GPU_MEM -
	 * CONTEXT_SIZE) memory available.
	 *
	 * CONTEXT_SIZE typically uses a few hundred MB and depends on the GPU
	 * model.
	 *
	 * cuBLAS and other CUDA libraries also eat away at this memory.
	 *
	 * When another app runs, this "working memory size" shrinks further
	 * and can lead to thrashing within the first application, even when
	 * it runs alone.
	 *
	 * We cannot shrink the memory allocations of a running app, and the
	 * app thinks all of its memory is physically backed, since it's
	 * programmed with cuMemAlloc semantics in mind.
	 *
	 * To avoid internal thrashing, we empirically choose a sane value for
	 * MEMINFO_RESERVE_MIB.
	 */
	reserve_mib = (MEMINFO_RESERVE_MIB) MiB;
	*free = *total - (size_t) reserve_mib;

	log_debug("nvshare's cuMemGetInfo returning free=%.2f MiB,"
		  " total=%.2f MiB", toMiB(*free), toMiB(*total));
	return result;
}

/*
 * A call to cuInit is an indicator that the present application is a CUDA
 * application and that we should bootstrap nvshare.
 */
CUresult cuInit(unsigned int flags)
{
	CUresult result = CUDA_SUCCESS;

	true_or_exit(pthread_once(&init_libnvshare_done, initialize_libnvshare) == 0);
	true_or_exit(pthread_once(&init_done, initialize_client) == 0);

	result = real_cuInit(flags);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuInit));

	return result;
}


/* Return 1 if work launched on hStream is being captured into a graph. */
static int stream_is_capturing(CUstream hStream)
{
	CUstreamCaptureStatus status;

	if (__atomic_load_n(&active_captures, __ATOMIC_ACQUIRE) == 0 ||
	    real_cuStreamIsCapturing == NULL)
		return 0;
	if (real_cuStreamIsCapturing(hStream, &status) != CUDA_SUCCESS)
		return 0;
	return status != CU_STREAM_CAPTURE_STATUS_NONE;
}

/* Count a launch toward the pending kernel window. */
static void count_launch(void)
{
	true_or_exit(pthread_mutex_lock(&kcount_mutex) == 0);
	/*
	 * Dynamic kernel submissing rate control.
	 *
	 * Some applications like to submit a huge amount of kernels in a short
	 * period of time.
	 *
	 * For nvshare, this means that they would still have pending kernels
	 * on the GPU when asked to relinquish the GPU lock.
	 *
	 * Since we sync the CUDA context before releasing the lock, this
	 * would mean we would hold the lock for much longer than TQ seconds,
	 * as that sync could possible take a very long time.
	 *
	 * To alleviate this source of unfairness, try to keep the completion
	 * time of submitted kernels to within 5 seconds, while simultaneously
	 * trying to maintain a good throughput rate for smaller kernels.
	 */
	kern_since_sync++;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	/* Don't sync during a capture; catch up after it ends */
	if (kern_since_sync >= pending_kernel_window && active_captures == 0) {
		struct timespec cuda_cuda_sync_start_time = {0, 0};
		struct timespec cuda_sync_complete_time = {0, 0};
		struct timespec cuda_sync_duration = {0, 0};
		true_or_exit(clock_gettime(CLOCK_MONOTONIC, &cuda_cuda_sync_start_time) == 0);
		CUresult result = real_cuCtxSynchronize();
		cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuCtxSynchronize));
		true_or_exit(clock_gettime(CLOCK_MONOTONIC, &cuda_sync_complete_time) == 0);
		timespecsub(&cuda_sync_complete_time, &cuda_cuda_sync_start_time, &cuda_sync_duration);

		/*
		 * Possibly a series of huge kernels. We cannot risk to
		 * simply fall back to previous window. Fall back to
		 * the initial window of 1.
		 */
		if (cuda_sync_duration.tv_sec >= KERN_SYNC_DURATION_BIG)
			pending_kernel_window = 1;

		/*
		 * Intermediate situation, don't be too harsh. Rein the
		 * rate in.
		 */
		else if (cuda_sync_duration.tv_sec >= KERN_SYNC_WINDOW_STEPDOWN_THRESH)
			pending_kernel_window = max(pending_kernel_window/2, 1);

		/*
		 * Max window size is simply a heuristic.
		 */
		else pending_kernel_window = min(pending_kernel_window*2,
				                 KERN_SYNC_WINDOW_MAX);

		log_debug("Pending Kernel Window is %d.", pending_kernel_window);
		kern_since_sync = 0;
	}
	true_or_exit(pthread_mutex_unlock(&capture_mutex) == 0);
	true_or_exit(pthread_mutex_unlock(&kcount_mutex) == 0);
}

CUresult cuLaunchKernel(CUfunction f, unsigned int gridDimX,
	unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX,
	unsigned int blockDimY, unsigned int blockDimZ,
	unsigned int sharedMemBytes, CUstream hStream, void **kernelParams,
	void **extra)
{
	CUresult result = CUDA_SUCCESS;

	/* Return immediately if not initialized */
	if (real_cuLaunchKernel == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	/* Captured, not run: no lock needed */
	if (stream_is_capturing(hStream))
		return real_cuLaunchKernel(f, gridDimX, gridDimY, gridDimZ,
			blockDimX, blockDimY, blockDimZ, sharedMemBytes, hStream,
			kernelParams, extra);

	continue_with_lock();
	result = real_cuLaunchKernel(f, gridDimX, gridDimY, gridDimZ, blockDimX,
		blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams, extra);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuLaunchKernel));

	count_launch();
	return result;
}


/*
 * Memory copy functions can affect the resident pages on GPU, so we must
 * block them as well when the client doesn't have the GPU lock.
 */
CUresult cuMemcpy(CUdeviceptr dst, CUdeviceptr src, size_t ByteCount)
{
	CUresult result = CUDA_SUCCESS;


	if (real_cuMemcpy == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();

	result = real_cuMemcpy(dst, src, ByteCount);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpy));

	return result;
}

CUresult cuMemcpyAsync(CUdeviceptr dst, CUdeviceptr src, size_t ByteCount,
	CUstream hStream)
{
	CUresult result = CUDA_SUCCESS;


	if (real_cuMemcpyAsync == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();

	result = real_cuMemcpyAsync(dst, src, ByteCount, hStream);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyAsync));

	return result;
}

CUresult cuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount)
{
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemcpyDtoH == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();
	result = real_cuMemcpyDtoH(dstHost, srcDevice, ByteCount);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyDtoH));

	return result;
}

CUresult cuMemcpyDtoHAsync(void* dstHost, CUdeviceptr srcDevice,
	size_t ByteCount, CUstream hStream)
{
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemcpyDtoHAsync == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();
	result = real_cuMemcpyDtoHAsync(dstHost, srcDevice, ByteCount, hStream);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyDtoHAsync));

	return result;
}

CUresult cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost,
	size_t ByteCount)
{
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemcpyHtoD == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();
	result = real_cuMemcpyHtoD(dstDevice, srcHost, ByteCount);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyHtoD));

	return result;
}

CUresult cuMemcpyHtoDAsync(CUdeviceptr dstDevice, const void* srcHost,
	size_t ByteCount, CUstream hStream)
{
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemcpyHtoDAsync == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();
	result = real_cuMemcpyHtoDAsync(dstDevice, srcHost, ByteCount, hStream);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyHtoDAsync));

	return result;
}

CUresult cuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice,
	size_t ByteCount)
{
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemcpyDtoD == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();
	result = real_cuMemcpyDtoD(dstDevice, srcDevice, ByteCount);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyDtoD));

	return result;
}

CUresult cuMemcpyDtoDAsync(CUdeviceptr dstDevice, CUdeviceptr srcDevice,
	size_t ByteCount, CUstream hStream)
{
	CUresult result = CUDA_SUCCESS;


	/* Return immediately if not initialized */
	if (real_cuMemcpyDtoDAsync == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	continue_with_lock();
	result = real_cuMemcpyDtoDAsync(dstDevice, srcDevice, ByteCount, hStream);
	cuda_driver_check_error(result, CUDA_SYMBOL_STRING(cuMemcpyDtoDAsync));

	return result;
}

static CUresult launch_ex(cuLaunchKernelEx_func real, const CUlaunchConfig *config,
	CUfunction f, void **kernelParams, void **extra, CUstream query_stream)
{
	CUresult result;

	if (real == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	if (stream_is_capturing(query_stream))
		return real(config, f, kernelParams, extra);

	continue_with_lock();
	result = real(config, f, kernelParams, extra);
	cuda_driver_check_error(result, "cuLaunchKernelEx");
	count_launch();
	return result;
}

CUresult cuLaunchKernelEx(const CUlaunchConfig *config, CUfunction f,
	void **kernelParams, void **extra)
{
	return launch_ex(real_cuLaunchKernelEx, config, f, kernelParams, extra,
		config ? config->hStream : NULL);
}

CUresult cuLaunchKernelEx_ptsz(const CUlaunchConfig *config, CUfunction f,
	void **kernelParams, void **extra)
{
	CUstream stream = config ? config->hStream : NULL;

	return launch_ex(real_cuLaunchKernelEx_ptsz, config, f, kernelParams,
		extra, stream ? stream : CU_STREAM_PER_THREAD);
}

static CUresult launch_cooperative(cuLaunchCooperativeKernel_func real,
	CUfunction f, unsigned int gridDimX, unsigned int gridDimY,
	unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY,
	unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream,
	void **kernelParams, CUstream query_stream)
{
	CUresult result;

	if (real == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	if (stream_is_capturing(query_stream))
		return real(f, gridDimX, gridDimY, gridDimZ, blockDimX,
			blockDimY, blockDimZ, sharedMemBytes, hStream,
			kernelParams);

	continue_with_lock();
	result = real(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY,
		blockDimZ, sharedMemBytes, hStream, kernelParams);
	cuda_driver_check_error(result, "cuLaunchCooperativeKernel");
	count_launch();
	return result;
}

CUresult cuLaunchCooperativeKernel(CUfunction f, unsigned int gridDimX,
	unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX,
	unsigned int blockDimY, unsigned int blockDimZ,
	unsigned int sharedMemBytes, CUstream hStream, void **kernelParams)
{
	return launch_cooperative(real_cuLaunchCooperativeKernel, f, gridDimX,
		gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
		sharedMemBytes, hStream, kernelParams, hStream);
}

CUresult cuLaunchCooperativeKernel_ptsz(CUfunction f, unsigned int gridDimX,
	unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX,
	unsigned int blockDimY, unsigned int blockDimZ,
	unsigned int sharedMemBytes, CUstream hStream, void **kernelParams)
{
	return launch_cooperative(real_cuLaunchCooperativeKernel_ptsz, f,
		gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
		sharedMemBytes, hStream, kernelParams,
		hStream ? hStream : CU_STREAM_PER_THREAD);
}

static CUresult launch_graph(cuGraphLaunch_func real, CUgraphExec hGraphExec,
	CUstream hStream, CUstream query_stream)
{
	CUresult result;

	if (real == NULL) return CUDA_ERROR_NOT_INITIALIZED;

	if (stream_is_capturing(query_stream))
		return real(hGraphExec, hStream);

	continue_with_lock();
	result = real(hGraphExec, hStream);
	cuda_driver_check_error(result, "cuGraphLaunch");
	count_launch();
	return result;
}

CUresult cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream)
{
	return launch_graph(real_cuGraphLaunch, hGraphExec, hStream, hStream);
}

CUresult cuGraphLaunch_ptsz(CUgraphExec hGraphExec, CUstream hStream)
{
	return launch_graph(real_cuGraphLaunch_ptsz, hGraphExec, hStream,
		hStream ? hStream : CU_STREAM_PER_THREAD);
}

static void capture_begun(CUresult result)
{
	if (result == CUDA_SUCCESS)
		__atomic_add_fetch(&active_captures, 1, __ATOMIC_RELEASE);
	true_or_exit(pthread_mutex_unlock(&capture_mutex) == 0);
}

CUresult cuStreamBeginCapture(CUstream hStream)
{
	CUresult result;

	if (real_cuStreamBeginCapture == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	result = real_cuStreamBeginCapture(hStream);
	capture_begun(result);
	return result;
}

CUresult cuStreamBeginCapture_ptsz(CUstream hStream)
{
	CUresult result;

	if (real_cuStreamBeginCapture_ptsz == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	result = real_cuStreamBeginCapture_ptsz(hStream);
	capture_begun(result);
	return result;
}

CUresult cuStreamBeginCapture_v2(CUstream hStream, CUstreamCaptureMode mode)
{
	CUresult result;

	if (real_cuStreamBeginCapture_v2 == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	result = real_cuStreamBeginCapture_v2(hStream, mode);
	capture_begun(result);
	return result;
}

CUresult cuStreamBeginCapture_v2_ptsz(CUstream hStream, CUstreamCaptureMode mode)
{
	CUresult result;

	if (real_cuStreamBeginCapture_v2_ptsz == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	result = real_cuStreamBeginCapture_v2_ptsz(hStream, mode);
	capture_begun(result);
	return result;
}

CUresult cuStreamBeginCaptureToGraph(CUstream hStream, CUgraph hGraph,
	const CUgraphNode *dependencies, const CUgraphEdgeData *dependencyData,
	size_t numDependencies, CUstreamCaptureMode mode)
{
	CUresult result;

	if (real_cuStreamBeginCaptureToGraph == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	result = real_cuStreamBeginCaptureToGraph(hStream, hGraph, dependencies,
		dependencyData, numDependencies, mode);
	capture_begun(result);
	return result;
}

CUresult cuStreamBeginCaptureToGraph_ptsz(CUstream hStream, CUgraph hGraph,
	const CUgraphNode *dependencies, const CUgraphEdgeData *dependencyData,
	size_t numDependencies, CUstreamCaptureMode mode)
{
	CUresult result;

	if (real_cuStreamBeginCaptureToGraph_ptsz == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	result = real_cuStreamBeginCaptureToGraph_ptsz(hStream, hGraph,
		dependencies, dependencyData, numDependencies, mode);
	capture_begun(result);
	return result;
}

static CUresult end_capture(cuStreamEndCapture_func real, CUstream hStream,
	CUgraph *phGraph, CUstream query_stream)
{
	CUstreamCaptureStatus before, after;
	CUresult result;
	int ended;

	if (real == NULL) return CUDA_ERROR_NOT_INITIALIZED;
	true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
	if (real_cuStreamIsCapturing == NULL) {
		result = real(hStream, phGraph);
		ended = (result == CUDA_SUCCESS ||
			 result == CUDA_ERROR_STREAM_CAPTURE_INVALIDATED);
	} else {
		if (real_cuStreamIsCapturing(query_stream, &before) != CUDA_SUCCESS)
			before = CU_STREAM_CAPTURE_STATUS_NONE;
		result = real(hStream, phGraph);
		/* E.g. ending from a joined stream fails and leaves it open */
		if (real_cuStreamIsCapturing(query_stream, &after) != CUDA_SUCCESS)
			after = CU_STREAM_CAPTURE_STATUS_NONE;
		ended = before != CU_STREAM_CAPTURE_STATUS_NONE &&
			after == CU_STREAM_CAPTURE_STATUS_NONE;
	}
	if (ended && active_captures > 0) {
		__atomic_sub_fetch(&active_captures, 1, __ATOMIC_RELEASE);
		true_or_exit(pthread_cond_broadcast(&capture_cv) == 0);
	}
	true_or_exit(pthread_mutex_unlock(&capture_mutex) == 0);
	return result;
}

CUresult cuStreamEndCapture(CUstream hStream, CUgraph *phGraph)
{
	return end_capture(real_cuStreamEndCapture, hStream, phGraph, hStream);
}

CUresult cuStreamEndCapture_ptsz(CUstream hStream, CUgraph *phGraph)
{
	return end_capture(real_cuStreamEndCapture_ptsz, hStream, phGraph,
		hStream ? hStream : CU_STREAM_PER_THREAD);
}

/* Our version of a launch, graph or capture function, by exact name. */
static void *get_variant_hook(const char *symbol)
{
	static const struct { const char *name; void *hook; } hooks[] = {
		{"cuLaunchKernelEx", (void *)cuLaunchKernelEx},
		{"cuLaunchKernelEx_ptsz", (void *)cuLaunchKernelEx_ptsz},
		{"cuLaunchCooperativeKernel", (void *)cuLaunchCooperativeKernel},
		{"cuLaunchCooperativeKernel_ptsz", (void *)cuLaunchCooperativeKernel_ptsz},
		{"cuGraphLaunch", (void *)cuGraphLaunch},
		{"cuGraphLaunch_ptsz", (void *)cuGraphLaunch_ptsz},
		{"cuStreamBeginCapture", (void *)cuStreamBeginCapture},
		{"cuStreamBeginCapture_ptsz", (void *)cuStreamBeginCapture_ptsz},
		{"cuStreamBeginCapture_v2", (void *)cuStreamBeginCapture_v2},
		{"cuStreamBeginCapture_v2_ptsz", (void *)cuStreamBeginCapture_v2_ptsz},
		{"cuStreamBeginCaptureToGraph", (void *)cuStreamBeginCaptureToGraph},
		{"cuStreamBeginCaptureToGraph_ptsz", (void *)cuStreamBeginCaptureToGraph_ptsz},
		{"cuStreamEndCapture", (void *)cuStreamEndCapture},
		{"cuStreamEndCapture_ptsz", (void *)cuStreamEndCapture_ptsz},
	};

	for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
		if (strcmp(symbol, hooks[i].name) == 0)
			return hooks[i].hook;
	return NULL;
}

/*
 * cuGetProcAddress() picks the variant (_v2, _ptsz) from the version and
 * flags. Map the real function it returned to our wrapper of that variant.
 */
static void *hook_for_real(const char *symbol, void *fn)
{
	const struct { void *real; void *hook; } hooks[] = {
		{(void *)real_cuLaunchKernelEx, (void *)cuLaunchKernelEx},
		{(void *)real_cuLaunchKernelEx_ptsz, (void *)cuLaunchKernelEx_ptsz},
		{(void *)real_cuLaunchCooperativeKernel, (void *)cuLaunchCooperativeKernel},
		{(void *)real_cuLaunchCooperativeKernel_ptsz, (void *)cuLaunchCooperativeKernel_ptsz},
		{(void *)real_cuGraphLaunch, (void *)cuGraphLaunch},
		{(void *)real_cuGraphLaunch_ptsz, (void *)cuGraphLaunch_ptsz},
		{(void *)real_cuStreamBeginCapture, (void *)cuStreamBeginCapture},
		{(void *)real_cuStreamBeginCapture_ptsz, (void *)cuStreamBeginCapture_ptsz},
		{(void *)real_cuStreamBeginCapture_v2, (void *)cuStreamBeginCapture_v2},
		{(void *)real_cuStreamBeginCapture_v2_ptsz, (void *)cuStreamBeginCapture_v2_ptsz},
		{(void *)real_cuStreamBeginCaptureToGraph, (void *)cuStreamBeginCaptureToGraph},
		{(void *)real_cuStreamBeginCaptureToGraph_ptsz, (void *)cuStreamBeginCaptureToGraph_ptsz},
		{(void *)real_cuStreamEndCapture, (void *)cuStreamEndCapture},
		{(void *)real_cuStreamEndCapture_ptsz, (void *)cuStreamEndCapture_ptsz},
	};

	for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
		if (fn != NULL && fn == hooks[i].real)
			return hooks[i].hook;
	if (get_variant_hook(symbol) != NULL)
		log_warn("Can't hook %s, its work may break or bypass nvshare",
			 symbol);
	return fn;
}


__asm__(".symver dlsym_225, dlsym@@GLIBC_2.2.5");
__asm__(".symver dlsym_234, dlsym@GLIBC_2.34");

