#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <vector>
#include <string>
#include <ncurses.h>

void init_instruction(void);

void initialize_instruction(void);

void instruction_update_display(void);

void clear_instruction(void);

extern vector<class Instruction *> instruction_all;

class Instruction;
class Instruction {
public:
	char desc[4096];
};

class instruction_window* inst_window;
#endif
