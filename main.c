/*
 * main.c
 *
 *  Created on: Mar 4, 2014
 *      Author: nathan
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "filters.h"
#include "server_listener.h"
#include "stat_tracking.h"

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		puts("BETTER ARGS PLEASE");
		return 1;
	}

	long port_l = strtol(argv[1], 0, 10);

	if(port_l > USHRT_MAX)
	{
		puts("BETTER PORT PLEASE");
		return 1;
	}

	for(unsigned i = 2; i < argc; ++i)
	{
		filter_add(es_copy(es_temp(argv[i])));
		stat_filter(es_temp(argv[i]));
	}

	return serve_forever(port_l);
}

