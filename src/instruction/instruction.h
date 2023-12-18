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

void init_instruction(void);

void __initialize_instruction(void);

void instruction_update_display(void);

void clear_instruction(void);

void instruction_update_display(void);

extern vector<class Instruction *> instruction_all;

class Instruction;
class Instruction {
public:
	char desc[4096];
};

#endif
