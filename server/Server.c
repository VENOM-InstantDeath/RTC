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
#include <sys/prctl.h>
#include "linked/linked.h"
#include "vector/vector.h"

struct argst {
	char* strs;
	int sockfd;
};

struct Commvar {
	struct vector addr;
	struct vector code;
	struct ivector fd;
};

struct argsc {
	struct Commvar *cmvr;
	int sockfd;
};

int randint(int a, int b) {
	srand(time(NULL));
	return (rand()%(b-a+1)+a);
}

void *communicator(void* vargs) {
	puts("Communicator instance started");
	struct argsc *args = (struct argsc*)vargs;
	int sockfd = args->sockfd;
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(sockfd, &readfd);
	while (1) {
		char *buffer = calloc(50,1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			read(sockfd, buffer, 50);
			if (!strlen(buffer)) {free(buffer);break;}
			printf("From communicator. Data is %s\n", buffer);
			struct vector vec = string_split(buffer, '|');
			if (!strcmp(vec.str[1], "start-session")) {
				puts("Communicator start-session.");
				int code = randint(11111, 99999);
				char *codebuff = calloc(6,1);
				sprintf(codebuff,"%d",code);
				int index = args->cmvr->addr.size;  // no es necesario aún, pero queda ahí por las dudas
				vector_add(&(args->cmvr->addr), vec.str[0]);
				vector_add(&(args->cmvr->code), codebuff);
				ivector_add(&(args->cmvr->fd), sockfd);
				printf("Communicator generated the code: %s\n", codebuff);
				write(sockfd, codebuff, 6);
			}
			if (!strcmp(vec.str[1], "join-session")) {
				if (vec.size < 3) {
					write(sockfd, "0", 1);
					continue;
				}
				char* code = vec.str[2];
				int ctrl = 0;
				for (int i=0;i<args->cmvr->addr.size;i++) {
					if (!strcmp(vec.str[2], args->cmvr->code.str[i])) {
						int fd = args->cmvr->fd.num[i];
						printf("From communicator. Code is correct, fd is %d\n", fd);
						char* thisfd = malloc(4);
						sprintf(thisfd, "%d", sockfd);
						write(fd,thisfd,2); // Para el host
						sprintf(thisfd, "%d", fd);
						write(sockfd,thisfd,1); // Para el guest
						free(thisfd);
						ctrl = 1;
					}
				}
				if (!ctrl) write(sockfd, "0", 1);
			}
			if (!strcmp(vec.str[1], "ping-pong")) {
				puts("From communicator. ping-pong mode enabled");
			}
		}
		free(buffer);
	}
}

void* hostth(void *arg) {
	int *fd = (int*)arg;
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(fd[1], &readfd);
	while (1) {
		char *buffer = calloc(10,1);
		int selerr = pselect(fd[1]+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			puts("From host's thread. Data available!");
		}
	}
}

void hostfn(int sockfd, int uxsfd) {
	puts("hostfn instance started");
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(uxsfd, &readfd);
	char *buffer = malloc(5);
	int opfd;
	int selerr = pselect(uxsfd+1, &readfd, NULL, NULL, NULL, NULL);
	if (selerr) {
		puts("From hostfn (stage 1). Data available!");
		read(uxsfd, buffer, 5);  /*Debería recibir un comando. Aún no, primero el OK*/
		opfd = atoi(buffer)
		free(buffer);
		puts("From hostfn. Received guestfd from communicator. Continuing to stage 2..");
		break;
	}
	pthread_t th;
	int fdarr[2] = {sockfd, uxsfd};
	pthread_create(&th, NULL, hostth, fdarr);
	free(buffer);
	FD_ZERO(&readfd);FD_SET(sockfd, &readfd);
	while (1) {
		char *buffer = calloc(10,1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			puts("From hostfn (stage 2). Data available!");
			read(sockfd, buffer, 10);
			/*After reading from host's socket, then you write to uxsfd*/
		}
		free(buffer);
	}
}

