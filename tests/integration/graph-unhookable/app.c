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
 * Test app: looks up cuGraphLaunch through cuGetProcAddress_v2().
 */

#include <stdio.h>

int cuInit(unsigned int flags);
int cuGetProcAddress_v2(const char *symbol, void **pfn, int version,
			unsigned long long flags, int *status);

int main(void)
{
	void *fn = NULL;

	if (cuInit(0) != 0)
		return 1;
	return cuGetProcAddress_v2("cuGraphLaunch", &fn, 12080, 0, NULL);
}
