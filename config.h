/*
 * config.h
 *
 *  Created on: Feb 19, 2014
 *      Author: nathan
 *
 *  Compile-time constants used throughout the program. All are set here.
 */

#pragma once

//Max length of a request/response line
const static unsigned long MAX_MSG_LINE_SIZE = 1024;

//Max size of any header line
const static unsigned long MAX_HEADER_LINE_SIZE = 1024;

//This is the max combined size of ALL headers
const static unsigned long MAX_HEADER_SIZE = 1024 * 1024;

//Max supported body size
const static unsigned long MAX_BODY_SIZE = 1024 * 1024 * 1024;

//Max size of a chunked encoding line
const static unsigned long MAX_CHUNK_HEADER_SIZE = 1024;

//Max size of a chunked encoding chunk
const static unsigned long MAX_CHUNK_SIZE = 1024 * 1024;

//Max number of headers
const static int MAX_NUM_HEADERS = 1024;

//If true, debug prints will be sent
const static int DEBUG_PRINT = 1;

//If true, thread IDs will be added to print output
const static int PRINT_TID = 1;
