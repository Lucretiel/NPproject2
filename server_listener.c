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
#include <stdlib.h>

#include "config.h"
#include "server_listener.h"
#include "http_manager_thread.h"
#include "print_thread.h"
#include "stat_tracking.h"

typedef struct sockaddr_in SockAddrIn;


static void print_signal(int sig)
{
	print_stats();
}

static void quit_signal(int sig)
{
	exit(0); //Registered cleanup handlers will ensure clean shutdown
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

	signal(SIGUSR1, &print_signal);
	signal(SIGUSR2, &quit_signal);
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

	while(1)
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

	return 0;
}
