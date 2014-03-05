/*
 * filters.h
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#pragma once

#include <stdbool.h>

#include "EasyString/easy_string.h"

void filter_add(String filter);
bool filter_match_any(StringRef domain);
