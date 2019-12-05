/*
 * A minimalist RISC-V emulator for the RV32I architecture.
 *
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "emu-rv32i.h"

/* returns realtime in nanoseconds */
int64_t get_clock()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}


void riscv_cpu_interp_x32()
{
    /* we use a single execution loop to keep a simple control flow for
     * emscripten */
    while (machine_running) {
#if 1
        /* update timer, assuming 10 MHz clock (100 ns period) for the mtime
         * counter */
        mtime = get_clock() / 100ll;

        /* for reproducible debug runs, you can use a fixed fixed increment per
         * instruction */
#else
        mtime += 10;
#endif
        /* default value for next PC is next instruction, can be changed by
         * branches or exceptions */
        next_pc = pc + 4;

        /* test for timer interrupt */
        if (mtimecmp <= mtime) {
            mip |= MIP_MTIP;
        }
        if ((mip & mie) != 0 && (mstatus & MSTATUS_MIE)) {
            raise_interrupt();
        } else {
            /* normal instruction execution */
            insn = get_insn32(pc);
            insn_counter++;

            debug_out("[%08x]=%08x, mtime: %lx, mtimecmp: %lx\n", pc, insn,
                      mtime, mtimecmp);
            execute_instruction();
        }

        /* test for misaligned fetches */
        if (next_pc & 3) {
            raise_exception(CAUSE_MISALIGNED_FETCH, next_pc);
        }

        /* update current PC */
        pc = next_pc;
    }

    debug_out("done interp %lx int=%x mstatus=%lx prv=%d\n",
              (uint64_t) insn_counter, mip & mie, (uint64_t) mstatus, priv);
}

int main(int argc, char **argv)
{
#ifdef DEBUG_OUTPUT
    FILE *fo;
    char *po, hex_file[100];
#endif

    /* automatic STDOUT flushing, no fflush needed */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* parse command line */
    const char *elf_file = NULL;
    const char *signature_file = NULL;
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg == strstr(arg, "+signature=")) {
            signature_file = arg + 11;
        } else if (arg[0] != '-') {
            elf_file = arg;
        }
    }
    if (elf_file == NULL) {
        printf("missing ELF file\n");
        return 1;
    }

    for (uint32_t u = 0; u < RAM_SIZE; u++)
        ram[u] = 0;


#ifdef DEBUG_EXTRA
    init_stats();
#endif

    uint32_t start = 0;
    
    /* open ELF file */
    elf_version(EV_CURRENT);
    int fd = open(elf_file, O_RDONLY);
    if (fd == -1) {
        printf("can't open file %s\n", elf_file);
        return 1;
    }
    Elf *elf = elf_begin(fd, ELF_C_READ, NULL);

    /* scan for symbol table */
    Elf_Scn *scn = NULL;
    GElf_Shdr shdr;
    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        gelf_getshdr(scn, &shdr);
        if (shdr.sh_type == SHT_SYMTAB) {
            Elf_Data *data = elf_getdata(scn, NULL);
            int count = shdr.sh_size / shdr.sh_entsize;
            for (int i = 0; i < count; i++) {
                GElf_Sym sym;
                gelf_getsym(data, i, &sym);
                char *name = elf_strptr(elf, shdr.sh_link, sym.st_name);
                if (strcmp(name, "begin_signature") == 0) {
                    begin_signature = sym.st_value;
                }
                if (strcmp(name, "end_signature") == 0) {
                    end_signature = sym.st_value;
                }

                /* for compliance test */
                if (strcmp(name, "_start") == 0) {
                    start = sym.st_value;
                }

                /* for zephyr */
                if (strcmp(name, "__reset") == 0) {
                    start = sym.st_value;
                }
                if (strcmp(name, "__irq_wrapper") == 0) {
                    mtvec = sym.st_value;
                }
            }
        }
    }

    /* set .text section as the base address */
    scn = NULL;
    size_t shstrndx;
    elf_getshdrstrndx(elf, &shstrndx);
    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        gelf_getshdr(scn, &shdr);
        const char *name = elf_strptr(elf, shstrndx, shdr.sh_name);

        if (shdr.sh_type == SHT_PROGBITS) {
            if (strcmp(name, ".text") == 0) {
                ram_start = shdr.sh_addr;
                break;
            }
        }
    }

    debug_out("begin_signature: 0x%08x\n", begin_signature);
    debug_out("end_signature: 0x%08x\n", end_signature);
    debug_out("ram_start: 0x%08x\n", ram_start);
    debug_out("entry point: 0x%08x\n", start);

    /* scan for program */
    scn = NULL;
    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        gelf_getshdr(scn, &shdr);

        /* filter NULL address sections and .bss */
        if (shdr.sh_addr && shdr.sh_type != SHT_NOBITS) {
            Elf_Data *data = elf_getdata(scn, NULL);
            if (shdr.sh_addr >= ram_start) {
                for (size_t i = 0; i < shdr.sh_size; i++) {
                    ram_curr = shdr.sh_addr + i - ram_start;
                    if (ram_curr >= RAM_SIZE) {
                        debug_out(
                            "memory pointer outside of range 0x%08x (section "
                            "at address 0x%08x)\n",
                            ram_curr, (uint32_t) shdr.sh_addr);
                        /* break; */
                    } else {
                        ram[ram_curr] = ((uint8_t *) data->d_buf)[i];
                        if (ram_curr > ram_last)
                            ram_last = ram_curr;
                    }
                }
            } else {
                debug_out("ignoring section at address 0x%08x\n",
                          (uint32_t) shdr.sh_addr);
            }
        }
    }

    /* close ELF file */
    elf_end(elf);
    close(fd);

