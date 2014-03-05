/*
 * server_listener.c
 *
 *  Created on: Mar 4, 2014
 *      Author: nathan
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "config.h"
#include "server_listener.h"
#include "http_manager_thread.h"
#include "print_thread.h"
#include "stat_tracking.h"

typedef struct sockaddr_in SockAddrIn;

sig_atomic_t shutdown_requested;

static void signal_handler(int sig)
{
	switch(sig)
	{
	case SIGUSR1:
		print_stats();
		break;
	case SIGUSR2:
		shutdown_requested = 1;
		break;
	}
}

int serve_forever(uint16_t port)
{
	if(manager_status() != 0)
	{
		if(DEBUG_PRINT) puts("Manager thread failed to start");
		return 1;
	}

	if(print_thread_status() != 0)
	{
		if(DEBUG_PRINT) puts("Print thread failed to start");
		return 1;
	}

	submit_debug_c("Core server beginning");

	submit_debug_c("Installing signal handlers");
	shutdown_requested = 0;

	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	signal(SIGINT, SIG_IGN);

	#define PRINT_AND_ERROR(MESSAGE) \
		{ submit_debug(es_copy(es_temp(MESSAGE))); return 1; }

	submit_debug_c("Opening socket");
	//Open socket
	int listener_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(listener_socket < 0)
		PRINT_AND_ERROR("Error creating listener socket")

	//Set SO_REUSEADDR so we can immediately relaunch on a crash
	#ifdef DEBUG
	{
		int sockopt = 1;
		setsockopt(listener_socket,
			SOL_SOCKET,
			SO_REUSEADDR,
			&sockopt,
			sizeof(sockopt));
	}
	#endif

	submit_debug_c("Preparing listen address");
	//Prepare port
	SockAddrIn listener_addr;
	listener_addr.sin_family = PF_INET;
	listener_addr.sin_addr.s_addr = INADDR_ANY;
	listener_addr.sin_port = htons(port);

	submit_debug_c("Binding to listen address");
	//Bind
	if(bind(listener_socket,
			(struct sockaddr*)&listener_addr,
			sizeof(listener_addr)) < 0)
	{
		close(listener_socket);
		PRINT_AND_ERROR("Error binding socket to port")
	}

	submit_debug_c("Listening");
	//Listen
	listen(listener_socket, 8);

	while(!shutdown_requested)
	{
		struct sockaddr_in client_addr;
		socklen_t len = sizeof(client_addr);
		int client_fd = accept(
				listener_socket,
				(struct sockaddr*)(&client_addr),
				&len);
		if(client_fd < 0)
		{
			break;
		}
		else
		{
			handle_connection(client_fd, &client_addr);
		}
	}

	submit_debug_c("Cleanly exiting server");

	return 0;
}