void* guestth(void *arg) {
	int *fd = (int*)arg;
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(fd[1], &readfd);
	while (1) {
		char *buffer = calloc(10,1);
		int selerr = pselect(fd[1]+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			puts("From guest's thread. Data available!");
		}
	}
}

void guestfn(int sockfd, int uxsfd, int opfd) {
	puts("guestfn instance started");
	pthread_t th;
	int fdarr[2] = {sockfd, uxsfd};
	pthread_create(&th, NULL, hostth, fdarr);
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(sockfd, &readfd);
	while (1) {
		char *buffer = calloc(10,1);
		int selerr = pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (selerr) {
			puts("From guestfn. Data available!");
			read(sockfd, buffer, 10);
			/*After reading from guest's socket, then you write to uxsfd*/
		}
	}
}

void *threadfm(void *arg) {
	struct argst *args = (struct argst*)arg;
	const char* address = args->strs;
	int sockfd = args->sockfd;
	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "sock");
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
			//uxs: ip|request|arguments   ip|start-session ; ip|join-session|code
			if (!strcmp(req, "start-session")) {
				char* resp = malloc(6);
				int res = 0;
				/*send request*/
				char* temp = calloc(30,1);
				sprintf(temp, "%s|start-session", address);
				printf("From threadfm. About to send this: %s\n", temp);
				write(uxsfd, temp, 30);
				free(temp);
				/*receive request*/
				read(uxsfd, resp, 6);
				char* msg = malloc(16);
				sprintf(msg, "{\"code\": %s}", resp);
				printf("From threadfm. Received this from communicator: %s\n", resp);
				write(sockfd, msg, 15);
				free(resp);free(msg);
				hostfn(sockfd, uxsfd);
			}
			else if (!strcmp(req, "join-session")) {
				struct json_object *jcode;
				json_object_object_get_ex(jobj, "code", &jcode);
				const char* code = json_object_get_string(jcode);
				char* temp = calloc(38, 1);
				sprintf(temp, "%s|join-session|%s", address, code);
				write(uxsfd, temp, 38);
				free(temp);
				char* resp = malloc(5);
				read(uxsfd, resp, 5);
				int opfd = atoi(resp)
				if (opfd == 0) {
					write(sockfd, "0", 1);
				} else {
					write(sockfd, "1", 1);
					guestfn(sockfd, uxsfd, opfd);
				}
				free(resp);
			}
		}
		free(buffer);
	}
	free(args->strs);
	free(args);
	puts("Connection closed");
}

void cleanup() {unlink("sock");exit(0);}

int main() {
	atexit(cleanup);
	signal(SIGINT, cleanup);
	pid_t pid = fork();
	if (!pid) {
		char BUFF[BUFSIZ];
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		setvbuf(stdout, BUFF, _IOLBF, BUFSIZ);
		puts("Child process started");
		struct Commvar commvar;
		vector_init(&(commvar.addr));
		vector_init(&(commvar.code));
		ivector_init(&(commvar.fd));
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, "sock");
		int size = sizeof(addr);
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd == -1) puts("Socket creation error");
		if (bind(fd, (struct sockaddr*)&addr, (socklen_t)size) == -1) puts("Bind error");
		if (listen(fd, 1) == -1) puts("Listen error");
		while (1) {
			struct argsc *args = malloc(sizeof(struct argsc));
			int confd = accept(fd, (struct sockaddr*)&addr, (socklen_t*)&size);
			puts("From child. Connection accepted");
			args->cmvr = &commvar;
			args->sockfd = confd;
			pthread_t T;
			pthread_create(&T, NULL, communicator, args);
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
	while (1) {
		int confd = accept(sockfd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
		char* buff = malloc(20);
		printf("New connection from: %s\n", inet_ntop(AF_INET, &(address.sin_addr), buff, INET_ADDRSTRLEN));
		pthread_t T;
		struct argst *args = malloc(sizeof(struct argst));
		args->strs = buff;
		args->sockfd = confd;
		pthread_create(&T, NULL, threadfm, args);
		puts("Connection delivered to a thread");
	}
	return 0;
}
