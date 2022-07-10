/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tyler J. Anderson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file air-quality-db.c
 * @author Tyler J. Anderson
 *
 * Filing program to store json strings in a postgres database
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define ARRAY_LEN(array) sizeof(array)/sizeof(array[0])

#define APP_RUN(fun) do {			\
		int ret;			\
		ret = fun;			\
		if (ret < 0)			\
			return ret;		\
	} while (0)

struct app {
	int fd;
	int argc;
	const char **argv;
	char addr[48];
	char port[10];
	char data[10240];
	char name[32];
} local_app;

int app_tcp_connect(struct app *a);
int app_parse_args(struct app *a);
int app_close_file(struct app *a);
int app_print_data(struct app *a);
void signal_handler(int signal);

int app_tcp_connect(struct app *a)
{
	int s;
	char errstr[128];
	struct addrinfo* sockai;
	struct addrinfo* ai_iter;

	struct addrinfo hints = {
		.ai_flags = AI_PASSIVE,
		.ai_family = 0,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
		.ai_addrlen = 0,
		.ai_canonname = NULL,
		.ai_addr = NULL,
		.ai_next = NULL
	};

	if (getaddrinfo(a->addr, a->port, &hints, &sockai) != 0) {
		perror("ERROR getaddrinfo()");
		return -1;
	}

	ai_iter = sockai;

	do {
		if ((s = socket(ai_iter->ai_family, ai_iter->ai_socktype,
				ai_iter->ai_protocol)) < 0) {
			continue;
		}

		if (connect(s, ai_iter->ai_addr, ai_iter->ai_addrlen) == 0) {
			a->fd = s;
			break;
		}

		fprintf(stderr, "ERROR connect(): %s",
			strerror(errno));
		s = -1;
	} while ((ai_iter = ai_iter->ai_next) != NULL);

	return s;
}

int app_parse_args(struct app *a)
{
	if (a->argc == 4) {
		memset(a->name, '\0', sizeof(a->name));
		strncpy(a->name, a->argv[3], sizeof(a->name) - 1);
	} else if (a->argc != 3) {
		fprintf(stderr, "Bad argument count %d, "
			"2 or 3 expected\n", a->argc - 1);
		return -1;
	} else {
		strcpy(a->name, "default");
	}

	strncpy(a->addr, a->argv[1], sizeof(a->addr) - 1);
	strncpy(a->port, a->argv[2], sizeof(a->port) - 1);

	return 0;
}

int app_poll_data(struct app *a)
{
	int err;

	struct pollfd pfd = {
		.fd = a->fd,
		.events = POLLIN
	};

	err = poll(&pfd, 1, -1);

	if (err < 0) {
		perror("ERROR Failed polling socket");
		return -1;
	}

	for (unsigned int i = 0; i < ARRAY_LEN(a->data); ++i) {
		char c;

		read(a->fd, &c, sizeof(c));

		switch (c) {
		case '\r':
		case '\n':
			a->data[i] = '\0';
			return 0;
		default:
			a->data[i] = c;
			break;
		}
	}

	fprintf(stderr, "Data buffer filled before newline: %s\n",
		a->data);

	return -1;
}

int app_close_file(struct app *a)
{
	close(a->fd);

	return 0;
}

int app_print_data(struct app *a)
{
	printf("%s\t%s\n", a->data, a->name);

	return 0;
}

void signal_handler(int signal)
{
	fprintf(stderr, "Received signal %d, %s\n", signal,
		strsignal(signal));

	switch (signal) {
	case SIGINT:
	case SIGTERM:
		app_close_file(&local_app);
		exit(0);
	default:
		app_close_file(&local_app);
		exit(-1);
	}
}

int main(int argc, const char **argv)
{
	local_app.fd = -1;
	local_app.argc = argc;
	local_app.argv = argv;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);

	APP_RUN(app_parse_args(&local_app));
	APP_RUN(app_tcp_connect(&local_app));
	APP_RUN(app_poll_data(&local_app));
	APP_RUN(app_print_data(&local_app));

	return 0;
}
