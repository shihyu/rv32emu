// uncomment this for an instruction trace and other debug outputs
#define DEBUG_OUTPUT
#define DEBUG_EXTRA

#include "emu-rv32i.h"

#include <stdio.h>

uint16_t PROGRAM[] = {
    0b0010010110000001, // R11=0
    0b1001010110101010, // R11+=R10
    0b0001010101111101, // R10-=1
    0b1111110101110101, // IF R10 GOTO -2
    0b1000010100101110, // R10=R11
    0b1000000010000010, // RET
};

/*
uint16_t PROGRAM[] = {
    0b0010010100000101, // :'R10=1
    0b0000010100000101, // :'R10+=1
    0b0011010110000001, // :'R11=32
    0b1100000110001000, // :'[R11+0]=R10
    0b1011111111100101 //  :'GOTO -4
};
*/

/*
uint16_t PROGRAM[] = {
    0b0010010100000101, // :'R10=1
    0b0000010100000101, // :'R10+=1
    0b0011010110000001, // :'R11=32
    0b1000010100101110, // :'R10=R11
    0b1001010100101110, // :'R10+=R11
    0b1000010100001101, // :'R10>>=1
    0b0000010101000010, // :'R10<<=16
    0b1000110100001101, // :'R10-=R11
    //0b1000110101001101, // :'R10|=R11
    //0b1000110100101101, // :'R10^=R11
    //0b1000110101101101, // :'R10&=R11
    0b1100000110001000, // :'[R11+0]=R10
    0b0100000110010000, // :'R12=[R11+0]
    0b0000000000000001, // NOP
    0b1000000010000010 // :'RET
    //0b1011111111100101 //  :'GOTO -4
};
*/

int main(int argc, char** argv) {
    uint32_t start = 0;
    ram_start = 0;
    uint32_t end = 0xfffffffe;

    for (int i = 0; i < sizeof(PROGRAM); i++) {
        *(uint16_t*)(ram + start + i * 2) = PROGRAM[i];
    }

    pc = start;
    reg[2] = ram_start + RAM_SIZE; // sp - stack pointer
    reg[1] = end; // ra - return adderss

    reg[10] = 10; // a0
    reg[11] = 0; // a1

    while (machine_running) {
        insn = get_insn(pc);
        printf("[%08x]=%08x pc:%8x\n", pc, insn, next_pc);
        execute_instruction();
        //printf("[%08x]=%08x pc:%8x (after)\n", pc, insn, next_pc);
        if (mcause) {
            printf("exception %d\n", mcause);
            break;
        }
        pc = next_pc;
        if (pc == end)
            break;
    }

    #ifdef DEBUG_EXTRA
    dump_regs();
    #else
    printf("x10 a0: %08x\n", reg[10]);
    #endif
    
    printf("x10 a0: %d\n", reg[10]);

    return 0;
}
