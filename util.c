/*
 * Utility functions
 * Copyright (C) 2013 Unix Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License (COPYING file) for more details.
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "tsdumper2.h"

#ifdef __linux__
#include <sys/prctl.h>

void set_thread_name(char *thread_name) {
	prctl(PR_SET_NAME, thread_name, NULL, NULL, NULL);
}

#else
void set_thread_name(char *thread_name) {
    (void)thread_name;
}

#endif

int parse_host_and_port(char *input, struct io *io) {
	int port_set = 0;
	char *p, *proto;
	io->type = UDP;
	if (strlen(input) == 0)
		return 0;
	if (strstr(input, "udp://") == input)       io->type = UDP;
	else if (strstr(input, "rtp://") == input)  io->type = RTP;
	else
		die("Unsupported protocol (patch welcome): %s", input);
	proto = strstr(input, "://");
	if (proto)
		input = proto + 3;
	io->hostname = input;
	if (input[0] == '[') { // Detect IPv6 static address
		p = strrchr(input, ']');
		if (!p)
			die("Invalid IPv6 address format: %s\n", input);
		io->hostname = input + 1; // Remove first [
		*p = 0x00; // Remove last ]
		char *p2 = strchr(p + 1, ':');
		if (p2) {
			*p2 = 0x00;
			io->service = p2 + 1;
			port_set = 1;
		}
	} else {
		p = strrchr(input, ':');
		if (p) {
			*p = 0x00;
			io->service = p + 1;
			port_set = 1;
		}
	}
	if (io->service) {
		char *path = strstr(io->service, "/");
		if (path)
			path[0] = 0;
	}
	if (!port_set)
		die("Port is not set in the input url.");
	return 1;
}

char *my_inet_ntop(int family, struct sockaddr *addr, char *dest, int dest_len) {
	struct sockaddr_in  *addr_v4 = (struct sockaddr_in  *)addr;
	struct sockaddr_in6 *addr_v6 = (struct sockaddr_in6 *)addr;
	switch (family) {
		case AF_INET:
			return (char *)inet_ntop(AF_INET, &addr_v4->sin_addr, dest, dest_len);
			break;
		case AF_INET6:
			return (char *)inet_ntop(AF_INET6, &addr_v6->sin6_addr, dest, dest_len);
			break;
		default:
			memset(dest, 0, dest_len);
			strcpy(dest, "unknown");
			return dest;
	}
}

int create_dir(const char *dir, mode_t mode) {
	int ret = 0;
	unsigned int i;

	// Shortcut
	if (strchr(dir, '/') == NULL)
		return mkdir(dir, mode);

	char *d = strdup(dir);
	unsigned int dlen = strlen(dir);

	// Skip first char (it can be /)
	for (i = 1; i < dlen; i++) {
		if (d[i] != '/')
			continue;
		d[i] = '\0';
		ret = mkdir(d, mode);
		d[i] = '/';
		if (ret < 0 && errno != EEXIST)
			goto OUT;
	}
	ret = mkdir(d, mode);
OUT:
	free(d);
	return ret;
}

void die(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "ERROR: ");
	vfprintf(stderr, fmt, args);
	if (fmt[strlen(fmt) - 1] != '\n')
		fprintf(stderr, "\n");
	va_end(args);
	exit(EXIT_FAILURE);
}

void p_err(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "ERROR: ");
	vfprintf(stderr, fmt, args);
	if (fmt[strlen(fmt) - 1] != '\n')
		fprintf(stderr, "\n");
	va_end(args);
}

void p_info(const char *fmt, ...) {
	if (DEBUG > 0) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		fprintf(stdout, "%08ld.%08ld ", tv.tv_sec, tv.tv_usec);

		char date[64];
		struct tm tm;
		localtime_r(&tv.tv_sec, &tm);
		strftime(date, sizeof(date), "%F %H:%M:%S", localtime_r(&tv.tv_sec, &tm));
		fprintf(stdout, "%s | ", date);
	}
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	if (fmt[strlen(fmt) - 1] != '\n')
		fprintf(stdout, "\n");
	va_end(args);
	fflush(stdout);
}
