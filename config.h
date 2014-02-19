/*
 * config.h
 *
 *  Created on: Feb 19, 2014
 *      Author: nathan
 *
 *  Compile-time constants used throughout the program. All are set here.
 */

#pragma once

const static size_t autobuf_initial_size = 256;

const static size_t max_message_line_size = 1024 * 1024;
const static size_t max_header_size = 1024 * 1024;
const static size_t max_body_size = 1024 * 1024 * 1024;

const static int max_headers = 1024;

//If true, each request and response will be flushed after being written
const static int flush_http_messages = 1;
