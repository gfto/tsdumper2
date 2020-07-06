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

#define PROGRAM_NAME "tsdumper2-simple"
static const char *program_id = PROGRAM_NAME " v" VERSION " (git-" GIT_VER ", build date " BUILD_ID ")";

static int keep_running = 1;

static const char short_options[] = "n:s:d:i:z46DhV";

static const struct option long_options[] = {
	{ "input",				required_argument, NULL, 'i' },

	{ "help",				no_argument,       NULL, 'h' },
	{ "version",			no_argument,       NULL, 'V' },

	{ 0, 0, 0, 0 }
};

static void show_help(void) {
	printf("%s\n", program_id);
	printf("Copyright (C) 2013 Unix Solutions Ltd.\n");
	printf("\n");
	printf("	Usage: " PROGRAM_NAME " -n <name> -i <input>\n");
	printf("\n");
	printf("Input options:\n");
	printf(" -i --input <source>        | Where to read from.\n");
	printf("                            .  -i udp://224.0.0.1:5000    (v4 multicast)\n");
	printf("                            .  -i udp://[ff01::1111]:5000 (v6 multicast)\n");
	printf("\n");
	printf("Misc options:\n");
	printf(" -h --help                  | Show help screen.\n");
	printf(" -V --version               | Show program version.\n");
	printf("\n");
}

static void parse_options(struct ts *ts, int argc, char **argv) {
	int j, input_addr_err = 1;
	while ((j = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (j) {
			case 'i': // --input
				input_addr_err = !parse_host_and_port(optarg, &ts->input);
				break;
			case 'h': // --help
				show_help();
				exit(EXIT_SUCCESS);
			case 'V': // --version
				printf("%s\n", program_id);
				exit(EXIT_SUCCESS);
		}
	}
	if (input_addr_err) {
		show_help();
		if (input_addr_err)
			fprintf(stderr, "ERROR: Input address is invalid (--input XXX | -i XXX).\n");
		exit(EXIT_FAILURE);
	}
}

static uint8_t ts_packet[FRAME_SIZE];
static struct ts ts;

int main(int argc, char **argv) {
	ts.output_fd = -1;
	parse_options(&ts, argc, argv);
	if (udp_connect_input(&ts.input) < 1)
		exit(EXIT_FAILURE);
	do {
		set_log_io_errors(0);
		ssize_t readen = fdread_ex(ts.input.fd, (char *)ts_packet, FRAME_SIZE, 250, 4, 1);
		set_log_io_errors(1);
		write(1, ts_packet, readen);
	} while (keep_running);
	exit(EXIT_SUCCESS);
}
