/*
 * config.h
 *
 *  Created on: Feb 19, 2014
 *      Author: nathan
 *
 *  Compile-time constants used throughout the program. All are set here.
 */

#pragma once

const static int MAX_MSG_LINE_SIZE = 1024 * 1024;
const static int MAX_HEADER_SIZE = 1024 * 1024;
const static int MAX_BODY_SIZE = 1024 * 1024 * 1024;

const static int MAX_NUM_HEADERS = 1024;

//If true, each request and response will be flushed after being written
const static int FLUSH_HTTP_MSGS = 1;
