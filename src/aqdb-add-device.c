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
 * @file aqdb-add-device.c
 * @author Tyler J. Anderson
 *
 * Command-line utility to add a new device to an existing air quality
 * database
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libpq-fe.h>

#include <unistd.h>
#include <signal.h>

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

#define APP_OK 0
#define APP_E_BAD_ARGS 0x01
#define APP_E_OOM 0x02

#define CHECK_MAKE_QUERY(rslt, len)					\
	if (rslt > (int) len) {						\
		fprintf(stderr, "Query too large for buffer\n");	\
		return NULL;						\
	}

struct device {
	char sernum[128];
	char name[256];
	int numrows;
	int interval;
} static dev;

static unsigned long app_status = 0;
static int devid;
static char buffer[2048];
const char *conninfo = NULL;
static PGconn *conn = NULL;
static PGresult *res = NULL;

int get_next_available_key(const char *table, const char *key)
{
	const unsigned int bufsize = 2048;
	char query[bufsize];
	int rslt;

	rslt = snprintf(query, bufsize,
			"select max(%s) from %s", key, table);

	if (rslt > (int) bufsize) {
		fprintf(stderr, "Key query too large for buffer\n");
		return -1;
	}

	res = PQexec(conn, query);

	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		fprintf(stderr, "Error in Next Available Key query\n");
		return -1;
	}

	rslt = PQnfields(res);

	if (rslt != 1) {
		fprintf(stderr, "Max key returned more than one field\n");
		return -1;
	}

	rslt = PQntuples(res);

	if (rslt != 1) {
		fprintf(stderr, "Max key returned more than one result\n");
		return -1;
	}

	rslt = strtol(PQgetvalue(res, 0, 0), NULL, 10);
	PQclear(res);

	return rslt + 1;
}

/* Define the device query, return NULL if failure */
const char *make_query_define_device(char *buf, unsigned int len,
				     const char *sernum,
				     const char *devname)
{
	int rslt;

	devid = get_next_available_key("aqfeather.devices", "devid");

	if (devid < 0)
		return NULL;

	rslt = snprintf(buf, len,
			"insert into aqfeather.devices "
			"(devid, serialnum, devname) "
			"values (%d, '%s', '%s') "
			"on conflict do nothing"
			, devid, sernum, devname);

	CHECK_MAKE_QUERY(rslt, len);

	return buf;
}

const char *make_query_define_meta(char *buf, unsigned int len,
				   int numrows, int measinterval)
{
	int dmid;
	int rslt;

	dmid = get_next_available_key("aqfeather.dev_metadata", "dmid");

	if (dmid < 0)
		return NULL;

	rslt = snprintf(buf, len,
			"insert into aqfeather.dev_metadata "
			"(dmid, devid, numrows, measinterval) "
			"values (%d, %d, %d, '%d seconds'::interval) "
			"on conflict do nothing",
			dmid, devid, numrows, measinterval);

	CHECK_MAKE_QUERY(rslt, len);

	return buf;
}

const char *make_query_cursor_init(char *buf, unsigned int len)
{
	int rrcid;
	int rslt;

	rrcid = get_next_available_key("aqfeather.rrcursor", "rrcid");

	if (rrcid < 0)
		return NULL;

	rslt = snprintf(buf, len,
			"insert into aqfeather.rrcursor "
			"(rrcid, rrid, devid, updatetime) "
			"values ("
			"%d, "
			"1::int, "
			"%d, "
			"transaction_timestamp()) "
			"on conflict do nothing",
			rrcid, devid);

	CHECK_MAKE_QUERY(rslt, len);

	return buf;
}

const char *make_query_rrdata_init(char *buf, unsigned int len, int numrows)
{
	int rslt;

	rslt = snprintf(buf, len,
			"insert into aqfeather.rrdata "
			"select s.a::int as rrid, %d::int as devid, "
			"NULL::jsonb as datastring "
			"from generate_series(1::int, %d::int, 1::int) "
			" as s(a) "
			"on conflict do nothing", devid, numrows);

	CHECK_MAKE_QUERY(rslt, len);

	return buf;
}

void print_usage(int argc, char *const argv[])
{
	(void) argc;

	fprintf(stderr,
		"Usage:\n"
		"%s [-C <connstring>] -s <serial> -n <devname> -i <interval>\n"
		"\t-t <timeperiod>\n"
		"\n"
		"Options:\n"
		"-C <connstring>     Connection string for database (optional)\n"
		"-s <serial>         Serial number for device\n"
		"-n <devname>        Unique name for device\n"
		"-i <interval>       The time interval between measurements in seconds\n"
		"-t <timeperiod>     The length of time in hours to store measurements\n"
		"-h                  Print this usage message\n",
		argv[0]);
}

