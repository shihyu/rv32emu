// uncomment this for an instruction trace and other debug outputs
#define DEBUG_OUTPUT
#define DEBUG_EXTRA

#include "emu-rv32i.h"

#include <stdio.h>

int main(int argc, char** argv) {
    uint32_t start = 0;
    ram_start = 0;
    uint32_t end = 0xfffffffe;

    *(uint*)(ram + start + 0) = 0b00000000101101010000010100110011; // add a0,a0,a1 - S-type reg-reg func7:0, rs2:11, rs1:10, funct3:0, rd:10, opcode:0b0110011 a0=a0+a1
    *(uint*)(ram + start + 4) = 0b00000000000000001000000001100111; // jal ra  - B-type branch imm:0, rs2:0, rs1:1, funct3:0, opcode:0b1100111, jal ra

    pc = start;
    reg[2] = ram_start + RAM_SIZE; // sp - stack pointer
    reg[1] = end; // ra - return adderss

    reg[10] = 1; // a0
    reg[11] = 1; // a1

    while (machine_running) {
        next_pc = pc + 4;
        insn = get_insn32(pc);
        printf("[%08x]=%08x\n", pc, insn);
        execute_instruction();
        pc = next_pc;
        if (pc == end)
            break;
    }

    #ifdef DEBUG_EXTRA
    dump_regs();
    #else
    printf("x10 a0: %08x\n", reg[10]);
    #endif

    return 0;
}
