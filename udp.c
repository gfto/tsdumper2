/*
 * UDP functions
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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "tsdumper2.h"

#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

int ai_family = AF_UNSPEC;

static int is_multicast(struct sockaddr_storage *addr) {
	int ret = 0;
	switch (addr->ss_family) {
	case AF_INET: {
		struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
		ret = IN_MULTICAST(ntohl(addr4->sin_addr.s_addr));
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
		ret = IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr);
		break;
	} }
	return ret;
}

static int bind_addr(const char *hostname, const char *service, int socktype, struct sockaddr_storage *addr, int *addrlen, int *sock) {
	struct addrinfo hints, *res, *ressave;
	int n, ret = -1;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = ai_family;
	hints.ai_socktype = socktype;

	n = getaddrinfo(hostname, service, &hints, &res);
	if (n < 0) {
		p_info("ERROR: getaddrinfo(%s): %s\n", hostname, gai_strerror(n));
		return ret;
	}

	ressave = res;
	while (res) {
		*sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (*sock > -1) {
			int on = 1;
			setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
			set_sock_nonblock(*sock);
			if (bind(*sock, res->ai_addr, res->ai_addrlen) == 0) {
				memcpy(addr, res->ai_addr, res->ai_addrlen);
				*addrlen = res->ai_addrlen;
				ret = 0;
				goto OUT;
			} else {
				char str_addr[INET6_ADDRSTRLEN];
				my_inet_ntop(res->ai_family, res->ai_addr, str_addr, sizeof(str_addr));
				p_err("bind: %s:%s (%s)", hostname, service, str_addr);
			}
			close(*sock);
			*sock = -1;
		}
		res = res->ai_next;
	}
OUT:
	freeaddrinfo(ressave);

	return ret;
}

static int join_multicast_group(int sock, struct sockaddr_storage *addr) {
	switch (addr->ss_family) {
	case AF_INET: {
		struct ip_mreq mreq;
		mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
		mreq.imr_interface.s_addr = INADDR_ANY;

		if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
			p_err("setsockopt(IP_ADD_MEMBERSHIP)");
			return -1;
		}
		break;
	}

	case AF_INET6: {
		struct ipv6_mreq mreq6;
		memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
		mreq6.ipv6mr_interface = 0; // interface index, will be set later

		if (setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
			p_err("setsockopt(IPV6_ADD_MEMBERSHIP)");
			return -1;
		}
		break;
	}
	}

	return 0;
}

int udp_connect_input(struct io *io) {
	struct sockaddr_storage addr;
	int addrlen = sizeof(addr);
	int sock = -1;

	memset(&addr, 0, sizeof(addr));

	p_info("Connecting input to %s port %s\n", io->hostname, io->service);
	if (bind_addr(io->hostname, io->service, SOCK_DGRAM, &addr, &addrlen, &sock) < 0)
		return -1;

	/* Set receive buffer size to ~4.0MB */
	int bufsize = (4000000 / 1316) * 1316;
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void *)&bufsize, sizeof(bufsize));

	if (is_multicast(&addr)) {
		if (join_multicast_group(sock, &addr) < 0) {
			close(sock);
			return -1;
		}
	}

	io->fd = sock;
	p_info("Input connected to fd:%d\n", io->fd);

	return 1;
}
