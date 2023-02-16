#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>

struct argst {
	char* strs[1];
	int sockfd;
};

void *threadfunc(void *arg) {
	struct argst *args = (struct argst*)arg;
	const char* address = *(args->strs);
	int sockfd = args->sockfd;
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(sockfd, &readfd);
	while (1) {
		// {"request": "start-session"}
		// {"request": "join-session", "code": "your-code"}
		// {""}
		char* buffer = calloc(20, 1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			printf("Data available on %s\n", address);
			read(sockfd, buffer, 20);
			if (!strlen(buffer)) break;
			struct json_object *jobj, *key;
			jobj = json_tokener_parse(buffer);
			puts(json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY));
			json_object_object_get_ex(jobj, "request", &key);
			const char* req = json_object_get_string(key);
			if (!strcmp(req, "start-session"));
		}
		free(buffer);
	}
	free(args->strs[0]);
	free(args);
	puts("Connection closed");
}

int main() {
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = htons(4444);
	address.sin_addr.s_addr = INADDR_ANY;
	int addrlen = sizeof(address);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));
	bind(sockfd, (struct sockaddr*)&address, (socklen_t)addrlen);
	listen(sockfd, 5);
	while (1) {
		int confd = accept(sockfd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
		char* buff = malloc(20);
		printf("New connection from: %s\n", inet_ntop(AF_INET, &(address.sin_addr), buff, INET_ADDRSTRLEN));
		pthread_t T;
		struct argst *args = malloc(sizeof(args));
		args->strs[0] = buff;
		args->sockfd = confd;
		pthread_create(&T, NULL, threadfunc, args);
		puts("Connection delivered to a thread");
	}
	return 0;
}
