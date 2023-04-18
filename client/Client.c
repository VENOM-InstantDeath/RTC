#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <json.h>
#include <wchar.h>
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
int SOCKFD, PID, MASTERFD, SLAVEFD;
struct termios tty, old;
struct winsize win;

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

void parse_size(char* chws, int *winsz) {
	int c1 = 0, c2 = 0;
	char *c1s = calloc(4,1);
	for (int i=0; i<strlen(chws); i++) {
		if (chws[i] == ';') {
			winsz[c1] = atoi(c1s);
			c1s = calloc(4,1);
			c1++; c2 = 0;
			continue;
		}
		c1s[c2] = chws[i]; c2++;
	}
	winsz[c1] = atoi(c1s);
	win.ws_row = winsz[0]; win.ws_col = winsz[1];
	/*printf("winsz %d;%d\n", win.ws_row, win.ws_col);*/
}

void cexit(int sig) {/*Received SIGCHLD*/ exit(0);}

void swinch(int sig) { /*Received SIGWINCH*/
	struct winsize wins;
	ioctl(1, TIOCGWINSZ, &wins);
	char ws[9] = {0};
	sprintf(ws, "%d;%d", wins.ws_row, wins.ws_col);
	int k[1] = {9};
	write(SOCKFD, k, 1);
	write(SOCKFD, ws, 9);
	fd_set readfd;
	FD_ZERO(&readfd); FD_SET(SOCKFD, &readfd);
	pselect(SOCKFD+1, &readfd, NULL, NULL, NULL, NULL);
	read(SOCKFD, ws, 2);
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
	MASTERFD = open("/dev/ptmx", O_RDWR);
	grantpt(MASTERFD); unlockpt(MASTERFD);
	SLAVEFD = open(ptsname(MASTERFD), O_RDWR);
	fd_set readfd;
	FD_ZERO(&readfd); FD_SET(sockfd, &readfd);
	pselect(sockfd+1, &readfd, NULL, NULL, NULL, NULL);
	int winsz[2];
	char chws[9] = {0};
	read(sockfd, chws, 9);
	write(sockfd, "OK", 2);
	parse_size(chws, winsz);
	pid_t pid = fork();
	if (!pid) {
		close(0);close(1);close(2);
		dup2(SLAVEFD, 0);
		dup2(SLAVEFD, 1);
		dup2(SLAVEFD, 2);
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		ioctl(1, TIOCSWINSZ, &win);
		setsid();
		ioctl(SLAVEFD, TIOCSCTTY, 1);
		struct termios tty;
		tcgetattr(0, &tty);
		tty.c_lflag &= ~(ICANON);
		tty.c_cc[VTIME] = 0; tty.c_cc[VMIN] = 1;
		tcsetattr(0, TCSADRAIN, &tty);
		execl("/usr/bin/bash", "bash", "-i", NULL);
		/*puts("Error in forked process.");*/
		exit(1);
	}
	FD_ZERO(&readfd); FD_SET(MASTERFD, &readfd); FD_SET(sockfd, &readfd);
	signal(SIGCHLD, cexit);
	int maxfd = (sockfd > MASTERFD) ? sockfd : MASTERFD;
	while (1) {
		int res = pselect(maxfd+1, &readfd, NULL, NULL, NULL, NULL);
		if (FD_ISSET(MASTERFD, &readfd)) {
			int c[1];
			read(MASTERFD, c, 1);
			c[0] = htonl(c[0]);
			char size[1] = {1};
			write(sockfd, size, 1);
			write(sockfd, c, sizeof(uint32_t));
			fflush(stdout);
			FD_SET(sockfd, &readfd);
		}
		else if (FD_ISSET(sockfd, &readfd)) {
			char size[1];
			read(sockfd, size, 1);
			if (size[0] == 1) { /* normal */
				int *buffer = calloc(1, sizeof(int));
				read(sockfd, buffer, sizeof(uint32_t));
				*buffer = ntohl(*buffer);
				write(MASTERFD, buffer, sizeof(int));
			} else { /* winch */
				/*printf("Data from sockfd  -  %d\n", *c);*/
				int winsz[2];
				char *buffer = calloc(9, 1);
				read(sockfd, buffer, 9);
				write(sockfd, "OK", 2);
				parse_size(buffer, winsz);
				ioctl(SLAVEFD, TIOCSWINSZ, &win);
			}
			FD_SET(MASTERFD, &readfd);
		}
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
	argdata->data = &sockfd;
	/*argdata->addata = fdarr;*/
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
	SOCKFD = socket(AF_INET, SOCK_STREAM, 0);
	connect(SOCKFD, (struct sockaddr*)&address, addrlen);
	/* Send the request */
	char request[45];
	sprintf(request, "{\"request\": \"join-session\", \"code\": \"%s\"}", code);
	write(SOCKFD,request,45);
	char* resp = malloc(1);
	read(SOCKFD, resp, 1);
	if (*resp == '0') {
		/* You can print some error in red here */
		return 1;
	}
	/* Focus standard screen, that is were the action is going to take place. */
	touchwin(stdscr);
	wmove(stdscr, 0, 0);
	wrefresh(stdscr);
	struct winsize winsz;
	ioctl(1, TIOCGWINSZ, &winsz);
	char ws[9] = {0};
	sprintf(ws, "%d;%d", winsz.ws_row, winsz.ws_col);
	int k[1] = {9};
	write(SOCKFD, k, 1);
	write(SOCKFD, ws, 9);
	fd_set readfd;
	FD_ZERO(&readfd); FD_SET(SOCKFD, &readfd);
	pselect(SOCKFD+1, &readfd, NULL, NULL, NULL, NULL);
	read(SOCKFD, ws, 2);
	struct termios tty, old;
	tcgetattr(0,&old);
	tty = old;
	tty.c_cc[VTIME] = 0; tty.c_cc[VMIN] = 1;
	tty.c_iflag &= ~(IXON);
	tty.c_iflag &= (IGNCR);
	tty.c_lflag &= ~(ECHO | ICANON | ISIG);
	tcsetattr(0, TCSADRAIN, &tty);
	signal(SIGWINCH, swinch);
	FD_ZERO(&readfd); FD_SET(0, &readfd); FD_SET(SOCKFD, &readfd);
	while (1) {
		int res = pselect(SOCKFD+1, &readfd, NULL, NULL, NULL, NULL);
		if (FD_ISSET(SOCKFD, &readfd)) {
			char size[1];
			read(SOCKFD, size, 1);
			if (size[0] == 1) { /* normal */
				int *buffer = calloc(1,sizeof(int));
				read(SOCKFD, buffer, sizeof(uint32_t));
				*buffer = ntohl(*buffer);
				if (*buffer == 0) break;
				//wprintf(L"%lc", *buffer);
				printf("%c", *buffer);
				fflush(stdout);
				FD_SET(0, &readfd);
			}
		}
		else if (FD_ISSET(0, &readfd)) {
			char size[1] = {1};
			write(SOCKFD, size, 1);
			int *c = calloc(1, sizeof(int));
			read(0, c, sizeof(int));
			*c = htonl(*c);
			write(SOCKFD, c, sizeof(uint32_t));
			FD_SET(SOCKFD, &readfd);
		}
	}
	tcsetattr(0, TCSADRAIN, &old);
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
