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

public void init_instruction(void);

public void __initialize_instruction(void);

public void instruction_update_display(void);

public void clear_instruction(void);

public void instruction_update_display(void);

extern vector<class Instruction *> instruction_all;

class Instruction;
class Instruction {
public:
	char desc[4096];
};

class instruction_window* inst_window;
#endif
