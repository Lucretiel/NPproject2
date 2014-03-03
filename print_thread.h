/*
 * print_thread.h
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 *
 *  ALL HAIL THE GLOBAL PRINT THREAD
 */

#pragma once

int begin_print_thread();
void end_print_thread();

void submit_print(char* message);
void submit_print_mve(char* message);
void submit_debug(char* message);
void submit_debug_mve(char* message);
