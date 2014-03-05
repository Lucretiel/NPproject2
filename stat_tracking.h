/*
 * stat_tracking.h
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#pragma once

#include "EasyString/easy_string.h"

void stat_add_success();
void stat_add_filtered();
void stat_add_error();
void stat_filter(StringRef filter);

void print_stats();
