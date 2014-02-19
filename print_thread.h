/*
 * print_thread.h
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#pragma once

int begin_print_thread();
void end_print_thread();
void submit_print(const char* message);
