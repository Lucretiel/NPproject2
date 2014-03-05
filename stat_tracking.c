/*
 * stat_tracking.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "stat_tracking.h"
#include "print_thread.h"

typedef struct
{
	String filters;
	unsigned num_successful;
	unsigned num_filtered;
	unsigned num_errors;
} Stats;

static Stats stats;

static pthread_mutex_t stat_mutex;

#define DO_WITH_LOCK(THING) \
	pthread_mutex_lock(&stat_mutex); \
	THING \
	pthread_mutex_unlock(&stat_mutex);

__attribute__((constructor))
void init_stat_tracking()
{
	if(DEBUG_PRINT) puts("Initializing stat tracking");
	memset(&stats, 0, sizeof(stats));
	pthread_mutex_init(&stat_mutex, 0);
}

__attribute__((destructor))
void deinit_stat_tracking()
{
	if(DEBUG_PRINT) puts("Deinitializng stat tracking");
	pthread_mutex_destroy(&stat_mutex);
	es_free(&stats.filters);
}

void stat_add_success()
{
	DO_WITH_LOCK(++stats.num_successful;)
}

void stat_add_filtered()
{
	DO_WITH_LOCK(++stats.num_filtered;)
}

void stat_add_error()
{
	DO_WITH_LOCK(++stats.num_errors;)
}

void stat_filter(StringRef filter)
{
	DO_WITH_LOCK(
		es_append(&stats.filters, filter);
		es_append(&stats.filters, es_temp("; "));)
}

void print_stats()
{
	Stats stats_copy;

	DO_WITH_LOCK(stats_copy = stats;)

	String output = es_printf(
		"Received SIGUSR1...reporting status:\n"
		"-- Processed %u requests successfully\n"
		"-- Filtering: %.*s\n"
		"-- Filtered %u requests\n"
		"-- Encountered %u requests in error",

		stats_copy.num_successful,
		ES_STRINGPRINT(&stats_copy.filters),
		stats_copy.num_filtered,
		stats_copy.num_errors);

	submit_print(output);
}
