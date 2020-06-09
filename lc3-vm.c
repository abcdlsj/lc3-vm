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
  R_PC,   // program counter
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

// definition trap code
enum {
  TRAP_GETC =
  0x20, /* get character from keyboard, not echoed onto the terminal */
  TRAP_OUT = 0x21,   /* output a character */
  TRAP_PUTS = 0x22,  /* output a word string */
  TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
  TRAP_PUTSP = 0x24, /* output a byte string */
  TRAP_HALT = 0x25   /* halt the program */
};

// KBSR: keyboard status register
// KBDR: keyboard data register
enum {
  MR_KBSR = 0xFE00, /* keyboard status */
  MR_KBDR = 0xFE02  /* keyboard data */
};

// 65536 locations
uint16_t memory[UINT16_MAX];

uint16_t reg[R_COUNT];

// the break condition
int running = 1;

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
void update_flags(uint16_t r) {
  if (reg[r] == 0) {
    reg[R_COND] = FL_ZRO;
  } else if (reg[r] >> 15) {
    reg[R_COND] = FL_NEG;
  } else {
    reg[R_COND] = FL_POS;
  }
}

// big-endian
uint16_t swap16(uint16_t x) { return (x << 8) | (x >> 8); }

void read_image_file(FILE *file) {
  uint16_t origin;
  fread(&origin, sizeof(origin), 1, file);
  origin = swap16(origin);

  uint16_t max_read = UINT16_MAX - origin;
  uint16_t *p = memory + origin;
  size_t read = fread(p, sizeof(uint16_t), max_read, file);

  while (read-- > 0) {
    *p = swap16(*p);
    ++p;
  }
}

int read_image(const char *image_path) {
  FILE *file = fopen(image_path, "rb");
  if (!file) {
    return 0;
  };
  read_image_file(file);
  fclose(file);
  return 1;
}

uint16_t check_key() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

// Memory Mapped Registers
void mem_write(uint16_t address, uint16_t val) { memory[address] = val; }

// Memory Mapped Registers
uint16_t mem_read(uint16_t address) {
  if (address == MR_KBSR) {
    if (check_key()) {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[address];
}
// unix terminal input
struct termios original_tio;

void disable_input_buffering() {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_lflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal) {
  restore_input_buffering();
  printf("\n");
  exit(-2);
}

// -------------------------------------------------

// Addition
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
    uint16_t imm = sign_extend(instr & 0x1F, 5);
    reg[dr] = reg[sr1] + imm;
  }

  update_flags(dr);
}

// Bit-wise Logical AND
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
    uint16_t imm = sign_extend(instr & 0x1f, 5);
    reg[dr] = reg[sr1] & imm;
  }

  update_flags(dr);
}

// Bit-Wise Complement
// Assembler Format
// NOT DR, SR
// example:
// NOT R4, R2 ;R4 <- NOT(R2)
void NOT(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t sr = (instr >> 6) & 0x7;

  reg[dr] = ~reg[sr];
  update_flags(dr);
}

// Conditional Branch
// Format:
// BR LAMBEL

// Description：
// The condition codes specified by the state of bits [11:9] are tested. If bit
// [11] is set, N is tested; if bit [11] is clear, N is not tested. If bit [10]
// is set, Z is tested, etc. If any of the condition codes tested is set, the
// program branches to the location specified by adding the sign-extended
// PCoffset9 field to the incremented PC.

// example：
// BRzp LOOP ;Branch to LOOP if the last result was zero or positive.
// BR   NEXT ;Unconditionally branch to NEXT.
void BR(uint16_t instr) {
  uint16_t cond_flag = (instr >> 9) & 0x7;
  uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
  if (cond_flag & reg[R_COND]) {
    reg[R_PC] += pc_offset;
  }
}

// Jump/Return from Subroutine
// example:
// JMP R2 ;PC <- R2
// RET    ;PC <- R7
void JMP(uint16_t instr) {
  // Also handles RET
  uint16_t jmp_flag = (instr >> 6) & 0x7;
  reg[R_PC] = reg[jmp_flag];
}

// Jump to Subroutine
// JSR/JSRR LABEL/BaseR
// Description
// First, the incremented PC is saved in R7. This is the linkage back to the
// calling routine. Then the PC is loaded with the address of the first
// instruction of the subroutine, causing an unconditional jump to that address.
// The address of the subroutine is obtained from the base register (if bit [11]
// is 0), or the address is computed by sign-extending bits [10:0] and adding
// this value to the incremented PC (if bit [11] is 1).

void JSR(uint16_t instr) {
  uint16_t baser = (instr >> 6) & 0x7;
  uint16_t long_pc_offset = sign_extend(instr & 0x7ff, 11);
  uint16_t long_flag = (instr >> 11) & 1;

  reg[R_R7] = reg[R_PC];

  if (long_flag) {
    reg[R_PC] += long_pc_offset; // JSR
  } else {
    reg[R_PC] = reg[baser]; // JSRR
  }
}

// load
// lD DR, LABEL
// example:
// LD R4, VALUE ;R4 <- mem[VALUE]
void LD(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

  reg[dr] = mem_read(reg[R_PC] + pc_offset);
  update_flags(dr);
}

// load indirect
// example:
// LDI R4, ONEMORE ; R4 <- mem[mem[ONEMORE]]
void LDI(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

  reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset));
  update_flags(dr);
}

// Load Base+offset
// Assembler Format
// LDR DR, BaseR, offset6
// example:
// LDR R4, R2, #−5 ;R4 <- mem[R2 − 5]
void LDR(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t baser = (instr >> 6) & 0x7;
  uint16_t offset = sign_extend(instr & 0x3f, 6);

  reg[dr] = mem_read(reg[baser] + offset);
  update_flags(dr);
}

