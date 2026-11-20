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
 * The nvshare client. It implements the nvshare-related logic/threads
 * that we inject into a CUDA application and which manages the interactions
 * with the nvshare scheduler on behalf of the application.
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>

#include "comm.h"
#include "common.h"
#include "client.h"
#include "version.h"
#include "cuda_defs.h"

void *client_fn(void *arg __attribute__((unused)));
void *release_early_fn(void *arg __attribute__((unused)));

pthread_t client_tid;
pthread_t release_early_thread_tid;
pthread_mutex_t global_mutex;
pthread_cond_t own_lock_cv;
pthread_cond_t release_early_cv;
sem_t got_initial_sched_status;
struct message req_lock_msg = {0};
CUcontext cuda_ctx;
int rsock;
int scheduler_on;
int release_early_check_interval = 5;
int own_lock;
int need_lock;
int did_work;
uint64_t nvshare_client_id;
char nvscheduler_socket_path[NVSHARE_SOCK_PATH_MAX];


static void cuda_sync_context(void) {
	CUresult cu_err = CUDA_SUCCESS;

	pending_kernel_window = 1;
	cu_err = real_cuCtxSetCurrent(cuda_ctx);
	cuda_driver_check_error(cu_err, CUDA_SYMBOL_STRING(cuCtxSetCurrent));
	cu_err = real_cuCtxSynchronize();
	cuda_driver_check_error(cu_err, CUDA_SYMBOL_STRING(cuCtxSynchronize));
}


/*
 * Only returns if the client has the GPU lock or if the scheduler is off.
 */
void continue_with_lock(void)
{
	CUresult cu_err = CUDA_SUCCESS;
	static int cuda_ctx_ok = 0;

	true_or_exit(pthread_mutex_lock(&global_mutex) == 0);
	if (cuda_ctx_ok == 0) {
		cu_err = real_cuCtxGetCurrent(&cuda_ctx);
		cuda_driver_check_error(cu_err,
				        CUDA_SYMBOL_STRING(cuCtxGetCurrent));
		if (cu_err != CUDA_SUCCESS) {
			log_fatal("Can't get app's CUDA context!");
		}
		cuda_ctx_ok = 1;
	}
	while (own_lock == 0) {
		/*
		 * The application may comprise multiple threads. We must
		 * request the lock only once on behalf of the whole app.
		 */
		if (need_lock == 0) {
			need_lock = 1;
			true_or_exit(write_whole(rsock, &req_lock_msg, sizeof(req_lock_msg)) == sizeof(req_lock_msg));
		}

		true_or_exit(pthread_cond_wait(&own_lock_cv, &global_mutex) == 0);
	}

	/* We did something. Reset the early release timer. */
	did_work = 1;
	true_or_exit(pthread_cond_broadcast(&release_early_cv) == 0);

	true_or_exit(pthread_mutex_unlock(&global_mutex) == 0);
}


/* We use the HOSTNAME environment variable to read the Kubernetes pod name,
 * when we are running on Kubernetes.
 *
 * Only called if we've detected that we're actually running on Kubernetes.
 */
static void read_pod_name(char *pod_name, size_t size)
{
	char *env_hostname = "HOSTNAME";
	char *value;


	value = getenv(env_hostname);
	if (value != NULL) {
		if (strlcpy(pod_name, value, size) >= size)
			log_warn("Pod name is longer than %zu"
				 " characters. Truncating it.", size);
	} else {
		log_debug("Environment variable %s is not set, defaulting to"
			  " \"none\"", env_hostname);
		strlcpy(pod_name, "none", size);
	}
}


/*
 * The namespace of a Pod is accessible by default on Kubernetes through a
 * certain mounted volume:
 *
 * https://stackoverflow.com/a/42449618
 *
 * It's our best shot, so why not take it?
 */
static void read_pod_namespace(char *pod_namespace, size_t size)
{
	char *k8s_pod_ns_file = "/var/run/secrets/kubernetes.io/serviceaccount/namespace";
	FILE *fp;


	fp = fopen(k8s_pod_ns_file, "r");
	if (!fp) {
		log_warn("Couldn't open file %s to read Pod namespace",
			 k8s_pod_ns_file);
		goto out_ns_none;
	} else {
		if (!fgets(pod_namespace, size, fp)) {
			log_warn("Couldn't read the Pod namespace from %s",
				 k8s_pod_ns_file);
			true_or_exit(fclose(fp) == 0);
			goto out_ns_none;
		}
	}

	true_or_exit(fclose(fp) == 0);
	return;

out_ns_none:
	strlcpy(pod_namespace, "none", size);
}


