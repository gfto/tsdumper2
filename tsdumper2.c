/*
 * tsdumper2
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
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h>

#include "tsdumper2.h"

extern int ai_family;

#define PROGRAM_NAME "tsdumper2"
static const char *program_id = PROGRAM_NAME " v" VERSION " (git-" GIT_VER ", build date " BUILD_ID ")";

static int keep_running = 1;
static unsigned long long total_read;

static const char short_options[] = "n:s:d:i:z46DhV";

static const struct option long_options[] = {
	{ "prefix",				required_argument, NULL, 'n' },
	{ "seconds",			required_argument, NULL, 's' },
	{ "output-dir",			required_argument, NULL, 'd' },
	{ "create-dirs",		no_argument,       NULL, 'D' },

	{ "input",				required_argument, NULL, 'i' },
	{ "input-ignore-disc",	no_argument,       NULL, 'z' },
	{ "ipv4",				no_argument,       NULL, '4' },
	{ "ipv6",				no_argument,       NULL, '6' },

	{ "help",				no_argument,       NULL, 'h' },
	{ "version",			no_argument,       NULL, 'V' },

	{ 0, 0, 0, 0 }
};

static void show_help(struct ts *ts) {
	printf("%s\n", program_id);
	printf("Copyright (C) 2013 Unix Solutions Ltd.\n");
	printf("\n");
	printf("	Usage: " PROGRAM_NAME " -n <name> -i <input>\n");
	printf("\n");
	printf("Settings:\n");
	printf(" -n --prefix <name>         | Filename prefix.\n");
	printf(" -s --seconds <seconds>     | How much to save (default: %u sec).\n", ts->rotate_secs);
	printf(" -d --output-dir <dir>      | Startup directory (default: %s).\n", ts->output_dir);
	printf(" -D --create-dirs           | Save files in subdirs YYYY/MM/DD/HH/file.\n");
	printf("\n");
	printf("Input options:\n");
	printf(" -i --input <source>        | Where to read from.\n");
	printf("                            .  -i udp://224.0.0.1:5000    (v4 multicast)\n");
	printf("                            .  -i udp://[ff01::1111]:5000 (v6 multicast)\n");
	printf("                            .  -i rtp://224.0.0.1:5000    (v4 RTP input)\n");
	printf("                            .  -i rtp://[ff01::1111]:5000 (v6 RTP input)\n");
	printf(" -z --input-ignore-disc     | Do not report discontinuty errors in input.\n");
	printf(" -4 --ipv4                  | Use only IPv4 addresses.\n");
	printf(" -6 --ipv6                  | Use only IPv6 addresses.\n");
	printf("\n");
	printf("Misc options:\n");
	printf(" -h --help                  | Show help screen.\n");
	printf(" -V --version               | Show program version.\n");
	printf("\n");
}

static void parse_options(struct ts *ts, int argc, char **argv) {
	int j, input_addr_err = 1;
	while ((j = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		if (j == '?')
			exit(EXIT_FAILURE);
		switch (j) {
			case 'n': // --prefix
				ts->prefix = optarg;
				if (strlen(optarg) >= PREFIX_MAX_LENGTH)
					die("Prefix is longer than %d characters!", PREFIX_MAX_LENGTH);
				break;
			case 's': // --seconds
				ts->rotate_secs = atoi(optarg);
				break;
			case 'd': // --output-dir
				ts->output_dir = optarg;
				break;
			case 'D': // --create-dirs
				ts->create_dirs = !ts->create_dirs;
				break;
			case 'i': // --input
				input_addr_err = !parse_host_and_port(optarg, &ts->input);
				break;
			case 'z': // --input-ignore-disc
				ts->ts_discont = !ts->ts_discont;
				break;
			case '4': // --ipv4
				ai_family = AF_INET;
				break;
			case '6': // --ipv6
				ai_family = AF_INET6;
				break;
			case 'h': // --help
				show_help(ts);
				exit(EXIT_SUCCESS);
			case 'V': // --version
				printf("%s\n", program_id);
				exit(EXIT_SUCCESS);
		}
	}
	if (input_addr_err || !ts->prefix) {
		show_help(ts);
		if (!ts->prefix)
			fprintf(stderr, "ERROR: File name prefix is not set (--prefix XXX | -n XXX).\n");
		if (input_addr_err)
			fprintf(stderr, "ERROR: Input address is invalid (--input XXX | -i XXX).\n");
		exit(EXIT_FAILURE);
	}

	p_info("Prefix     : %s\n", ts->prefix);
	p_info("Input addr : %s://%s:%s/\n",
		ts->input.type == UDP ? "udp" :
		ts->input.type == RTP ? "rtp" : "???",
		ts->input.hostname, ts->input.service);
	p_info("Seconds    : %u\n", ts->rotate_secs);
	p_info("Output dir : %s (create directories: %s)\n", ts->output_dir,
		ts->create_dirs ? "YES" : "no");
	if (chdir(ts->output_dir) < 0)
		die("Can not change directory to %s: %s\n", ts->output_dir, strerror(errno));
}

void signal_quit(int sig) {
	if (!keep_running)
		raise(sig);
	keep_running = 0;
	p_info("Killed %s with signal %d\n", program_id, sig);
	signal(sig, SIG_DFL);
}

static void clear_packet(struct packet *p) {
	p->ts.tv_sec  = 0;
	p->ts.tv_usec = 0;
	p->data_len   = 0;
	p->allocated  = 0;
	p->in_use     = 0;
}

struct packet *alloc_packet(struct ts *ts) {
	// check for free static allocations
	struct packet *p;
	int i;
	for (i = 0; i < NUM_PACKETS; i++) {
		p = &ts->packets[i];
		if (!p->in_use) {
			p->in_use = 1;
			p_dbg2("STATIC packet, num %d\n", p->num);
			goto OUT;
		}
	}
	// Dynamically allocate packet
	p = malloc(sizeof(struct packet));
	if (!p)
		die("Can't alloc %lu bytes.\n", (unsigned long)sizeof(struct packet));
	clear_packet(p);
	p->num       = time(NULL);
	p->allocated = 1;
	p_dbg2("ALLOC  packet, num %d\n", p->num);
OUT:
	return p;
}

void free_packet(struct packet *packet) {
	if (!packet->allocated) {
		clear_packet(packet);
		return;
	}
	p_dbg2("FREE   packet, num %d\n", packet->num);
	free(packet);
}

#define RTP_HDR_SZ  12

static uint8_t ts_packet[FRAME_SIZE + RTP_HDR_SZ];
static uint8_t rtp_hdr[2][RTP_HDR_SZ];
static struct ts ts;

int main(int argc, char **argv) {
	int i;
	int have_data = 1;
	int ntimeouts = 0;
	int rtp_hdr_pos = 0, num_packets = 0;
	struct rlimit rl;

	if (getrlimit(RLIMIT_STACK, &rl) == 0) {
		if (rl.rlim_cur > THREAD_STACK_SIZE) {
			rl.rlim_cur = THREAD_STACK_SIZE;
			setrlimit(RLIMIT_STACK, &rl);
		}
	}

	memset(rtp_hdr[0], 0, RTP_HDR_SZ);
	memset(rtp_hdr[1], 0, RTP_HDR_SZ);

	for (i = 0; i < NUM_PACKETS; i++) {
		struct packet *p = &ts.packets[i];
		p->num = i + 1;
	}

	ts.ts_discont     = 1;
	ts.output_dir     = ".";
	ts.rotate_secs    = 60;
	ts.current_packet = alloc_packet(&ts);
	ts.output_fd      = -1;

	pthread_attr_init(&ts.thread_attr);
	size_t stack_size;
	pthread_attr_getstacksize(&ts.thread_attr, &stack_size);
	if (stack_size > THREAD_STACK_SIZE)
		pthread_attr_setstacksize(&ts.thread_attr, THREAD_STACK_SIZE);

	parse_options(&ts, argc, argv);

	ts.packet_queue   = queue_new();

	p_info("Start %s\n", program_id);

	switch (ts.input.type) {
	case UDP:
	case RTP:
		if (udp_connect_input(&ts.input) < 1)
			exit(EXIT_FAILURE);
		break;
	}

	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	signal(SIGINT , signal_quit);
	signal(SIGTERM, signal_quit);

	pthread_create(&ts.write_thread , &ts.thread_attr, &write_thread , &ts);

	int data_received = 0;
	do {
		ssize_t readen = -1;
		set_log_io_errors(0);
		switch (ts.input.type) {
		case UDP:
			readen = fdread_ex(ts.input.fd, (char *)ts_packet, FRAME_SIZE, 250, 4, 1);
			break;
		case RTP:
			readen = fdread_ex(ts.input.fd, (char *)ts_packet, FRAME_SIZE + RTP_HDR_SZ, 250, 4, 1);
			if (readen > RTP_HDR_SZ) {
				memcpy(rtp_hdr[rtp_hdr_pos], ts_packet, RTP_HDR_SZ);
				memmove(ts_packet, ts_packet + RTP_HDR_SZ, FRAME_SIZE);
				readen -= RTP_HDR_SZ;
				uint16_t ssrc  = (rtp_hdr[rtp_hdr_pos][2] << 8) | rtp_hdr[rtp_hdr_pos][3];
				uint16_t pssrc = (rtp_hdr[!rtp_hdr_pos][2] << 8) | rtp_hdr[!rtp_hdr_pos][3];
				rtp_hdr_pos = !rtp_hdr_pos;
				if (pssrc + 1 != ssrc && (ssrc != 0 && pssrc != 0xffff) && num_packets > 2)
					if (ts.ts_discont)
						p_info(" *** RTP discontinuity last_ssrc %5d, curr_ssrc %5d, lost %d packet ***\n",
							pssrc, ssrc, ((ssrc - pssrc)-1) & 0xffff);
				num_packets++;
			}
			break;
		}
		set_log_io_errors(1);
		if (readen < 0) {
			p_info(" *** Input read timeout ***\n");
			data_received = 0;
			ntimeouts++;
		} else {
			if (ntimeouts && readen > 0) {
				ntimeouts = 0;
			}
		}
		if (readen > 0) {
			if (!data_received) {
				p_info("Data received.\n");
				data_received = 1;
			}
			total_read += readen;
			process_packets(&ts, ts_packet, readen);
		}
		if (!keep_running)
			break;
	} while (have_data);

	queue_add(ts.packet_queue, ts.current_packet);
	queue_add(ts.packet_queue, NULL); // Exit write_thread
	pthread_join(ts.write_thread, NULL);

	p_info("Stop %s (bytes_processed:%llu).\n", program_id, total_read);

	queue_free(&ts.packet_queue);

	pthread_attr_destroy(&ts.thread_attr);

	exit(EXIT_SUCCESS);
}
