#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <json.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include "menu/menu.h"
#include "libncread/ncread.h"

#include <signal.h>

const char *VERSION = "v1.0.0";

struct argst {
	void *data;
	void *addata;
	WINDOW* std;
};

struct ncreadst {
	char **buffer;
	int *chlim;
};

void debug(const char* str) {
	FILE* f = fopen("debug", "a");
	fwrite(str, 1, strlen(str), f);
	fclose(f);
}

int host_lan(WINDOW* win, int* wcaps, void* data) {
	struct argst *argdata = (struct argst*)data;
	int **_data = (int**)argdata->data;
	return 0;
}

void* hostth(void* args) {
	/* args: int sockfd*/
	struct argst *argdata = (struct argst*)args;
	int sockfd = *((int*)argdata->data);
	int *ptyfd = (int*)argdata->addata;
	int masterfd = ptyfd[0]; int slavefd = ptyfd[1];
	pid_t pid = fork();
	if (!pid) {
		close(0);close(1);close(2);
		dup2(slavefd,0);dup2(slavefd,1);dup2(slavefd,2);
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		setsid();
		ioctl(slavefd, TIOCSCTTY, 1);
		struct termios tty, old;
		tcgetattr(0, &old);
		tty = old;
		tty.c_cc[VTIME]=0;tty.c_cc[VMIN]=1;
		tty.c_lflag &= ~(ECHO | ICANON);
		tcsetattr(0, TCSADRAIN, &tty);
		execlp("bash", NULL);
		/* IF ERROR HERE, CANCEL EVERYTHING. */
	}
	fd_set readfd;
	FD_ZERO(&readfd); FD_SET(sockfd, &readfd); FD_SET(slavefd, &readfd);
	int maxfd = (sockfd>slavefd) ? sockfd : slavefd;
	while (1) {
		char *buff = calloc(1,1);
		pselect(maxfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (FD_ISSET(slavefd, &readfd)) {
			read(slavefd, buff, 1);
			write(sockfd, buff, 1);
			FD_SET(sockfd, &readfd);
		}
		if (FD_ISSET(sockfd, &readfd)) {
			read(sockfd, buff, 1);
			write(masterfd, buff, 1);
			FD_SET(slavefd, &readfd);
		}
		free(buff);
	}
}

int host(WINDOW* win, int* wcaps, void* data) {
	struct argst *argdata = (struct argst*)data;
	int **_data = (int**)argdata->data;
	if (_data[1][3]) {
		host_lan(win, wcaps, data);
		return 0;
	}
	/*Not in LAN. Contact server*/
	int y, x; getmaxyx(argdata->std, y, x);
	WINDOW* nwin = newwin(9, 40, y/2-4, x/2-20);
	wrefresh(argdata->std);
	int wy, wx = getmaxyx(nwin, wy, wx);
	wbkgd(nwin, COLOR_PAIR(2));
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = htons(4444);
	inet_pton(AF_INET, "127.0.0.1", &(address.sin_addr));
	socklen_t addrlen = sizeof(address);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	connect(sockfd, (struct sockaddr*)&address, addrlen);
	char* cmd = "{\"request\": \"start-session\"}";
	write(sockfd, cmd, strlen(cmd));
	fd_set readfd;
	FD_ZERO(&readfd); FD_SET(sockfd, &readfd);
	pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
	char buff[18];
	read(sockfd, buff, 18);
	struct json_object *jobj, *key;
	jobj = json_tokener_parse(buff);
	json_object_object_get_ex(jobj, "code", &key);
	const char *code = json_object_get_string(key);
	/*Notify that session started and show the code*/
	wattron(nwin, A_BOLD);
	mvwaddstr(nwin, 1, wx/2-4, "Host Mode");
	wattroff(nwin, A_BOLD);
	mvwaddstr(nwin, 3, 1, "A new session has started.");
	mvwaddstr(nwin, 5, 1, "The code is: ");
	waddstr(nwin, code);
	mvwaddstr(nwin, 7, 1, "Waiting for remote...");
	wrefresh(nwin);
	write(sockfd, "OK", 2);
	FD_ZERO(&readfd);FD_SET(sockfd, &readfd);
	pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL); /*Waiting until notified of guest connection.*/
	read(sockfd, NULL, 2); 
	write(sockfd, "OK", 2);
	/* Limpia la ventana, informa que la conexión se ha establecido, ofrece un botón para desconectar. */
	wmove(nwin,0,0);wclrtobot(nwin);
	wattron(nwin, A_BOLD);
	mvwaddstr(nwin, 3, wx/2-10, "Connection established");
	wattroff(nwin, A_BOLD);
	wattron(nwin, COLOR_PAIR(1));
	mvwaddstr(nwin, 7, wx/2-6, "[Disconnect]");
	wattroff(nwin, COLOR_PAIR(2));
	wrefresh(nwin);
	/* Launch a thread doing the actual task. If the button is pressed in main thread, disconnect and close socket and kill thread.*/
	/* The thread is going to handle the PTY. */
	int masterfd = open("/dev/ptmx", O_RDWR);
	grantpt(masterfd);
	unlockpt(masterfd);
	int slavefd = open(ptsname(masterfd), O_RDWR);
	int fdarr[2] = {masterfd, slavefd};
	argdata->data = &sockfd;
	argdata->addata = fdarr;
	pthread_t hth;
	pthread_create(&hth, NULL, hostth, argdata);
	wgetch(nwin);
	close(sockfd);
	return 0;
}