/*
 * Spawn all nvshare-related threads, bootstrap the client.
 *
 * In more detail:
 * 1. Initialize all locking primitives
 * 2. Create the client thread.
 * 3. Create the early releaser thread.
 * 4. Fill in the globally visible req_lock_msg, that the
 *    app threads will send to the nvshare-scheduler to request
 *    the GPU lock.
 */
void initialize_client(void)
{
	scheduler_on = 0;
	cuda_ctx = NULL;
	own_lock = 0;
	need_lock = 0;

	true_or_exit(pthread_cond_init(&own_lock_cv, NULL) == 0);
	true_or_exit(pthread_cond_init(&release_early_cv, NULL) == 0);
	true_or_exit(pthread_mutex_init(&global_mutex, NULL) == 0);
	true_or_exit(sem_init(&got_initial_sched_status, 0, 0) == 0);

	/* Client thread. */
	true_or_exit(pthread_create(&client_tid, NULL, client_fn, NULL) == 0);

	/* Ensure the client thread has received the initial scheduler status */
	true_or_exit(RETRY_INTR(sem_wait(&got_initial_sched_status)) == 0);

	true_or_exit(pthread_create(&release_early_thread_tid, NULL,
		     release_early_fn, NULL) == 0);

	memset(&req_lock_msg, 0, sizeof(req_lock_msg));
	req_lock_msg.type = REQ_LOCK;
	req_lock_msg.id = nvshare_client_id;
}


/* The nvshare client main thread.
 *
 * Does the following:
 * 1. Registers client to the nvshare-scheduler
 * 2. Listens for messages from the nvshare-scheduler on a persistent connection
 */
