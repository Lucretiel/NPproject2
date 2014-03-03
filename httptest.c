/*
 * httptest.c
 *
 *  Created on: Feb 15, 2014
 *      Author: nathan
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>


#include "http.h"

int main(int argc, char **argv)
{
	printf("init_http\n");
	init_http();
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);

	if(server_sock < 0)
	{
		perror("socket");
		return 1;
	}

	struct sockaddr_in server;
	server.sin_family = PF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(8080);

	if(bind(server_sock, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		perror("bind");
		return 1;
	}

	listen(server_sock, 5);

	printf("Listening on socket\n");

	while(1)
	{

		struct sockaddr_in client;
		unsigned len = sizeof(client);
		printf("Awaiting Connection\n");
		int client_fd = accept(server_sock, (struct sockaddr*)&client, &len);
		printf("Accepted connection\n");

		HTTP_Message message = empty_message;

		printf("Reading HTTP request line\n");
		if(read_request_line(&message, client_fd))
		{
			printf("Error reading HTTP request\n");
			clear_request(&message);
			close(client_fd);
			continue;
		}

		printf("Reading HTTP headers\n");
		if(read_headers(&message, client_fd))
		{
			printf("Error reading HTTP headers\n");
			clear_request(&message);
			close(client_fd);
			continue;
		}

		printf("Reading HTTP body\n");
		if(read_body(&message, client_fd))
		{
			printf("Error reading HTTP body\n");
			clear_request(&message);
			close(client_fd);
			continue;
		}

		printf("Forwarding\n");

		printf("Opening socket\n");
		int server_socket = socket(PF_INET, SOCK_STREAM, 0);
		if(server_socket < 0)
		{
			perror("Failed to open socket\n");
			return 1;
		}

		printf("Looking up host\n");
		struct addrinfo* host_info;
		getaddrinfo(es_cstrc(&message.request.domain), "http", 0, &host_info);

		printf("Connecting to host\n");
		if(connect(server_socket, host_info->ai_addr, sizeof(*host_info->ai_addr)) < 0)
		{
			printf("Failed to connect to host\n");
			exit(1);
		}
		freeaddrinfo(host_info);

		//int server_socket = 1;
		es_clear(&message.request.domain);

		printf("Writing request\n");
		write_request(&message, server_socket);

		printf("Clearing request\n");
		clear_request(&message);

		printf("Reading response\n");
		read_response_line(&message, server_socket);
		read_headers(&message, server_socket);
		read_body(&message, server_socket);
		//printf("SIKE\n");

		printf("Writing response\n");
		write_response(&message, client_fd);
		//printf("NOPE\n");

		printf("Clearing response\n\n");
		clear_response(&message);
		close(server_socket);
		close(client_fd);
	}

	close(server_sock);
	printf("deinit_http\n");
	deinit_http();
}