#ifdef DEBUG_OUTPUT
    printf("codesize: 0x%08x (%i)\n", ram_last + 1, ram_last + 1);
    strcpy(hex_file, elf_file);
    po = strrchr(hex_file, '.');
    if (po != NULL)
        *po = 0;
    strcat(hex_file, ".mem");
    fo = fopen(hex_file, "wt");
    if (fo != NULL) {
        for (uint32_t u = 0; u <= ram_last; u++) {
            fprintf(fo, "%02X ", ram[u]);
            if ((u & 15) == 15)
                fprintf(fo, "\n");
        }
        fprintf(fo, "\n");
        fclose(fo);
    }
#if 1
    fo = fopen("rom.v", "wt");
    if (fo != NULL) {
        fprintf(fo, "module rom(addr,data);\n");
        uint32_t romsz = (ram_start & 0xFFFF) + ram_last + 1;
        printf("codesize with offset: %i\n", romsz);
        if (romsz >= 32768)
            fprintf(fo, "input [15:0] addr;\n");
        else if (romsz >= 16384)
            fprintf(fo, "input [14:0] addr;\n");
        else if (romsz >= 8192)
            fprintf(fo, "input [13:0] addr;\n");
        else if (romsz >= 4096)
            fprintf(fo, "input [12:0] addr;\n");
        else if (romsz >= 2048)
            fprintf(fo, "input [11:0] addr;\n");
        else if (romsz >= 1024)
            fprintf(fo, "input [10:0] addr;\n");
        else if (romsz >= 512)
            fprintf(fo, "input [9:0] addr;\n");
        else if (romsz >= 256)
            fprintf(fo, "input [8:0] addr;\n");
        else
            fprintf(fo, "input [7:0] addr;\n");
        fprintf(fo,
                "output reg [7:0] data;\nalways @(addr) begin\n case(addr)\n");
        for (uint32_t u = 0; u <= ram_last; u++) {
            fprintf(fo, " %i : data = 8'h%02X;\n", (ram_start & 0xFFFF) + u,
                    ram[u]);
        }
        fprintf(fo,
                " default: data = 8'h01; // invalid instruction\n "
                "endcase\nend\nendmodule\n");
        fclose(fo);
    }
#endif
#endif

    uint64_t ns1 = get_clock();

    /* run program in emulator */
    pc = start;
    reg[2] = ram_start + RAM_SIZE;
    riscv_cpu_interp_x32();

    uint64_t ns2 = get_clock();

    /* write signature */
    if (signature_file) {
        FILE *sf = fopen(signature_file, "w");
        int size = end_signature - begin_signature;
        for (int i = 0; i < size / 16; i++) {
            for (int j = 0; j < 16; j++) {
                fprintf(sf, "%02x", ram[begin_signature + 15 - j - ram_start]);
            }
            begin_signature += 16;
            fprintf(sf, "\n");
        }
        fclose(sf);
    }

#ifdef DEBUG_EXTRA
    dump_regs();
    print_stats(insn_counter);
#endif

#if 1
    printf("\n");
    printf(">>> Execution time: %llu ns\n", (long long unsigned) ns2 - ns1);
    printf(">>> Instruction count: %llu (IPS=%llu)\n",
           (long long unsigned) insn_counter,
           (long long) insn_counter * 1000000000LL / (ns2 - ns1));
    printf(">>> Jumps: %llu (%2.2lf%%) - %llu forwards, %llu backwards\n",
           (long long unsigned) jump_counter,
           jump_counter * 100.0 / insn_counter,
           (long long unsigned) forward_counter,
           (long long unsigned) backward_counter);
    printf(">>> Branching T=%llu (%2.2lf%%) F=%llu (%2.2lf%%)\n",
           (long long unsigned) true_counter,
           true_counter * 100.0 / (true_counter + false_counter),
           (long long unsigned) false_counter,
           false_counter * 100.0 / (true_counter + false_counter));
    printf("\n");
#endif
    return 0;
}