void check_args(int argc, char * const argv[])
{
	int ch;
	int reqargs = 's' + 'n' + 'i' + 't';
	long long timeperiod = 0;

	while ((ch = getopt(argc, argv, "C:s:n:i:t:h")) != -1) {
		switch (ch) {
		case 'C':
			conninfo = optarg;
			break;
		case 's':
			strncpy(dev.sernum, optarg, sizeof(dev.sernum));
			reqargs -= ch;
			break;
		case 'n':
			strncpy(dev.name, optarg, sizeof(dev.name));
			reqargs -= ch;
			break;
		case 'i':
			dev.interval = strtol(optarg, NULL, 10);
			reqargs -= ch;
			break;
		case 't':
			timeperiod = strtoll(optarg, NULL, 10);
			reqargs -= ch;
			break;
		case 'h':
			print_usage(argc, argv);
			exit(EXIT_SUCCESS);
			break;
		default:
			print_usage(argc, argv);
			app_status |= APP_E_BAD_ARGS;
			raise(SIGUSR1);
			break;
		}
	}

	if (reqargs) {
		print_usage(argc, argv);
		app_status |= APP_E_BAD_ARGS;
		raise(SIGUSR1);
	}

	timeperiod = timeperiod * 60ll * 60ll / dev.interval;
	dev.numrows = timeperiod;
}

void db_connect()
{
	conn = PQconnectdb(conninfo);

	if (PQstatus(conn) != CONNECTION_OK) {
		fprintf(stderr, "Failed to connect\n");
		exit(EXIT_FAILURE);
	}

	res = PQexec(conn, "select pg_catalog.set_config('search_path', "
		     "'', false)");

	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		fprintf(stderr, "Failed to set always-secure search path\n");
		exit(EXIT_FAILURE);
	}
}

void db_disconnect()
{
	if (res)
		PQclear(res);

	if (conn)
		PQfinish(conn);
}

void db_send_query(const char *query, const char *msg)
{
	if (!query) raise(SIGABRT);

	res = PQexec(conn, query);

	if (PQresultStatus(res) != PGRES_TUPLES_OK &&
	    PQresultStatus(res) != PGRES_COMMAND_OK) {
		const char *errmsg = PQresultErrorMessage(res);
		fprintf(stderr, "Error on query: %s,\n %s\n", msg, errmsg);
		raise(SIGABRT);
	}
}

void do_you_want_to_quit()
{
	char c;
	printf("Do you want to quit?\n"
	       "Press y for yes or n for no: ");
	c = getchar();

	switch (c) {
	case 'y':
		printf("Canceling operation...\n");
		db_disconnect();
		printf("Success, exiting\n");
		exit(EXIT_SUCCESS);
		break;
	case 'n':
		printf("No response received, continuing\n");
		break;
	default:
		printf("Response not recognized, continuing\n");
		break;
	}
}

void app_status_event() {
	while (app_status) {
		switch (app_status) {
		case APP_E_BAD_ARGS:
			app_status &= ~APP_E_BAD_ARGS;
			fprintf(stderr, "Bad or malformed arguments passed, exiting\n");
			exit(EXIT_FAILURE);
			break;
		case APP_E_OOM:
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
			break;
		default:
			fprintf(stderr, "Warning: undefined status\n");
			app_status = APP_OK;
			break;
		}
	}
}

void signal_handler(int sig)
{
	switch (sig) {
	case SIGINT:
		do_you_want_to_quit();
		break;
	case SIGUSR1:
		app_status_event();
		break;
	case SIGTERM:
	case SIGHUP:
	case SIGABRT:
	case SIGTRAP:
	default:
		fprintf(stderr, "Sorry, we are forced to exit early because "
			"signal %d was received.\n", sig);
		db_disconnect();
		exit(EXIT_FAILURE);
		break;
	}
}

int main(int argc, char *const argv[])
{
	const char *strq;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGTRAP, signal_handler);
	signal(SIGUSR1, signal_handler);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGINFO, SIG_IGN);

	check_args(argc, argv);
	db_connect();

	/* If we made it to this stage, now we can do our work! */
	strq = make_query_define_device(buffer, sizeof(buffer),
					dev.sernum, dev.name);
	db_send_query(strq, "Define Device");

	strq = make_query_define_meta(buffer, sizeof(buffer),
				      dev.numrows, dev.interval);
	db_send_query(strq, "Define Meta");

	strq = make_query_cursor_init(buffer, sizeof(buffer));
	db_send_query(strq, "Init Cursor");

	strq = make_query_rrdata_init(buffer, sizeof(buffer), dev.numrows);
	db_send_query(strq, "Init Data");

	/* We did it! Yay! Now let's say goodbye to the db and exit! */
	db_disconnect();
	printf("Successfully added device\n");
	exit(EXIT_SUCCESS);
}