void *client_fn(void *arg __attribute__((unused)))
{
	struct message in_msg;
	struct message out_msg;

	memset(&out_msg, 0, sizeof(out_msg));
	out_msg.id = 1234;

	/*
	 * Block every signal for this thread. We want the main thread of the
	 * application to catch all signals.
	 */
	sigset_t signal_set;
	true_or_exit(sigfillset(&signal_set) == 0);
	true_or_exit(pthread_sigmask(SIG_SETMASK, &signal_set, NULL) == 0);

	if (getenv("KUBERNETES_SERVICE_HOST")) {
		read_pod_namespace(out_msg.pod_namespace, sizeof(out_msg.pod_namespace));
		read_pod_name(out_msg.pod_name, sizeof(out_msg.pod_name));
	} else {
		strlcpy(out_msg.pod_namespace, "none", sizeof(out_msg.pod_namespace));
		strlcpy(out_msg.pod_name, "none", sizeof(out_msg.pod_name));
	}

	log_debug("NVSHARE_POD_NAME = %s", out_msg.pod_name);
	log_debug("NVSHARE_POD_NAMESPACE = %s", out_msg.pod_namespace);

	true_or_exit(nvshare_get_scheduler_path(nvscheduler_socket_path) == 0);

	out_msg.type = REGISTER;

	true_or_exit(nvshare_connect(&rsock, nvscheduler_socket_path) == 0);
	true_or_exit(write_whole(rsock, &out_msg, sizeof(out_msg)) == sizeof(out_msg));
	log_debug("Sent %s", message_type_string[out_msg.type]);

	/*
	 * Obtain the inital nvshare-scheduler status
	 */
	true_or_exit(nvshare_receive_block(rsock, &in_msg, sizeof(in_msg)) == sizeof(in_msg));
	switch (in_msg.type) {
	case SCHED_ON:
		log_debug("Received %s", message_type_string[in_msg.type]);

		true_or_exit(sscanf(in_msg.data, "%" SCNx64, &nvshare_client_id) == 1);
		log_info("Successfully initialized nvshare GPU (version %s)", NVSHARE_VERSION);
		log_info("Client ID = %016" PRIx64, nvshare_client_id);
		scheduler_on = 1;
		own_lock = 0;
		need_lock = 0;
		break;

	case SCHED_OFF:
		log_debug("Received %s", message_type_string[in_msg.type]);

		true_or_exit(sscanf(in_msg.data, "%" SCNx64, &nvshare_client_id) == 1);
		log_info("Successfully initialized nvshare GPU (version %s)", NVSHARE_VERSION);
		log_info("Client ID = %016" PRIx64, nvshare_client_id);
		scheduler_on = 0;
		own_lock = 1;
		need_lock = 0;

		break;
	default:
		log_fatal("Got message with type (%d) instead of initial"
			  " nvshare-scheduler status", (int)in_msg.type);
		break;
	}

	/* The ID will not change henceforth. Fill it in now. */
	memset(&out_msg, 0, sizeof(out_msg));
	out_msg.id = nvshare_client_id;

	true_or_exit(sem_post(&got_initial_sched_status) == 0);

	while (1) {
		true_or_exit(nvshare_receive_block(rsock, &in_msg, sizeof(in_msg)) == sizeof(in_msg));
		true_or_exit(pthread_mutex_lock(&global_mutex) == 0);

		switch (in_msg.type) {
		case LOCK_OK:
			log_debug("Received %s", message_type_string[in_msg.type]);

			need_lock = 0;
			own_lock = 1;
			did_work = 1; /* Restart the early release timer to avoid race */
			true_or_exit(pthread_cond_broadcast(&own_lock_cv) == 0);
			true_or_exit(pthread_cond_broadcast(&release_early_cv) == 0);

			break;
		case DROP_LOCK:
			log_debug("Received %s", message_type_string[in_msg.type]);

			if (own_lock == 1) { /* Sanity check */
				/*
				 * We can't sync while a graph capture is open.
				 * Wait for it to end, and keep new ones from
				 * starting until we've released the lock.
				 */
				true_or_exit(pthread_mutex_unlock(&global_mutex) == 0);
				true_or_exit(pthread_mutex_lock(&capture_mutex) == 0);
				while (active_captures > 0 && own_lock == 1)
					true_or_exit(pthread_cond_wait(&capture_cv, &capture_mutex) == 0);
				true_or_exit(pthread_mutex_lock(&global_mutex) == 0);
				if (own_lock == 1) {
					own_lock = 0; /* Block work submission */
					cuda_sync_context(); /* Ensure all submitted work done */
					out_msg.type = LOCK_RELEASED;
					true_or_exit(write_whole(rsock, &out_msg, sizeof(out_msg)) == sizeof(out_msg));
					log_debug("Sent %s", message_type_string[out_msg.type]);
				}
				true_or_exit(pthread_mutex_unlock(&capture_mutex) == 0);
			}

			break;
		case SCHED_ON:
			log_debug("Received %s", message_type_string[in_msg.type]);

			if (!scheduler_on) { /* WAS OFF, NOW ON */
				log_debug("Scheduler status changed to ON");
				scheduler_on = 1;
				need_lock = 0;
				own_lock = 0;
			} else log_debug("Scheduler status did not change, doing nothing");

			break;
		case SCHED_OFF:
			log_debug("Received %s", message_type_string[in_msg.type]);

			if (scheduler_on) { /* WAS ON, NOW OFF */
				log_debug("Scheduler status changed to OFF");
				scheduler_on = 0;
				own_lock = 1;
				need_lock = 0;
				true_or_exit(pthread_cond_broadcast(&own_lock_cv) == 0);
			}
			break;

		default:
			log_warn("Unknown message type (%d)",
				 (int)in_msg.type);
			break;
		}

		/* Done with this messsage */
		true_or_exit(pthread_mutex_unlock(&global_mutex) == 0);

	}
}


/*
 * Return 1 if the GPU looks idle, after the client has submitted no work for
 * a whole interval. It may still be busy with work submitted earlier.
 */
