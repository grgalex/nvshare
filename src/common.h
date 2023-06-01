/*
 * Copyright (c) 2023 Georgios Alexopoulos
 */
#ifndef _COMMON_H_
#define _COMMON_H_

#include <time.h>
#include <string.h>

extern int __debug;
extern int pending_kernel_window;
extern ssize_t write_whole(int fd, const void *buf, size_t count);
extern ssize_t read_whole(int fd, void *buf, size_t count);
extern size_t strlcpy(char *dst, const char *src, size_t siz);


#define log_fatal_errno(fmt, ...)                             \
do {                                                          \
	fprintf(stderr, "[NVSHARE][FATAL]: " fmt "\n", ##__VA_ARGS__); \
	fprintf(stderr, "errno = %s\n", strerror(errno));     \
	exit(1);                                              \
} while (0)

#define log_fatal(fmt, ...)                                   \
do {                                                          \
	fprintf(stderr, "[NVSHARE][FATAL]: " fmt "\n", ##__VA_ARGS__); \
	exit(1);                                              \
} while (0)

#define log_info(fmt, ...)                      \
do {                                              \
	fprintf(stderr, "[NVSHARE][INFO]: " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define log_warn(fmt, ...)                                   \
do {                                                         \
	fprintf(stderr, "[NVSHARE][WARN]: " fmt "\n", ##__VA_ARGS__); \
} while (0)

/* Source: https://stackoverflow.com/a/1644898 */
#define log_debug(fmt, ...)                                                \
do {                                                                       \
	if (__debug) fprintf(stderr, "[NVSHARE][DEBUG]: " fmt "\n", ##__VA_ARGS__); \
} while (0)


#define true_or_exit(condition)                                 \
	do {                                                    \
		if (!(condition))                               \
			log_fatal("Condition failed: %s", \
				  #condition);                  \
	} while (0)


#define MiB * (1 << 20)
#define toMiB(x) (((double)x) / (1 MiB))


/* From NetBSD */
#define	timespecsub(tsp, usp, vsp)                                \
	do {                                                      \
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;    \
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec; \
		if ((vsp)->tv_nsec < 0) {                         \
			(vsp)->tv_sec--;                          \
			(vsp)->tv_nsec += 1000000000L;            \
		}                                                 \
	} while (0)


#define RETRY_INTR(exp)                   \
({                                        \
    int __rc;                             \
    do {                                  \
        __rc = (exp);                     \
    } while (__rc < 0 && errno == EINTR); \
    __rc;                                 \
})


/* Source: https://stackoverflow.com/a/3437484 */
#define max(a, b) ({ typeof (a) _a = (a); typeof (b) _b = (b); _a > _b ? _a : _b; })
#define min(a, b) ({ typeof (a) _a = (a); typeof (b) _b = (b); _a < _b ? _a : _b; })


#define HEX_STR_LEN(x) (2 * sizeof(x) + 1)
#define EPOLL_MAX_EVENTS 32
#define NVSHARE_UNREGISTERED_ID 0xF00DF00DF00DF00D

#define ENV_NVSHARE_DEBUG         "NVSHARE_DEBUG"

#endif /* _COMMON_H_ */

