#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* unix */
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>

// registers
enum {
  R_R0 = 0,
  R_R1,
  R_R2,
  R_R3,
  R_R4,
  R_R5,
  R_R6,
  R_R7,
  R_RC,   // program counter
  R_COND, // condition flags
  R_COUNT
};

enum {
  OP_BR = 0, /* branch */
  OP_ADD,    /* add  */
  OP_LD,     /* load */
  OP_ST,     /* store */
  OP_JSR,    /* jump register */
  OP_AND,    /* bitwise and */
  OP_LDR,    /* load register */
  OP_STR,    /* store register */
  OP_RTI,    /* unused */
  OP_NOT,    /* bitwise not */
  OP_LDI,    /* load indirect */
  OP_STI,    /* store indirect */
  OP_JMP,    /* jump */
  OP_RES,    /* reserved (unused) */
  OP_LEA,    /* load effective address */
  OP_TRAP    /* execute trap */
};

enum {
  FL_POS = 1 << 0, /* P */
  FL_ZRO = 1 << 1, /* Z */
  FL_NEG = 1 << 2, /* N */
};

enum {
  TRAP_GETC =
      0x20, /* get character from keyboard, not echoed onto the terminal */
  TRAP_OUT = 0x21,   /* output a character */
  TRAP_PUTS = 0x22,  /* output a word string */
  TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
  TRAP_PUTSP = 0x24, /* output a byte string */
  TRAP_HALT = 0x25   /* halt the program */
};

// 65536 locations
uint16_t memory[UINT16_MAX];

uint16_t reg[R_COUNT];

// If bit [5] is 0, the second source operand is obtained from SR2. If bit [5]
// is 1, the second source operand is obtained by sign-extending the imm5 field
// to 16 bits. In both cases, the second source operand is added to the contents
// of SR1 and the result stored in DR.

// sign-extending
uint16_t sign_extend(uint16_t x, int bit_count) {
  if ((x >> (bit_count - 1)) & 1) {
    x |= (0xFFFF << bit_count);
  }
  return x;
}

// The condition codes are set, based on whether the result is
// negative, zero, or positive.

// update flags: negative, zero, positive
void update_falgs(uint16_t r) {
  if (reg[r] == 0) {
    reg[R_COND] = FL_ZRO;
  } else if (reg[r] >> 15) {
    reg[R_COND] = FL_POS;
  } else {
    reg[R_COND] = FL_NEG;
  }
}

// implementation ADD
// ADD | DR | SR1 | SR2/imm5
// example:
// ADD R2, R3, R4 ;R2 ← R3 + R4
// ADD R2, R3, #7 ;R@ ← R3 + 7
void ADD(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t sr1 = (instr >> 6) & 0x7;
  uint16_t imm_flag = (instr >> 5) & 0x1;

  if (!imm_flag) {
    uint16_t sr2 = instr & 0x7;
    reg[dr] = reg[sr1] + reg[sr2];
  } else {
    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
    reg[dr] = reg[sr1] + imm5;
  }

  update_falgs(dr);
}

// implementation AND
// AND | DR | SR1 | SR2/imm5
// example:
// AND R2, R3, R4 ;R2 ← R3 AND R4
// AND R2, R3, #7 ;R2 ← R3 AND 7
void AND(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t sr1 = (instr >> 6) & 0x7;
  uint16_t imm_flag = (instr >> 5) & 0x1;

  if (!imm_flag) {
    uint16_t sr2 = instr & 0x7;
    reg[dr] = reg[sr1] & reg[sr2];
  } else {
    uint16_t imm5 = sign_extend((instr & 0x1f, 5);
    reg[dr] = reg[sr1] & imm5;
  }

  update_falgs(dr);
}