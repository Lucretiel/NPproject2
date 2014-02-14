/*
 * autobuf.h
 *
 *  Created on: Feb 14, 2014
 *      Author: nathan
 */

#pragma once

//Read into an allocated, automatically sized buffer.

typedef struct
{
	char* storage_begin;
	char* storage_end;
	char* read_point;
} AutoBuffer;

/*
 * Not much interface needed. Initialize to init_autobuf when creating fresh,
 * then pass into readline. Free storage_begin when done.
 */
static const AutoBuffer init_autobuf;

extern const int READ_LINE_SUCESS;
extern const int READ_LINE_EOF;
extern const int READ_LINE_ERROR;
/*
 * Read a line up to CR LF, reallocating as nessesary.
 */
int read_line(AutoBuffer* buffer, FILE* connection);
