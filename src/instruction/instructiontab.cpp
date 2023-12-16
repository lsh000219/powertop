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

std::vector<std::string> instruction_all;

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

static void init_instruction(void) {
    instruction_all.clear(); 
    instruction_all.push_back("Overview - CPU에 웨이크업을 가장 자주 보내거나 시스템 전원을 가장 많이 사용하는 시스템 구성 요소 목록을 볼 수 있습니다.");
    instruction_all.push_back("Usage - 초당 전력 사용량 / Events/s - 초당 event(Wakeup) 발생량 / Category - 분류 / Description - 설명");
    instruction_all.push_back("Idle stats - 코어상태에 대한 다양한 정보를 표시합니다.");
    instruction_all.push_back("Frequency stats - CPU 웨이크업 빈도를 표시합니다.");
    instruction_all.push_back("Device stats - Overview 탭과 유사한 정보를 제공하지만 device에만 해당됩니다");
    instruction_all.push_back("Usage - 전력 사용 비율 / Device name - 기기 이름");
    instruction_all.push_back("Tunable - 전력 소비를 줄이기 위해 시스템을 최적화하기 위한 제안을 제공합니다.");
    instruction_all.push_back("위쪽 및 아래 키를 사용하여 제안을 통해 이동하고, Enter 키를 사용하여 제안을 전환하거나 해제할 수 있습니다.");
    instruction_all.push_back("전력소비 최적화 상태 / 제안");
}

void initialize_instruction(void) {
    instruction_window* win = new instruction_window();
    create_tab("Instruction", _("Instruction"), win, _(" <ESC> Exit | <Enter> Execute Instruction | <r> Window refresh"));
    init_instruction();
    if (win) {
        win->cursor_max = instruction_all.size() - 1;
        if (newtab_window)
            delete newtab_window;
        newtab_window = win;
    }
}

void instruction_update_display(void) {
    class tab_window* wt = tab_windows["Instruction"];
    if (!wt)
        return;
    wt->repaint();
}

void clear_instruction() {
    instruction_all.clear();
}


