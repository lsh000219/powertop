#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <ncurses.h>
#include <vector>
#include "../lib.h"
#include "../measurement/sysfs.h"
#include "../display.h"
#include "../report/report.h"
#include "../report/report-maker.h"
#include "../report/report-data-html"
#include "instruction.h"
#include "display.h"

static bool should_clear = false;

class instruction_window *inst_window;

class instruction_window: public tab_window {
public:
	virtual void repaint(void);
	virtual void cursor_enter(void);
	virtual void expose(void);
	virtual void window_refresh(void);
};

static void init_instruction(void) {                                           
	instruction_update_display();
}

void initialize_instruction(void) {
    class instruction_window* w;

    w = new instruction_window();
    create_tab("Instructions", _("Instructions"), w, _(" <ESC> Exit |"));

    init_instruction();

    w->cursor_max = instruction_all.size() - 1;

    if (inst_window)
        delete inst_window;

    inst_window = w;
}

static void __instruction_update_display(int cursor_pos) {
    WINDOW* win;
    unsigned int i;

    win = get_ncurses_win("Instruction");

    if (!win)
        return;

    if (should_clear_instruction) {
        should_clear_instruction = false;
        wclear(win);
    }

    wmove(win, 2, 0);

    for (i = 0; i < instruction_all.size(); i++) {
    if ((int)i != cursor_pos) {
        wattrset(win, A_NORMAL);
        wprintw(win, "   ");
    }
    else {
        wattrset(win, A_REVERSE);
        wprintw(win, ">> ");
    }

    if (i == 0) {
        wprintw(win, "Overview - CPU에 웨이크업을 가장 자주 보내거나 시스템 전원을 가장 많이 사용하는 시스템 구성 요소 목록을 볼 수 있습니다.");
    }
    else if (i == 2) {
        wprintw(win, "Usage - 초당 전력 사용량 / Events/s - 초당 event(Wakeup) 발생량 / Category - 분류 / Description - 설명");
    }
    else if (i == 4) {
        wprintw(win, "Idle stats - 코어상태에 대한 다양한 정보를 표시합니다.");
    }
    else if (i == 6) {
        wprintw(win, "Frequency stats - CPU 웨이크업 빈도를 표시합니다.");
    }
    else if (i == 8) {
        wprintw(win, "Device stats - Overview 탭과 유사한 정보를 제공하지만 device에만 해당됩니다");
    }
    else if (i == 10) {
        wprintw(win, "Usage - 전력 사용 비율 / Device name - 기기 이름");
    }
    else if (i == 12) {
        wprintw(win, "Tunable - 전력 소비를 줄이기 위해 시스템을 최적화하기 위한 제안을 제공합니다.");
    }
    else if (i == 14) {
        wprintw(win, "위쪽 및 아래 키를 사용하여 제안을 통해 이동하고, Enter 키를 사용하여 제안을 전환하거나 해제할 수 있습니다.");
    }
    else if (i == 16) {
        wprintw(win, "전력소비 최적화 상태 / 제안");
    }

    wprintw(win, "\n");
}
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
    __instruction_update_display(cursor_pos);
}

void instruction_window::cursor_enter(void) {
    if (cursor_pos >= 0 && cursor_pos < instruction_all.size()) {
        std::string selectedInstruction = instruction_all[cursor_pos];
    }
}

void instruction_window::expose(void) {
    cursor_pos = 0;
    repaint();
}

void instruction_window::window_refresh(void) {
    clear_instruction();
    should_clear = true;
    init_instruction();
}

void clear_instruction() {
    instruction_all.clear();
}
