/*
 * httptest.c
 *
 *  Created on: Feb 15, 2014
 *      Author: nathan
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "http.h"

char* copy_string(const char* str)
{
	return strcpy(malloc(strlen(str) + 1), str);
}

int main(int argc, char **argv)
{
	init_http();
	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if(sock < 0)
	{
		perror("socket");
		return 1;
	}

	struct sockaddr_in server;
	server.sin_family = PF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(8080);

	if(bind(sock, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		perror("bind");
		return 1;
	}

	listen(sock, 5);

	printf("Listening on socket\n");

	while(1)
	{
		struct sockaddr_in client;
		unsigned len = sizeof(client);
		printf("Awaiting Connection\n");
		int connection_fd = accept(sock, (struct sockaddr*)&client, &len);
		printf("Accepted connection\n");

		FILE* connection = fdopen(connection_fd, "r+");
		HTTP_Message message;
		printf("Reading HTTP request line\n");
		read_request_line(&message, connection);
		printf("\tMethod: %d\n\tDomain: %s\n\tPath: %s\n\tVersion: %c\n",
				message.request.method,
				message.request.domain,
				message.request.path,
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

		printf("Clearing HTTP request\n");
		clear_request(&message);

		printf("Creating Response\n");
		message.response.http_version = '1';
		message.response.status = 200;
		message.response.phrase = "OK";

		message.body = "Hello World!\n";
		message.body_length = strlen(message.body);

		HTTP_Header headers[2];
		char content_length_buffer[80];
		headers[0].name = "Content-Type";
		headers[0].value = "text/plain";
		headers[1].name = "Content-Length";
		headers[1].value = content_length_buffer;
		sprintf(content_length_buffer, "%lu", message.body_length);

		message.headers = headers;

		printf("Writing Response\n");
		write_response(&message, connection);

		printf("Closing Socket\n");
		fclose(connection);
	}
}
