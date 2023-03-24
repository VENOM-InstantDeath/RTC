#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <json.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include "menu/menu.h"

const char *VERSION = "v1.0.0";


int host(WINDOW* win, int* wcaps, void* data) {
	return 1;
}

int guest(WINDOW* win, int* wcaps, void* data) {
	return 1;
}

int tickf(WINDOW* win, int* mdata, void* data) {
	int **_data = (int**)data;
	if (_data[mdata[1]][2] == 0) {
		if (_data[mdata[1]][3] == 0) {
			_data[mdata[1]][3] = 1;
			mvwaddstr(win, mdata[0], 1, "x");
		} else {
			_data[mdata[1]][3] = 0;
			mvwaddstr(win, mdata[0], 1, " ");
		}
	}
	return 1;
}

void wr_optselect(WINDOW* win, int* wcaps, struct optst opts, int mode, int* mdata, void* data, int* colors) {
	/* data va a contener un array con la misma cantidad de elementos que la cantidad de opciones.
	 * Ese array va a tener las coordenadas de cada entrada (y,x) + un tercer elemento, la selección (1|0)
	 * Esto implica un int**. */
	/* Esta función tiene que imprimir cada entrada con su [ ] */
	int **_data = (int**)data;
	const char** keys = opts.opt;
	if (!mode) {
		int tmp = 0;
		for (int i=mdata[2]; i<mdata[3]; i++) {
			if (i == opts.size) break;
			mvwaddstr(win,tmp, 0, "[ ]");
			mvwaddstr(win,tmp, 4, keys[i]);
			tmp++;
		}
		wattron(win, COLOR_PAIR(colors[1]));
		mvwaddstr(win, 0, 4, keys[0]);
		wattroff(win, COLOR_PAIR(colors[1]));
		wrefresh(win);
		return;
	}
	mvwaddstr(win, mdata[0], 4, keys[mdata[1]]);
	if (mode == 1) {
		if (mdata[1]) { /*if sp != 0*/
			mdata[1]--; mdata[0] = _data[mdata[1]][0];
		}
	} else {
		if (mdata[1] != mdata[3]-1) {
			mdata[1]++; mdata[0] = _data[mdata[1]][0];
		}
	}
	wattron(win, COLOR_PAIR(colors[1]));
	mvwaddstr(win, mdata[0], 4, keys[mdata[1]]);
	wattroff(win, COLOR_PAIR(colors[1]));
	wrefresh(win);
}

int main() {
	WINDOW* stdscr = initscr();
	int y, x; getmaxyx(stdscr, y,x);
	start_color(); use_default_colors();
	curs_set(0);
	init_pair(1, 0, 7);
	init_pair(2, 7, 20);
	/*v1-2-r3-4-t5-6*/
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
	int wcaps[4] = {3, 20, 3, wx/2-10};
	struct optst opts;
	const char* tmpopts[2] = {"Host", "Guest"};
	int (*tmpfunc[2])(WINDOW*, int*, void*) = {tickf, tickf};
	opts.opt = tmpopts;
	opts.func = tmpfunc;
	opts.size = 2;
	struct bindst bindings; bindings.size = 0;
	int colors[2] = {2, 1};
	while (1) {
		if (menu(selectwin, wcaps, opts, defwrite, 1, bindings, 1, colors, NULL)) {
			wmove(selectwin, 0, 0); wclrtobot(selectwin);
			continue;
		} else break;
	}

	/* 	SELECTWIN HOST
	mvwaddstr(selectwin, 3,(wx/2)-2, "Host");
	mvwaddstr(selectwin, 5,15, "[ ] Run in background");
	mvwaddstr(selectwin, 6,15, "[ ] LAN only");
	mvwaddstr(selectwin, 9,(wx/2)-2, "[OK]");
	wgetch(selectwin);
	*/
	endwin();
	return 0;
}


/*while (1) {
	int res = menu();
	if (res == 1) continue;
	else if (res == 2) {
		if (foo());
		etc;
	}
	else break;
}*/
