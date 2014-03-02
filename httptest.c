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
		int connection_fd = accept(server_sock, (struct sockaddr*)&client, &len);
		printf("Accepted connection\n");

		FILE* connection = fdopen(connection_fd, "r+");
		HTTP_Message message = empty_message;

		printf("Reading HTTP request line\n");
		if(read_request_line(&message, connection))
		{
			printf("Error reading HTTP request\n");
			clear_request(&message);
			fclose(connection);
			continue;
		}

		printf("Reading HTTP headers\n");
		if(read_headers(&message, connection))
		{
			printf("Error reading HTTP headers\n");
			clear_request(&message);
			fclose(connection);
			continue;
		}

		printf("Reading HTTP body\n");
		if(read_body(&message, connection))
		{
			printf("Error reading HTTP body\n");
			clear_request(&message);
			fclose(connection);
			continue;
		}

		printf("Forwarding\n");

		printf("Opening socket\n");
		int client_sock = socket(PF_INET, SOCK_STREAM, 0);
		if(client_sock < 0)
		{
			perror("Failed to open socket\n");
			return 1;
		}

		printf("Looking up host\n");
		struct addrinfo* host_info;
		getaddrinfo(es_cstrc(&message.request.domain), "http", 0, &host_info);

		printf("Connecting to host\n");
		if(connect(client_sock, host_info->ai_addr, sizeof(*host_info->ai_addr)) < 0)
		{
			printf("Failed to connect to host\n");
			exit(1);
		}
		freeaddrinfo(host_info);

		FILE* server_connection = fdopen(client_sock, "r+");

		es_clear(&message.request.domain);

		printf("Writing request\n");
		write_request(&message, server_connection);

		printf("Clearing request\n");
		clear_request(&message);

		printf("Reading response\n");
		read_response_line(&message, server_connection);
		read_headers(&message, server_connection);
		read_body(&message, server_connection);

		printf("Writing response\n");
		write_response(&message, connection);

		printf("Clearing response\n");
		clear_response(&message);
		fclose(server_connection);
		fclose(connection);
	}

	close(server_sock);
	printf("deinit_http\n");
	deinit_http();
}
