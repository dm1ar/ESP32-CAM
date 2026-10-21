/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_LWIP_SOCKETS_H
#define TEST_LWIP_SOCKETS_H
#include <arpa/inet.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
ssize_t test_sendto(int socket, const void* data, std::size_t length, int flags,
                    const sockaddr* destination, socklen_t size);
#define sendto test_sendto
#endif
