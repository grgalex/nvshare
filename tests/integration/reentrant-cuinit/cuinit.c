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
 * cuInit() that dlsym()s and calls "cuInit", like the driver in issue #27.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

typedef int (*cuInit_fn)(unsigned int);

static pthread_once_t driver_once = PTHREAD_ONCE_INIT;

static void driver_init(void)
{
	Dl_info self, found;
	void *handle;
	cuInit_fn fn;

	handle = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_NOLOAD);
	fn = (cuInit_fn)dlsym(handle, "cuInit");

	if (fn != NULL && dladdr((void *)fn, &found) != 0 &&
	    dladdr((void *)driver_init, &self) != 0 &&
	    found.dli_fbase != self.dli_fbase) {
		fprintf(stderr, "[fake-libcuda] dlsym(\"cuInit\") returned a"
			" function from %s, calling it\n", found.dli_fname);
		fn(0);
	}
}

int cuInit(unsigned int flags)
{
	(void)flags;
	pthread_once(&driver_once, driver_init);
	return 0;
}