static int gpu_is_idle(nvmlDevice_t *nvml_dev)
{
	struct timespec cuda_sync_start_time = {0, 0};
	struct timespec cuda_sync_complete_time = {0, 0};
	struct timespec cuda_sync_duration = {0, 0};
	unsigned int elapsed_ms;
	nvmlReturn_t nvml_ret;
	nvmlUtilization_t nvml_util;

	if (nvml_ok) {
		nvml_ret = real_nvmlDeviceGetUtilizationRates(*nvml_dev, &nvml_util);
		if (nvml_ret != NVML_SUCCESS) {
			/*
			 * Don't consider NVML failing a fatal thing. We found
			 * its symbols though, so this indicates a deeper error.
			 */
			log_warn("nvmlDeviceGetUtilizationRates failed with %d",
				 (int)nvml_ret);
			nvml_ok = 0; /* Stop using NVML */
			return 0;
		}
		log_debug("GPU Utilization = %u %%", nvml_util.gpu);
		if (nvml_util.gpu > 0) {
			log_debug("Early release timer elapsed but we are not idle");
			return 0;
		}
		return 1;
	}

	/*
	 * Fallback without NVML: consider a cuCtxSynchronize() that takes more
	 * than 100 ms (empirical threshold) as the GPU having been busy.
	 */
	true_or_exit(clock_gettime(CLOCK_MONOTONIC, &cuda_sync_start_time) == 0);
	cuda_sync_context();
	true_or_exit(clock_gettime(CLOCK_MONOTONIC, &cuda_sync_complete_time) == 0);
	timespecsub(&cuda_sync_complete_time, &cuda_sync_start_time, &cuda_sync_duration);
	elapsed_ms = ((unsigned int)cuda_sync_duration.tv_sec * 1000)
		     + (cuda_sync_duration.tv_nsec / 1000000);
	if (elapsed_ms >= 100) {
		log_debug("Early release timer elapsed but we are not idle");
		return 0;
	}
	return 1;
}

void *release_early_fn(void *arg __attribute__((unused)))
{
	struct message release_msg = {0};
	struct timespec timer_end_ts = {0, 0};
	int ret;
	nvmlReturn_t nvml_ret;
	nvmlDevice_t nvml_dev;

	release_msg.type = LOCK_RELEASED;
	release_msg.id = nvshare_client_id;

	/*
	 * Block every signal for this thread. We want the main thread of the
	 * application to catch all signals.
	 */
	sigset_t signal_set;
	true_or_exit(sigfillset(&signal_set) == 0);
	true_or_exit(pthread_sigmask(SIG_SETMASK, &signal_set, NULL) == 0);

	if (nvml_ok) {
		nvml_ret = real_nvmlInit();
		if (nvml_ret != NVML_SUCCESS) {
			log_warn("nvmlInit failed with %d", (int)nvml_ret);
			goto check_nvml_ret;
		}
		nvml_ret = real_nvmlDeviceGetHandleByIndex(0, &nvml_dev);
		if (nvml_ret != NVML_SUCCESS) {
			log_warn("nvmlDeviceGetHandleByIndex returned %d",
				 (int)nvml_ret);
		}
check_nvml_ret:
		if (nvml_ret != NVML_SUCCESS) nvml_ok = 0;
	}
	true_or_exit(pthread_mutex_lock(&global_mutex) == 0);

	while (1) {
		did_work = 0;
		true_or_exit(clock_gettime(CLOCK_REALTIME, &timer_end_ts) == 0);
		timer_end_ts.tv_sec += release_early_check_interval;
wait_remainder:
		ret = pthread_cond_timedwait(&release_early_cv, &global_mutex,
			                     &timer_end_ts);
		/* We've locked global_mutex */
		if (ret == ETIMEDOUT) {
			if (!scheduler_on || !own_lock) continue;
			if (did_work) {
				did_work = 0;
				continue;
			}
			/*
			 * Keep capture_mutex until we've released the lock, so
			 * that no capture starts in between. Only trylock it: we
			 * hold global_mutex.
			 */
			if (pthread_mutex_trylock(&capture_mutex) != 0)
				continue;
			/* Launches into a capture don't count as work: never idle then */
			if (active_captures > 0 || !gpu_is_idle(&nvml_dev)) {
				true_or_exit(pthread_mutex_unlock(&capture_mutex) == 0);
				continue;
			}
			log_debug("Releasing the lock early due to inactivity");
			true_or_exit(write_whole(rsock, &release_msg, sizeof(release_msg)) == sizeof(release_msg));
			own_lock = 0;
			/* A DROP_LOCK waiting for a capture to end has nothing left to do */
			true_or_exit(pthread_cond_broadcast(&capture_cv) == 0);
			true_or_exit(pthread_mutex_unlock(&capture_mutex) == 0);
			log_debug("Sent %s", message_type_string[release_msg.type]);
		} else if (ret != 0) { /* BAD */
			errno = ret;
			log_fatal_errno("pthread_cond_timedwait() failed");
		} else { /* Condition variable was signaled */
			if (did_work) continue;
			else goto wait_remainder; /* Spurious wakeup */
		}
	}
}

