/*
 * http_worker_thread.h
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#pragma once

#include <sys/types.h>
#include <netinet/in.h>

typedef struct
{
	int connection_fd;
	struct sockaddr_in connection_sockaddr;
} HTTP_Data;

//Send in a pointer to a malloc'd HTTP_Data
void* http_worker_thread(void* ptr);
