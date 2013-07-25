/*
 * Utility functions header
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
#ifndef UTIL_H
#define UTIL_H

#include <inttypes.h>
#include <arpa/inet.h>

void set_thread_name(char *thread_name);

int parse_host_and_port(char *input, struct io *io);
char *my_inet_ntop(int family, struct sockaddr *addr, char *dest, int dest_len);

int create_dir(const char *dir, mode_t mode);

#define p_dbg1(fmt, ...) \
	do { if (DEBUG > 0) p_info(fmt, __VA_ARGS__); } while(0)

#define p_dbg2(fmt, ...) \
	do { if (DEBUG > 1) p_info(fmt, __VA_ARGS__); } while(0)

__attribute__ ((format(printf, 1, 2))) void die(const char *fmt, ...);

__attribute__ ((format(printf, 1, 2))) void p_err(const char *fmt, ...);
__attribute__ ((format(printf, 1, 2))) void p_info(const char *fmt, ...);

#endif
