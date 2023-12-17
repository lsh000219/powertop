#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <vector>
#include <string>
#include <ncurses.h>

static void init_instruction(void);

extern void initialize_instruction(void);

extern void instruction_update_display(void);

extern void clear_instruction(void);

extern vector<class Instruction *> instruction_all;

class Instruction;
class Instruction {
public:
	char desc[4096];

	Instruction(const char *desc)
	{
		strcpy(this->desc, desc);
	}
};

class instruction_window* inst_window;
#endif
