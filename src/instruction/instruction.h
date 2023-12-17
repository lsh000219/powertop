#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <vector>
#include <string>
#include "display.h"

static void init_instruction(void);

extern void initialize_instruction(void);

extern void instruction_update_display(void);

extern void clear_instruction(void);

extern std::vector<Instruction> instruction_all;
#endif