int guest(WINDOW* stdscr, WINDOW* win, char* code) {
	int y, x; getmaxyx(stdscr, y, x);
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = htons(4444);
	inet_pton(AF_INET, "127.0.0.1", &(address.sin_addr));
	socklen_t addrlen = sizeof(address);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	connect(sockfd, (struct sockaddr*)&address, addrlen);
	/* Send the request */
	char request[45];
	sprintf(request, "{\"request\": \"join-session\", \"code\": \"%s\"}", code);
	write(sockfd,request,45);
	char* resp = malloc(1);
	read(sockfd, resp, 1);
	if (*resp == '0') {
		/* You can print some error in red here */
		return 1;
	}
	/* Focus standard screen, that is were the action is going to take place. */
	return 1;
}

int tickf(WINDOW* win, int* mdata, void* data) {
	struct argst* argdata = (struct argst*)data;
	int **_data = (int**)argdata->data;
	if (_data[mdata[1]][2] == 1) {
		if (_data[mdata[1]][3] == 0) {
			_data[mdata[1]][3] = 1;
			mvwaddstr(win, mdata[0], 1, "x");
			wrefresh(win);
		} else {
			_data[mdata[1]][3] = 0;
			mvwaddstr(win, mdata[0], 1, " ");
			wrefresh(win);
		}
	}
	return 1;
}

void fillwspace(WINDOW* win, int **_data, int sp, char* bf) {
	for (int i=0; i<_data[sp][4]; i++) mvwaddch(win, _data[sp][0], _data[sp][3]+i, ' ');
	mvwaddstr(win, _data[sp][0], _data[sp][3], bf);
	wrefresh(win);
}

