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
 * Communication primitives header file.
 */

#ifndef _NVSHARE_COMM_H_
#define _NVSHARE_COMM_H_

#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>

/* https://lists.debian.org/debian-glibc/2004/02/msg00232.html */
#include <sys/un.h>
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un *)0)->sun_path)
#endif

/* Maximum length of nvshare socket path */
#define NVSHARE_SOCK_PATH_MAX UNIX_PATH_MAX

/* 
 * A message's data segment must comfortably hold 16 HEX characters plus a
 * NULL terminator for the client ID which the scheduler sends as a response
 * to a REGISTER message.
 */
#define MSG_DATA_LEN          20
#define POD_NAME_LEN_MAX      254
#define POD_NAMESPACE_LEN_MAX 254

#define NVSHARE_SOCK_DIR          "/var/run/nvshare/"


extern const char *message_type_string[];
extern uint64_t nvshare_generate_id(void);
extern int nvshare_get_scheduler_path(char *sock_path);
extern int nvshare_bind_and_listen(int *lsock, const char *sock_path);
extern int nvshare_connect(int *rsock, const char *rpath);
extern int nvshare_accept(int lsock, int *rsock);
extern ssize_t nvshare_send_noblock(int rsock, const void *msg_p, size_t count);
extern ssize_t nvshare_receive_noblock(int rsock, void *msg_p, size_t count);
extern int nvshare_receive_block(int rsock, void *msg_p, size_t count);


enum message_type {
	REGISTER       = 1,
	SCHED_ON       = 2,
	SCHED_OFF      = 3,
	REQ_LOCK       = 4,
	LOCK_OK        = 5,
	DROP_LOCK      = 6,
	LOCK_RELEASED  = 7,
	SET_TQ         = 8,
} __attribute__((__packed__));

struct message {
	enum message_type type;
	/*
	 * Client id. Used only for debugging purposes (i.e., easily identify
	 * scheduler logs for a specific client).
	 */
	char pod_name[POD_NAME_LEN_MAX];
	char pod_namespace[POD_NAMESPACE_LEN_MAX];
	uint64_t id;
	char data[MSG_DATA_LEN];
} __attribute__((__packed__));


#endif /* _NVSHARE_COMM_H_ */

