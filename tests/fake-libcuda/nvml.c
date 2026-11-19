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
 * Fake libnvidia-ml.so.1: one GPU that always reports 0% utilization.
 */

typedef struct {
	unsigned int gpu;
	unsigned int memory;
} nvmlUtilization_t;

int nvmlInit_v2(void) { return 0; }

int nvmlDeviceGetHandleByIndex_v2(unsigned int index, void **device)
{
	(void)index;
	*device = (void *)0x1;
	return 0;
}

int nvmlDeviceGetUtilizationRates(void *device, nvmlUtilization_t *util)
{
	(void)device;
	util->gpu = 0;
	util->memory = 0;
	return 0;
}
