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

char* copy_string(const char* str)
{
	return strcpy(malloc(strlen(str) + 1), str);
}

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
		HTTP_Message message;
		printf("Reading HTTP request line\n");
		read_request_line(&message, connection);
		printf("\tMethod: %d\n\tDomain: %s\n\tPath: %s\n\tVersion: %c\n",
				message.request.method,
				message.request.domain ? message.request.domain : "",
				message.request.path ? message.request.path : "",
				message.request.http_version);

		printf("Reading HTTP headers\n");
		read_headers(&message, connection);
		printf("Got %d headers\n", message.num_headers);
		for(int i = 0; i < message.num_headers; ++i)
			printf("\t%s: %s\n", message.headers[i].name, message.headers[i].value);

		printf("Reading HTTP body\n");
		read_body(&message, connection);
		if(message.body_length > 0)
			printf("Got body\n%.*s\n", (int)message.body_length, message.body);

		printf("Forwarding\n");

		printf("Connecting to remote\n");
		int client_sock = socket(PF_INET, SOCK_STREAM, 0);
		if(client_sock < 0)
		{
			perror("Failed to open socket\n");
			return 1;
		}

		struct addrinfo* result;
		getaddrinfo(message.request.domain, "http", 0, &result);
		if(connect(client_sock, result->ai_addr, sizeof(*result->ai_addr)) < 0)
		{
			perror("FUCK\n");
			exit(1);
		}

		FILE* server_connection = fdopen(client_sock, "r+");

		free(message.request.domain); message.request.domain = 0;

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
		freeaddrinfo(result);
	}

	close(server_sock);
	printf("deinit_http\n");
	deinit_http();
}
