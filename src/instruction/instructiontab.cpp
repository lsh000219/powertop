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
// instruction_all 벡터 정의
// std::vector<std::string> instruction_all;
//
// // instruction_window 클래스 멤버 함수 구현
//
// void instruction_window::repaint(void) {
//     __instruction_update_display(cursor_pos);
//     }
//
//     void instruction_window::cursor_enter(void) {
//         // 선택된 instruction을 실행하는 로직 추가
//             // 필요한 동작 수행
//
//                 // 예시: 선택된 instruction을 얻어오고 그에 따른 동작 수행
//                     if (cursor_pos >= 0 && cursor_pos < instruction_all.size()) {
//                             std::string selectedInstruction = instruction_all[cursor_pos];
//                                     // 선택된 instruction에 따른 동작 수행
//                                             // 예시: 실행 로직, 명령어 실행 등
//                                                 }
//                                                 }
//
//                                                 void instruction_window::expose(void) {
//                                                     cursor_pos = 0;
//                                                         repaint();
//                                                         }
//
//                                                         void instruction_window::window_refresh(void) {
//                                                             clear_instruction();
//                                                                 should_clear = true;
//                                                                     init_instruction();
//                                                                     }
//
//                                                                     // init_instruction 함수 구현
//
//                                                                     static void init_instruction(void) {
//                                                                         // instruction_all 벡터 초기화 및 데이터 추가
//                                                                             instruction_all.clear(); // 기존 데이터를 초기화하고 새로운 데이터 추가
//                                                                                 instruction_all.push_back("Command 1: Description 1");
//                                                                                     instruction_all.push_back("Command 2: Description 2");
//                                                                                         // ... 추가적인 데이터 추가
//
//                                                                                             // 다른 Instruction 탭에 필요한 초기화 로직 추가
//                                                                                             }
//
//                                                                                             // initialize_instruction 함수 구현
//
//                                                                                             void initialize_instruction(void) {
//                                                                                                 instruction_window* win = new instruction_window();
//                                                                                                     create_tab("Instruction", _("Instruction"), win, _(" <ESC> Exit | <Enter> Execute Instruction | <r> Window refresh"));
//                                                                                                         init_instruction();
//                                                                                                             if (win) {
//                                                                                                                     win->cursor_max = instruction_all.size() - 1;
//                                                                                                                             if (newtab_window)
//                                                                                                                                         delete newtab_window;
//                                                                                                                                                 newtab_window = win;
//                                                                                                                                                     }
//                                                                                                                                                     }
//
//                                                                                                                                                     // instruction_update_display 함수 구현
//
//                                                                                                                                                     void instruction_update_display(void) {
//                                                                                                                                                         class tab_window* wt = tab_windows["Instruction"];
//                                                                                                                                                             if (!wt)
//                                                                                                                                                                     return;
//                                                                                                                                                                         wt->repaint();
//                                                                                                                                                                         }
//
//                                                                                                                                                                         // clear_instruction 함수 구현
//
//                                                                                                                                                                         void clear_instruction() {
//                                                                                                                                                                             instruction_all.clear();
//                                                                                                                                                                             }
//
//                                                                                                                                                                             // show_instruction_tab 함수 구현
//
//                                                                                                                                                                             void show_instruction_tab() {
//                                                                                                                                                                                 const char* instructionText = R"(
//                                                                                                                                                                                 Overview - CPU에 웨이크업을 가장 자주 보내거나 시스템 전원을 가장 많이 사용하는 시스템 구성 요소 목록을 볼 수 있습니다.
//                                                                                                                                                                                     Usage - 초당 전력 사용량 / Events/s - 초당 event(Wakeup) 발생량 / Category - 분류 / Description - 설명
//
//                                                                                                                                                                                     Idle stats - 코어상태에 대한 다양한 정보를 표시합니다.
//
//                                                                                                                                                                                     Frequency stats - CPU 웨이크업 빈도를 표시합니다.
//
//                                                                                                                                                                                     Device stats - Overview 탭과 유사한 정보를 제공하지만 device에만 해당됩니다
//                                                                                                                                                                                         Usage - 전력 사용 비율 / Device name - 기기 이름
//
//                                                                                                                                                                                         Tunable - 전력 소비를 줄이기 위해 시스템을 최적화하기 위한 제안을 제공합니다.
//                                                                                                                                                                                             위쪽 및 아래 키를 사용하여 제안을 통해 이동하고, Enter 키를 사용하여 제안을 전환하거나 해제할 수 있습니다.
//                                                                                                                                                                                                 전력소비 최적화 상태 / 제안
//                                                                                                                                                                                                 )";
//
//                                                                                                                                                                                                     // Instruction 탭에 내용을 출력합니다.
//                                                                                                                                                                                                         mvwprintw(stdscr, 5, 5, "%s", instructionText);
//
//                                                                                                                                                                                                             // 화면 갱신
//                                                                                                                                                                                                                 wrefresh(stdscr);
//                                                                                                                                                                                                                 }
//
