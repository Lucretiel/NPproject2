/*
 * http_manager_thread.h
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 *   The central manager subsystem. Responsible for launching and cleaning up
 *   worker threads. It's probably a good idea to launch the print system first
 */

#pragma once

void launch_http_manager();
void stop_http_manager();

void handle_connection(int fd);