void wr_optselect(WINDOW* win, int* wcaps, struct optst opts, int mode, int* mdata, void* data, int* colors) {
	/* data va a contener un array de integers de 3x5.  ( y | x | objtype | args...)
	 * 							      1         1|0
	 * 							      2         x | fillsize */
	struct argst *argdata = (struct argst*)data;
	struct ncreadst *ncst = (struct ncreadst*)argdata->addata;
	int **_data = (int**)argdata->data;
	const char** keys = opts.opt;
	if (!mode) {
		for (int i=0; i<opts.size; i++) {
			char buff[20];
			sprintf(buff, "i is: %d\n", i);
			buff[0] = 0;
			sprintf(buff, "_data[i][2] is: %d\n", _data[i][2]);
			if (_data[i][2] == 1) {
				if (_data[i][3] == 1) mvwaddstr(win, _data[i][0], 0, "[x]");
				else mvwaddstr(win, _data[i][0], 0, "[ ]");
			}
			mvwaddstr(win, _data[i][0], _data[i][1], keys[i]);
			wrefresh(win);
		}
		if (_data[0][2] == 2) {
			mvwaddstr(win, _data[0][0], _data[0][1], keys[0]);
			wattron(win, COLOR_PAIR(colors[1]));
			fillwspace(win, _data, mdata[1], ncst->buffer[mdata[1]]);
			wattroff(win, COLOR_PAIR(colors[1]));
		} else {
			wattron(win, COLOR_PAIR(colors[1]));
			mvwaddstr(win, _data[0][0], _data[0][1], keys[0]);
			wattroff(win, COLOR_PAIR(colors[1]));
			wrefresh(win);
		}
		return;
	}
	mvwaddstr(win, _data[mdata[1]][0], _data[mdata[1]][1], keys[mdata[1]]);
	if (_data[mdata[1]][2] == 2) fillwspace(win, _data, mdata[1], ncst->buffer[mdata[1]]);
	if (mode == 1) {
		if (mdata[1]) { /*if sp != 0*/
			mdata[1]--; mdata[0] = _data[mdata[1]][0];
		}
	} else {
		if (mdata[1] != mdata[3]-1) {
			mdata[1]++; mdata[0] = _data[mdata[1]][0];
		}
	}
	if (_data[mdata[1]][2] == 2) {
		mvwaddstr(win, _data[mdata[1]][0], _data[mdata[1]][1], keys[mdata[1]]);
		wattron(win, COLOR_PAIR(colors[1]));
		fillwspace(win, _data, mdata[1], ncst->buffer[mdata[1]]);
		wattroff(win, COLOR_PAIR(colors[1]));
	} else {
		wattron(win, COLOR_PAIR(colors[1]));
		mvwaddstr(win, _data[mdata[1]][0], _data[mdata[1]][1], keys[mdata[1]]);
		wattroff(win, COLOR_PAIR(colors[1]));
		wrefresh(win);
	}
}

int exitf(WINDOW* win, int* wcaps, void* data) { endwin();exit(0); }

int hostsel(WINDOW* win, int* caps, void* data) {
	struct argst *argdata = (struct argst*)data;
	int y,x; getmaxyx(argdata->std, y, x);
	WINDOW* nwin = newwin(11, 50, (y/2)-5, (x/2)-25);
	int wy, wx; getmaxyx(nwin, wy, wx);
	wrefresh(argdata->std);
	wbkgd(nwin, COLOR_PAIR(2));
	keypad(nwin, 1);
	wattron(nwin, A_BOLD);
	mvwaddstr(nwin, 1, wx/2-5, "Select mode");
	mvwaddstr(nwin, 3, wx/2-3, "Host");
	wattroff(nwin, A_BOLD);
	wrefresh(nwin);
	int wcaps[4] = {5, 30, 5, 10};
	const char* tmpopts[3] = {"Run in background", "LAN only", "[OK]"};
	int (*func[3])(WINDOW*,int*,void*) = {tickf, tickf, host};
	struct optst opts;
	opts.opt = tmpopts;
	opts.func = func;
	opts.size=3;
	struct bindst bindings;bindings.size=0;
	int colors[2] = {2,1};
	int **_data = malloc(sizeof(int*)*3);
	for (int i=0; i<3; i++) _data[i] = malloc(sizeof(int)*5);
	_data[0][0]=0;_data[0][1]=4;_data[0][2]=1;_data[0][3]=0;_data[0][4]=0;
	_data[1][0]=1;_data[1][1]=4;_data[1][2]=1;_data[1][3]=0;_data[1][4]=0;
	_data[2][0]=3;_data[2][1]=12;_data[2][2]=0;_data[2][3]=0;_data[2][4]=0;
	argdata->data = _data;
	while (1) {
		if (!menu(nwin, wcaps, opts, wr_optselect, 1, bindings, 1, colors, (void*)argdata)) {
			break;
		}
	}
	delwin(nwin);
	return 1;
}

int callncread(WINDOW* win, int* mdata, void* data) {
	struct argst *argsdata = (struct argst*)data;
	int **_data = (int**)argsdata->data;
	struct ncreadst *ncst = (struct ncreadst*)argsdata->addata;
	char **buffer = (char**)ncst->buffer;
	int *chlim = (int*)ncst->chlim;
	ampsread(win, &(buffer[mdata[1]]), _data[mdata[1]][0], _data[mdata[1]][3], _data[mdata[1]][4], chlim[mdata[1]], 0);
	return 1;
}

