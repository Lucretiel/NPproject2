/*
 * print_thread.h
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 *
 *  ALL HAIL THE GLOBAL PRINT THREAD
 */

#pragma once

#include <string.h>
#include "EasyString/easy_string.h"

int begin_print_thread();
void end_print_thread();

void submit_print(String message);
void submit_debug(String message);
