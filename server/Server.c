#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <json.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "vector.h"

struct argst {
	char* strs[1];
	int sockfd;
};

struct commvar {
	/* Aquí planeo poner un array de char y uno de int del mismo tamaño y relacionados*/
};

int randint(int a, int b) {
	srand(time(NULL));
	return (rand()%(b-a+1)+a);
}

void *communicator(int sockfd) {
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(sockfd, &readfd);
	while (1) {
		char buffer = calloc(50,1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			read(sockfd, buffer, 50);
			if (!strlen(buffer)) {free(buffer);break;}
			if (!strcmp(buffer, "start-session")) {
				int code = randint(11111, 99999);
				char codebuff* = malloc(6);
				sprintf(codebuff,"%d",code);
				write(sockfd, codebuff, 6);
				free(codebuff);
			}
		}
		free(buffer);
	}
}

void hostfd(int sockfd, int uxsfd) {
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(sockfd, &readfd);
	while (1) {
		char *buffer = calloc(20,1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			read(sockfd, buffer, 20);  /*Debería recibir un comando*/
			/*debe procesarlo*/
			/*debe devolver el output*/
		}
		free(buffer);
	}
}

void *threadfm(void *arg) {
	struct argst *args = (struct argst*)arg;
	const char* address = *(args->strs);
	int sockfd = args->sockfd;
	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "serv.sock");
	int addrlen = sizeof(addr);
	int uxsfd = socket(AF_UNIX, SOCK_STREAM, 0);
	connect(uxsfd, (struct sockaddr*)&addr, (socklen_t)addrlen);
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(sockfd, &readfd);
	while (1) {
		// {"request": "start-session"} -> {"code": 11111-99999}
		// {"request": "join-session", "code": "your-code"}
		char* buffer = calloc(50, 1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			printf("Data available on %s\n", address);
			read(sockfd, buffer, 50);
			if (!strlen(buffer)) {free(buffer);break;}
			struct json_object *jobj, *key;
			jobj = json_tokener_parse(buffer);
			puts(json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY));
			json_object_object_get_ex(jobj, "request", &key);
			const char* req = json_object_get_string(key);
			//uxs start-session join-session|code
			if (!strcmp(req, "start-session")) {
				char* resp = malloc(6);
				int res = 0;
				write(sockfd, "start-session", 13);
				read(sockfd, resp, 6);
				char* msg = malloc(16);
				sprintf(msg, "{\"code\": %s}", resp);
				write(sockfd, msg, 15);
				free(resp);free(msg);
				host(sockfd, uxsfd);
			}
			else if (!strcmp(req, "join-session")); //check if code key exist
		}
		free(buffer);
	}
	free(args->strs[0]);
	free(args);
	puts("Connection closed");
}

void* cleanup() {unlink("serv.sock");}

int main() {
	pid_t pid = fork();
	if (!pid) {
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strcmp(addr.sun_path, "serv.sock");
		int addrlen = sizeof(addr);
		socket(AF_UNIX, SOCK_STREAM, 0);
		bind(sockfd, (struct sockaddr*)&addr, (socklen_t)addrlen);
		listen(sockfd, 5);
		while (1) {
			int confd = accept(sockfd, (struct sockaddr*)&addr, (socklen_t*)&addrlen);
			pthread_t T;
			pthread_create(&T, NULL, communicator, confd);
		}
		exit(0);
	}
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
	atexit(cleanup);
	while (1) {
		int confd = accept(sockfd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
		char* buff = malloc(20);
		printf("New connection from: %s\n", inet_ntop(AF_INET, &(address.sin_addr), buff, INET_ADDRSTRLEN));
		pthread_t T;
		struct argst *args = malloc(sizeof(args));
		args->strs[0] = buff;
		args->sockfd = confd;
		pthread_create(&T, NULL, threadfm, args);
		puts("Connection delivered to a thread");
	}
	return 0;
}