int guestsel(WINDOW* win, int* caps, void* data) {
	struct argst *argdata = (struct argst*)data;
	int y,x; getmaxyx(stdscr, y, x);
	WINDOW* nwin = newwin(11, 50, (y/2)-5, (x/2)-25);
	int wy, wx; getmaxyx(nwin, wy, wx);
	wrefresh(stdscr);
	wbkgd(nwin, COLOR_PAIR(2));
	keypad(nwin, 1);
	wattron(nwin, A_BOLD);
	mvwaddstr(nwin, 1,(wx/2)-5, "Select mode");
	mvwaddstr(nwin, 3, wx/2-3, "Guest");
	wattroff(nwin, A_BOLD);
	mvwaddstr(nwin, 5, 10, "Code: ");
	wrefresh(nwin);
	char *code;
	while (1) {
		int res = ampsread(nwin, &code, 5, 16, 5, 5, 0);
		if (res) break;
		guest(argdata->std, nwin, code);
		touchwin(nwin);wmove(nwin,5,16);wclrtobot(nwin);
		wrefresh(nwin);
	}
	delwin(nwin);
	return 1;
}

int main() {
	WINDOW* stdscr = initscr();
	int y, x; getmaxyx(stdscr, y,x);
	start_color(); use_default_colors();
	curs_set(0);
	init_pair(1, 0, 7);
	init_pair(2, 7, 20);
	init_pair(3, 7, 26);
	WINDOW* introwin = newwin(12, 50, (y/2)-6, (x/2)-25);
	int wy, wx; getmaxyx(introwin, wy, wx);
	wrefresh(stdscr);
	wbkgd(introwin, COLOR_PAIR(2));
	mvwaddstr(introwin, 0, 0, "v1.0.0");
	mvwaddstr(introwin, 2, (wx/2)-9, "______ _____ _____");
	mvwaddstr(introwin, 3, (wx/2)-9, "| ___ \\_   _/  __ \\");
	mvwaddstr(introwin, 4, (wx/2)-9, "| |_/ / | | | /  \\/");
	mvwaddstr(introwin, 5, (wx/2)-9, "|    /  | | | |");
	mvwaddstr(introwin, 6, (wx/2)-9, "| |\\ \\  | | | \\__/\\");
	mvwaddstr(introwin, 7, (wx/2)-9, "\\_| \\_| \\_/  \\____/");
	mvwaddstr(introwin, 10, (wx/2)-9, "VENOM-InstantDeath");
	wrefresh(introwin);
	napms(1000);
	delwin(introwin);
	touchwin(stdscr);
	wrefresh(stdscr);
	WINDOW* selectwin = newwin(11, 50, (y/2)-5, (x/2)-25);
	wrefresh(stdscr);
	getmaxyx(selectwin, wy,wx);
	wbkgd(selectwin, COLOR_PAIR(2));
	keypad(selectwin, 1);
	wattron(selectwin, A_BOLD);
	mvwaddstr(selectwin, 1,(wx/2)-5, "Select mode");
	wattroff(selectwin, A_BOLD);
	wrefresh(selectwin);
	int wcaps[4] = {3, 20, 3, wx/2-3};
	struct optst opts;
	const char* tmpopts[3] = {"Host", "Guest", "Exit"};
	int (*tmpfunc[3])(WINDOW*, int*, void*) = {hostsel, guestsel, exitf};
	opts.opt = tmpopts;
	opts.func = tmpfunc;
	opts.size = 3;
	struct bindst bindings; bindings.size = 0;
	int colors[2] = {2, 1};
	struct argst *_data = malloc(sizeof(struct argst));
	_data->data = NULL;
	_data->std = stdscr;
	while (1) {
		if (menu(selectwin, wcaps, opts, defwrite, 1, bindings, 1, colors, (void*)_data)) {
			touchwin(selectwin);
			wmove(selectwin, 3, 0); wclrtobot(selectwin);
			wrefresh(selectwin);
			continue;
		} else break;
	}
	endwin();
	return 0;
}
