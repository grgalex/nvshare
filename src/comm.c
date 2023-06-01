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
 * Communication primitives for nvshare.
 */

/*
 * Define _GNU_SOURCE before including any header file, as it affects it.
 * In this case, we need it for accept4().
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "comm.h"
#include "common.h"


/* Message types as strings, to be used when printing */
const char *message_type_string[] = {
	[REQ_LOCK] = "REQ_LOCK",
	[LOCK_RELEASED] = "LOCK_RELEASED",
	[SCHED_ON] = "SCHED_ON",
	[SCHED_OFF] = "SCHED_OFF",
	[LOCK_OK] = "LOCK_OK",
	[DROP_LOCK] = "DROP_LOCK",
	[SET_TQ] = "SET_TQ",
	[REGISTER] = "REGISTER",
};


/*
 * Unsigned 64 bit integer generator
 * Source: https://stackoverflow.com/a/33021408
 */
#define IMAX_BITS(m) ((m)/((m)%255+1) / 255%255*8 + 7-86/((m)%255+12))
#define RAND_MAX_WIDTH IMAX_BITS(RAND_MAX)
_Static_assert((RAND_MAX & (RAND_MAX + 1u)) == 0, "RAND_MAX not a Mersenne number");

uint64_t nvshare_generate_id(void) {
	uint64_t r = 0;
	for (int i = 0; i < 64; i += RAND_MAX_WIDTH) {
		r <<= RAND_MAX_WIDTH;
		r ^= (unsigned) rand();
	}
	return r;
}


/* Stores the path to the nvshare-scheduler socket in sock_path. */
int nvshare_get_scheduler_path(char *sock_path)
{
	int offset;
	size_t ret;
	
	/* TODO: Ensure it fits in sock_path, check return value */
	ret = strlcpy(sock_path, NVSHARE_SOCK_DIR, NVSHARE_SOCK_PATH_MAX);

	offset = ret; /* Start from the trailing NULL byte */

	/* TODO: Ensure it fits in sock_path, check return value */
	ret = snprintf(sock_path + offset, NVSHARE_SOCK_PATH_MAX - offset,
			"%s", "scheduler.sock");
	return 0;
}


static int nvshare_unix_bind(int *sock, const char *path, int socket_type)
{
	int ret = 0;
	struct sockaddr_un addr;

	if ((*sock = socket(AF_UNIX, socket_type, 0)) < 0) {
		ret = -errno;
		log_info("Failed to create UNIX socket");
		goto out;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	ret = strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

	ret = unlink(path);
	if (ret < 0 && errno != ENOENT) {
		log_info("Error deleting existing socket `%s'", path);
		ret = -errno;
		goto out_with_sock;
	}

	ret = bind(*sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		ret = -errno;
		log_info("Failed to bind UNIX socket to %s",
			path);
		goto out_with_sock;
	}

	return ret;

out_with_sock:
	close(*sock);
out:
	errno = -ret;
	return -1;
}


static int nvshare_unix_connect(int *sock, const char *path, int socket_type)
{
	int ret = 0;
	struct sockaddr_un addr;

	if ((*sock = socket(AF_UNIX, socket_type, 0)) < 0) {
		ret = -errno;
		log_info("Failed to create UNIX socket");
		goto out;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	ret = strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

	ret = connect(*sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret != 0) {
		ret = -errno;
		log_info("Failed to connect to UNIX socket at %s\n", path);
		goto out_with_sock;
	}

	return ret;

out_with_sock:
	close(*sock);
out:
	errno = -ret;
	return -1;
}


int nvshare_bind_and_listen(int *lsock, const char *sock_path)
{
	int bklog = 32;
	int ret = 0;

	ret = nvshare_unix_bind(lsock, sock_path, SOCK_STREAM | SOCK_NONBLOCK);
	if (ret < 0) goto out;

	ret = listen(*lsock, bklog);
	if (ret < 0) {
		ret = -errno;
		close(*lsock);
		goto out;
	}
	/* Should be 0 */
	return ret;
out:
	errno = -ret;
	return -1;
}


int nvshare_connect(int *rsock, const char *rpath)
{
	return RETRY_INTR(nvshare_unix_connect(rsock, rpath, SOCK_STREAM));
}


int nvshare_accept(int lsock, int *rsock)
{
	int sock;
	sock = RETRY_INTR(accept4(lsock, NULL, NULL, SOCK_NONBLOCK));
	if (sock < 0) {
		if (errno == ECONNABORTED)
			log_debug("accept() connection aborted prematurely");
		return -1;
	}

	*rsock = sock;
	return 0;
}


/* Send a message on a non-blocking socket. */
ssize_t nvshare_send_noblock(int rsock, const void *msg_p, size_t count)
{
	return RETRY_INTR(write(rsock, msg_p, count));
}


/* Receive a message from a non-blocking socket. */
ssize_t nvshare_receive_noblock(int rsock, void *msg_p, size_t count)
{
	/* Clear the message buffer */
	memset(msg_p, 0, count);
	return RETRY_INTR(read(rsock, msg_p, count));
}


/* Receive a message from a blocking socket. */
int nvshare_receive_block(int rsock, void *msg_p, size_t count)
{
	/* Clear the message buffer */
	memset(msg_p, 0, count);
	return read_whole(rsock, msg_p, count);
}

