/*
 * Process incoming data and save it into files.
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "tsdumper2.h"

#define NO_UNLINK  0
#define UNLINK_OLD 1

#define ALIGN_DOWN(__src, __value) (__src - (__src % __value))

static mode_t dir_perm;

static int file_exists(char *filename) {
	return access(filename, W_OK) == 0;
}

static void format_output_filename(struct ts *ts, time_t file_time) {
	struct tm file_tm;
	localtime_r(&file_time, &file_tm);

	ts->output_startts = file_time;

	ts->output_filename[0] = '\0';
	strcat(ts->output_filename, ts->prefix);
	strcat(ts->output_filename, "-");
	strftime(ts->output_filename + strlen(ts->output_filename), OUTFILE_NAME_MAX, "%Y%m%d_%H%M%S-%s.ts", &file_tm);

	ts->output_dirname[0] = '\0';
	strftime(ts->output_dirname, OUTFILE_NAME_MAX, "%Y/%m/%d/%H", &file_tm);

	ts->output_full_filename[0] = '\0';
	snprintf(ts->output_full_filename, sizeof(ts->output_full_filename), "%s/%s",
		ts->output_dirname, ts->output_filename);
}

static void report_file_creation(struct ts *ts, char *text_prefix, char *filename) {
	char qdepth[32];
	qdepth[0] = '\0';
	if (ts->packet_queue->items)
		snprintf(qdepth, sizeof(qdepth), " (depth:%d)", ts->packet_queue->items);
	p_info("%s%s%s\n", text_prefix, filename, qdepth);
}

static void create_output_directory(struct ts *ts) {
	if (!ts->create_dirs)
		return;
	if (!file_exists(ts->output_dirname)) {
		p_info(" = Create directory %s", ts->output_dirname);
		create_dir(ts->output_dirname, dir_perm);
	}
}

static int create_output_file(struct ts *ts) {
	char *filename = ts->output_filename;
	if (ts->create_dirs)
		filename = ts->output_full_filename;
	create_output_directory(ts);
	report_file_creation(ts, " = Create new file ", filename);
	int fd = open(ts->output_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		p_err("Can't create output file %s", ts->output_filename);
		return -1;
	}
	if (ts->create_dirs) {
		link(ts->output_filename, ts->output_full_filename);
	}
	return fd;
}

static int append_output_file(struct ts *ts) {
	char *filename = ts->output_filename;
	if (ts->create_dirs)
		filename = ts->output_full_filename;
	create_output_directory(ts);
	report_file_creation(ts, " + Append to file ", filename);
	int fd = open(ts->output_filename, O_APPEND | O_WRONLY);
	if (fd < 0) {
		p_err("Can't append to output file %s", ts->output_filename);
		return -1;
	}
	if (ts->create_dirs) {
		link(ts->output_filename, ts->output_full_filename);
	}
	return fd;
}

static void close_output_file(struct ts *ts, int unlink_file) {
	if (ts->output_fd > -1) {
		close(ts->output_fd);
		if (unlink_file && ts->create_dirs) {
			// The file is hard linked into the subdirectory. There is no need
			// to keep it in the main directory.
			unlink(ts->output_filename);
		}
	}
}

static void handle_files(struct ts *ts, struct packet *packet) {
	int file_time = ALIGN_DOWN(packet->ts.tv_sec, ts->rotate_secs);

	// Is this file already created?
	if (file_time <= ts->output_startts)
		return;

	close_output_file(ts, UNLINK_OLD);
	format_output_filename(ts, file_time);

	/*
	 * When tsdumper2 is started, try to continue writing into "current" file.
	 * This allows the program to be killed/restarted.
	 *
	 * If current file does not exist, create new file with the time of the start
	 * (not aligned to rotate_secs).
	 */
	int append = 0;
	if (ts->output_fd < 0) { // First file (or error).
		append = file_exists(ts->output_filename);
		if (!append) { // Create first file *NOT ALIGNED*
			format_output_filename(ts, packet->ts.tv_sec);
		}
	}

	ts->output_fd = append ? append_output_file(ts) : create_output_file(ts);
}

void *write_thread(void *_ts) {
	struct ts *ts = _ts;
	struct packet *packet;

	mode_t umask_val = umask(0);
	dir_perm = (0777 & ~umask_val) | (S_IWUSR | S_IXUSR);

	set_thread_name("tsdump-write");
	while ((packet = queue_get(ts->packet_queue))) {
		if (!packet->data_len)
			continue;

		p_dbg1(" - Got packet %d, size: %u, file_time:%lu packet_time:%lu depth:%d\n",
			packet->num, packet->data_len, ALIGN_DOWN(packet->ts.tv_sec, ts->rotate_secs),
			packet->ts.tv_sec, ts->packet_queue->items);

		handle_files(ts, packet);

		if (ts->output_fd > -1) {
			p_dbg2(" - Writing into fd:%d size:%d file:%s\n", ts->output_fd, packet->data_len, ts->output_filename);
			ssize_t written = write(ts->output_fd, packet->data, packet->data_len);
			if (written != packet->data_len) {
				p_err("Can not write data (fd:%d written %zd of %d file:%s)",
					ts->output_fd, written, packet->data_len, ts->output_filename);
			}
		}
		free_packet(packet);
	}
	close_output_file(ts, NO_UNLINK);
	return NULL;
}

static struct packet *add_to_queue(struct ts *ts) {
	queue_add(ts->packet_queue, ts->current_packet);
	ts->current_packet = alloc_packet(ts);
	return ts->current_packet;
}

void process_packets(struct ts *ts, uint8_t *ts_packet, ssize_t readen) {
	struct timeval now;
	struct packet *packet = ts->current_packet;

	if (packet->data_len + readen < PACKET_MAX_LENGTH) {
		// Add data to buffer
		memcpy(packet->data + packet->data_len, ts_packet, readen);
		packet->data_len += readen;
	} else {
		// Too much data, add to queue
		p_dbg1("*** Reached buffer end (%zd + %zd > %d)\n", packet->data_len + readen, readen, PACKET_MAX_LENGTH);
		packet = add_to_queue(ts);
	}

	if (!packet->ts.tv_sec)
		gettimeofday(&packet->ts, NULL);

	gettimeofday(&now, NULL);
	unsigned long long diff = timeval_diff_msec(&packet->ts, &now);
	if (diff > PACKET_MAX_TIME) {
		// Too much time have passed, add to queue
		p_dbg1("+++ Reached time limit (%llu > %d)\n", diff, PACKET_MAX_TIME);
		add_to_queue(ts);
	}
}
