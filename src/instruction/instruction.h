#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <vector>
#include <string>
#include "display.h"

struct Instruction {
    std::string command;
    std::string description;
};

class instruction_window : public tab_window {
public:
    virtual void repaint(void);
    virtual void cursor_enter(void);
    virtual void expose(void);
    virtual void window_refresh(void);
};

static void init_instruction(void);

void initialize_instruction(void);

void instruction_update_display(void);

void clear_instruction(void);

extern std::vector<Instruction> instruction_all;

#endif