// Load Effective Address
// Assembler Format
// LEA DR, LABEL
// Description:
// An address is computed by sign-extending bits [8:0] to 16 bits and adding
// this value to the incremented PC. This address is loaded into DR. The
// condition codes are set, based on whether the value loaded is negative, zero,
// or positive.
// Example:
// LEA R4, TARGET ;R4 <- address of TARGET.

void LEA(uint16_t instr) {
  uint16_t dr = (instr >> 9) & 0x7;
  uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

  reg[dr] = reg[R_PC] + pc_offset;
  update_flags(dr);
}

// Store
// Assembler Format
// ST SR, LABEL
// Description:
// The contents of the register specified by SR are stored in the memory
// location whose address is computed by sign-extending bits [8:0] to 16 bits
// and adding this value to the incremented PC.
// Example:
// ST R4, HERE ;mem[HERE] <- R4
void ST(uint16_t instr) {
  uint16_t sr = (instr >> 9) & 0x7;
  uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

  mem_write(reg[R_PC] + pc_offset, reg[sr]);
}

// Store Indirect
// Assembler Format
// STI SR, LABEL
// Description
// The contents of the register specified by SR are stored in the memory
// location whose address is obtained as follows: Bits [8:0] are sign-extended
// to 16 bits and added to the incremented PC. What is in memory at this address
// is the address of the location to which the data in SR is stored.
// Example:
// STI R4, NOT_HERE ;mem[mem[NOT_HERE]] <- R4
void STI(uint16_t instr) {
  uint16_t sr = (instr >> 9) & 0x7;
  uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

  mem_write(mem_read(reg[R_PC] + pc_offset), reg[sr]);
}
// Store Base+offset
// Assembler Format
// STR SR, BaseR, offset6
// Example:
// STR R4, R2, #5 ;mem[R2 + 5] <- R4
void STR(uint16_t instr) {
  uint16_t sr = (instr >> 9) & 0x7;
  uint16_t baser = (instr >> 6) & 0x7;
  uint16_t offset = sign_extend(instr & 0x3f, 6);

  mem_write(reg[baser] + offset, reg[sr]);
}

// TRAP

// TRAP_GETC = 0x20, /* get character from keyboard, not echoed onto the
// terminal */ TRAP_OUT = 0x21,   /* output a character */ TRAP_PUTS = 0x22,  /*
// output a word string */ TRAP_IN = 0x23,    /* get character from keyboard,
// echoed onto the terminal */ TRAP_PUTSP = 0x24, /* output a byte string */
// TRAP_HALT = 0x25   /* halt the program */

void GETC() { reg[R_R0] = (uint16_t) getchar(); }

void OUT() {
  putc((char) reg[R_R0], stdout);
  fflush(stdout);
}

void PUTS() {
  uint16_t *c = memory + reg[R_R0];
  while (*c) {
    putc((char) *c, stdout);
    ++c;
  }
  fflush(stdout);
}

void IN() {
  printf("Enter a character: ");
  char c = getchar();
  putc(c, stdout);
  reg[R_R0] = (uint16_t) c;
}

void PUTSP() {
  uint16_t *c = memory + reg[R_R0];
  while (*c) {
    char char1 = (*c) & 0xff;
    putc(char1, stdout);
    char char2 = (*c) >> 8;
    if (char2) {
      putc(char2, stdout);
    }
    ++c;
  }
  fflush(stdout);
}

void HALT() {
  puts("HALT");
  fflush(stdout);
  running = 0;
}

// -------------------main func----------------------
int main(int argc, const char *argv[]) {
  // show usage string
  if (argc < 2) {
    printf("lc3 [image-file1] ...\n");
    exit(2);
  }

  for (int i = 1; i < argc; i++) {
    if (!read_image(argv[i])) {
      printf("failed to load image: %s\n", argv[i]);
      exit(1);
    }
  }

  /* Setup */
  signal(SIGINT, handle_interrupt);
  disable_input_buffering();

  /* set the PC to starting position */
  /* 0x3000 is the default */
  enum { PC_START = 0x3000 };
  reg[R_PC] = PC_START;

  while (running) {
    uint16_t instr = mem_read(reg[R_PC]++);
    uint16_t op = instr >> 12;

    switch (op) {
      case OP_ADD:ADD(instr);
        break;
      case OP_AND:AND(instr);
        break;
      case OP_NOT:NOT(instr);
        break;
      case OP_BR:BR(instr);
        break;
      case OP_JMP:JMP(instr);
        break;
      case OP_JSR:JSR(instr);
        break;
      case OP_LD:LD(instr);
        break;
      case OP_LDI:LDI(instr);
        break;
      case OP_LDR:LDR(instr);
        break;
      case OP_LEA:LEA(instr);
        break;
      case OP_ST:ST(instr);
        break;
      case OP_STI:STI(instr);
        break;
      case OP_STR:STR(instr);
        break;
      case OP_TRAP:
        switch (instr & 0xFF) {
          case TRAP_GETC:GETC();
            break;
          case TRAP_OUT:OUT();
            break;
          case TRAP_PUTS:PUTS();
            break;
          case TRAP_IN:IN();
            break;
          case TRAP_PUTSP:PUTSP();
            break;
          case TRAP_HALT:HALT();
            break;
        }
        break;
      case OP_RES:
      case OP_RTI:
      default:abort();
        break;
    }
  }
  restore_input_buffering();
}