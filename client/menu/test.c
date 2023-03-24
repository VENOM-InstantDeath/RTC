#include <ncurses.h>
#include "menu.h"

int callx(void *x) {endwin();exit(0);return 0;}
int _continue(void *x) {return (int)((int*)x);}

int main() {
	WINDOW* stdscr = initscr();
	start_color(); use_default_colors();
	init_pair(1, 0, 7); init_pair(2, 15, 20);
	int caps[2]; getmaxyx(stdscr,caps[0],caps[1]);
	struct optst opts;
	const char* topts[3] = {"exit", "continue", "break"};
	int (*func[3])(void*) = {callx, _continue, _continue};
	void* args[3] = {NULL, (void*)1, (void*)0};
	opts.opt = topts;
	opts.func = func;
	opts.size = 3;
	int wcaps[4] = {4, 20, caps[0]/2-2, caps[1]/2-10};
	int colors[2] = {2,1};
	struct bindst bindings; bindings.size = 0;
	while (1) {
		if (menu(stdscr, wcaps, opts, args, defwrite, 0, bindings, 1, colors, NULL)) {
			wmove(stdscr, 0,0);wclrtobot(stdscr);
			wrefresh(stdscr); continue;
		} else break;
	}
	endwin();
	return 0;
}
