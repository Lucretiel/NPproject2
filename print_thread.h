/*
 * print_thread.h
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 *
 *  ALL HAIL THE GLOBAL PRINT THREAD
 */

#pragma once

#include "EasyString/easy_string.h"

void submit_print(String message);
void submit_debug(String message);

static inline void submit_print_c(const char* cstr)
{ submit_print(es_copy(es_temp(cstr))); }

static inline void submit_debug_c(const char* cstr)
{ submit_debug(es_copy(es_temp(cstr))); }

int print_thread_status();
