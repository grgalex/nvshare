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
 * A command-line utility to configure the nvshare scheduler (nvshare-scheduler).
 */

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>

#include "xopt.h"
#include "comm.h"
#include "common.h"

static char nvscheduler_socket_path[NVSHARE_SOCK_PATH_MAX];


typedef struct {
	int cmdline_scheduler_tq;
	const char *cmdline_anti_thrash;
	bool help;
} SimpleConfig;


xoptOption options[] = {
	{
		"set-tq",
		'T',
		offsetof(SimpleConfig, cmdline_scheduler_tq),
		0,
		XOPT_TYPE_INT,
		"n",
	        "Set the time quantum of the scheduler to TQ seconds. Only"
		" accepts positive integers."
	},
	{
		"anti-thrash",
		'S',
		offsetof(SimpleConfig, cmdline_anti_thrash),
		0,
		XOPT_TYPE_STRING,
		"s",
		"Set the desired status of the scheduler. Only accepts values"
		" \"on\" or \"off\"."
	},
	{
		"help",
		'h',
		offsetof(SimpleConfig, help),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Shows this help message"
	},
	XOPT_NULLOPTION
};


static int change_tq(int newtq)
{
	int rsock;
	int ret;
	struct message msg = {0};

	msg.id = 0xBEEF;
	msg.type = SET_TQ;
	if (snprintf(msg.data, MSG_DATA_LEN, "%lld", (long long) newtq) <= 0)
		log_fatal("snprintf() failed");

	ret = 0;
	if (nvshare_connect(&rsock, nvscheduler_socket_path) != 0)
		log_fatal("nvshare_connect() failed");
	if (write_whole(rsock, &msg, sizeof(msg)) != sizeof(msg))
		ret = -1;
	true_or_exit(close(rsock) == 0);

	return ret;
}


static int change_status(int status)
{
	int rsock;
	int ret;
	struct message msg = {0};

	if (status == 1) msg.type = SCHED_ON;
	else msg.type = SCHED_OFF;
	//msg.type = (status == 1) ? SCHED_ON : SCHED_OFF;
	msg.id = 0xBEEF;

	ret = 0;
	true_or_exit(nvshare_connect(&rsock, nvscheduler_socket_path) == 0);
	if (write_whole(rsock, &msg, sizeof(msg)) != sizeof(msg))
		ret = -1;
	true_or_exit(close(rsock) == 0);

	return ret;
}


int main(int argc, const char *argv[])
{
	int status;
	int actions_done = 0;
	const char *opt_err = NULL;
	SimpleConfig config;
	xoptContext *ctx;
	const char **extras = NULL;

	config.cmdline_scheduler_tq = 0;
	config.cmdline_anti_thrash = NULL;

	ctx = xopt_context("nvsharectl", options,
			XOPT_CTX_POSIXMEHARDER | XOPT_CTX_STRICT, &opt_err);

	if (opt_err) {
		log_fatal("Error: %s", opt_err);
	}

	xopt_parse(ctx, argc, argv, &config, &extras, &opt_err);
	if (opt_err) {
		log_fatal("Error: %s", opt_err);
	}

	if (nvshare_get_scheduler_path(nvscheduler_socket_path) != 0)
		log_fatal("Failed to obtain nvshare-scheduler socket path.");

	if (config.cmdline_anti_thrash != NULL) {
		if (strcmp(config.cmdline_anti_thrash, "on") == 0)
			status = 1;
		else if (strcmp(config.cmdline_anti_thrash, "off") == 0)
			status = 0;
		else log_fatal("Invalid option for --anti-thrash (-S). Must"
			       " be one of 'on' or 'off'.");

		if (change_status(status) != 0)
			log_info("Failed to turn the nvshare-scheduler %s.",
				   config.cmdline_anti_thrash);
		else log_info("Successfully turned the nvshare-scheduler %s.",
				config.cmdline_anti_thrash);
		actions_done++;
	}

	if (config.cmdline_scheduler_tq != 0) {
		int parsed_scheduler_tq = config.cmdline_scheduler_tq;
		if (parsed_scheduler_tq <= 0)
			log_fatal("Invalid option for --set-tq. TQ value"
				  " must be a positive integer.");
		if (change_tq(parsed_scheduler_tq) != 0)
			log_info("Failed to set nvshare-scheduler TQ to %d"
			           " seconds.", parsed_scheduler_tq);
		else log_info("Successfully set the nvshare-scheduler TQ to %d"
			        " seconds.", parsed_scheduler_tq);

		actions_done++;
	}

	/* help? */
	if (config.help || (actions_done == 0)) {
		xoptAutohelpOptions opts;
		opts.usage = "[options]";
		opts.prefix = "A command line utility to configure the nvshare scheduler.";
		opts.spacer = 10;

		xopt_autohelp(ctx, stderr, &opts, &opt_err);
		exit(0);
	}

	return 0;
}

