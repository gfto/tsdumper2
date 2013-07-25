/*
 * Data definitions
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
#ifndef DATA_H
#define DATA_H

#include <pthread.h>
#include <inttypes.h>
#include <stdbool.h>

#include "libfuncs/libfuncs.h"

// Supported values 0, 1 and 2. Higher value equals more spam in the log.
#define DEBUG 0

// 7 * 188
#define FRAME_SIZE 1316

// 64k should be enough for everybody
#define THREAD_STACK_SIZE (64 * 1024)

enum io_type {
	UDP,
	RTP,
};

// 1.2MB = ~13Mbit/s
#define PACKET_MAX_LENGTH (1316 * 1024)

// Maximum packet fill time in ms
#define PACKET_MAX_TIME 1000

#define PREFIX_MAX_LENGTH 64

// PREFIX-20130717_000900-1374008940.ts (PREFIX-YYYYMMDD_HHMMSS-0123456789.ts)
#define OUTFILE_NAME_MAX  (PREFIX_MAX_LENGTH + 128)

#define NUM_PACKETS 16

struct packet {
	int					num;
	struct timeval		ts;							// packet start time
	int					allocated;					// set to true if the struct is dynamically allocated
	int					in_use;						// this packet is currently being used
	int					data_len;					// data length
	uint8_t				data[PACKET_MAX_LENGTH];	// the data
};

struct io {
	int					fd;
	enum io_type		type;
	char				*hostname;
	char				*service;
};

struct ts {
	char				*prefix;
	char				*output_dir;
	int					create_dirs;
	int					rotate_secs;
	int					ts_discont;
	struct io			input;

	pthread_attr_t		thread_attr;
	pthread_t			write_thread;

	struct packet		packets[NUM_PACKETS];
	int					packet_ptr;

	struct packet		*current_packet;
	QUEUE				*packet_queue;

	int					output_fd;
	time_t				output_startts;
	char				output_dirname[OUTFILE_NAME_MAX];
	char				output_filename[OUTFILE_NAME_MAX];
	char				output_full_filename[OUTFILE_NAME_MAX];
};

#include "util.h"

// From tsdumper2.c
struct packet *alloc_packet(struct ts *ts);
void free_packet(struct packet *packet);

// From process.c
void *write_thread(void *_ts);
void process_packets(struct ts *ts, uint8_t *ts_packet, ssize_t readen);

// From udp.c
int udp_connect_input(struct io *io);

#endif
