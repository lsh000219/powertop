#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "../display.h"
#include <vector>
#include <string>
#include <ncurses.h>

class instruction_window* inst_window;

class instruction_window : public tab_window {
public:
    virtual void repaint(void);
    virtual void cursor_enter(void);
    virtual void expose(void);
    virtual void window_refresh(void);
};

void init_instruction(void)
{
	instruction_update_display();
}

void __initialize_instruction(void)
{
	 class instruction_window* w;

	w = new instruction_window();
	create_tab("Instructions", _("Instructions"), w, _(" <ESC> Exit |"));

	init_instruction();

	if (inst_window)
		delete inst_window;

	inst_window = w;
}

void instruction_update_display(void)
{
	WINDOW* win;

	win = get_ncurses_win("Instructions");

	if (!win)
		return;

	if (should_clear) {
		should_clear = false;
		wclear(win);
	}

	wmove(win, 2, 0);

	wprintw(win, "%s\n", "Overview - CPU에 웨이크업을 가장 자주 보내거나 시스템 전원을 가장 많이 사용하는 시스템 구성 요소 목록을 볼 수 있습니다.");
	wprintw(win, "%s\n", "Usage - 초당 전력 사용량 / Events/s - 초당 event(Wakeup) 발생량 / Category - 분류 / Description - 설명");
	wprintw(win, "%s\n", "Idle stats - 코어상태에 대한 다양한 정보를 표시합니다.");
	wprintw(win, "%s\n", "Frequency stats - CPU 웨이크업 빈도를 표시합니다.");
	wprintw(win, "%s\n", "Device stats - Overview 탭과 유사한 정보를 제공하지만 device에만 해당됩니다.");
	wprintw(win, "%s\n", "Usage - 전력 사용 비율 / Device name - 기기 이름");
	wprintw(win, "%s\n", "Tunable - 전력 소비를 줄이기 위해 시스템을 최적화하기 위한 제안을 제공합니다. 위쪽 및 아래 키를 사용하여 제안을 통해 이동하고, Enter 키를 사용하여 제안을 전환하거나 해제할 수 있습니다.");
}

void clear_instruction(void)
{
	instruction_all.clear();
}

void instruction_update_display(void)
{
	class tab_window* w;

	w = tab_windows["Instructions"];
	if (!w)
		return;
	w->repaint();
}

void instruction_window::repaint(void) {
    __instruction_update_display();
}

extern vector<class Instruction *> instruction_all;

class Instruction;
class Instruction {
public:
	char desc[4096];
};

class instruction_window* inst_window;
#endif
