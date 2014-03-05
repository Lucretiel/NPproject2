/*
 * http_manager_thread.h
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 *   The central manager subsystem. Responsible for launching and cleaning up
 *   worker threads. It's probably a good idea to launch the print system first
 */

#pragma once

#include <netinet/in.h>

int manager_status();
int handle_connection(int connection_fd, struct sockaddr_in* connection_addr);
