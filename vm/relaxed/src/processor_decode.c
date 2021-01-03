#include "vm.h"

#include <assert.h>
#include <string.h>
#include <math.h>

static int32_t extendSign(uint32_t value, uint32_t bits)
{
    if(value > (1u << (bits - 1)))
    {
        return (int32_t)((0xFFFFFFFFu << bits) | value);
    }
    else
    {
        return (int32_t)value;
    }
}

static const Opcode BRUComparators[16] =
{
    OPCODE_BNE,
    OPCODE_BEQ,
    OPCODE_BL,
    OPCODE_BLE,
    OPCODE_BG,
    OPCODE_BGE,
    OPCODE_BLS,
    OPCODE_BLES,
    OPCODE_BGS,
    OPCODE_BGES,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN
};

static const Opcode BRUJumpsCalls[4] =
{
    OPCODE_CALL,
    OPCODE_JMP,
    OPCODE_CALLR,
    OPCODE_JMPR
};

static int decodeBRU(uint32_t pc, uint32_t opcode, Operation* restrict output)
{
    const uint32_t type = (opcode >> 2u) & 0x03u;

    if(type == 0) //REG-REG comparison or Branch
    {
        const uint32_t instruction = (opcode >> 4u) & 0x03u;

        if(instruction == 0) //CMP
        {
            const uint32_t size  = (opcode >> 8u ) & 0x03;
            const uint32_t right = (opcode >> 20u) & 0x3F;
            const uint32_t left  = (opcode >> 26u) & 0x3F;

            output->op = OPCODE_CMP;
            output->size = size;
            output->operands[0] = right;
            output->operands[1] = left;
        }
        else if(instruction == 1) //FCMP
        {
            const uint32_t right = (opcode >> 18u) & 0x7F;
            const uint32_t left  = (opcode >> 25u) & 0x7F;

            output->op = OPCODE_FCMP;
            output->operands[0] = right;
            output->operands[1] = left;
        }
        else if(instruction == 2) //DCMP
        {
            const uint32_t right = (opcode >> 20u) & 0x3F;
            const uint32_t left  = (opcode >> 26u) & 0x3F;

            output->op = OPCODE_DCMP;
            output->operands[0] = right;
            output->operands[1] = left;
        }
        else //Branching
        {
            const uint32_t category = (opcode >> 6u) & 0x03u;

            if(category == 0) //Branch (BCC, BCCS, FBCC, DBCC)
            {
                const uint32_t comp  = (opcode >> 8u ) & 0x0Fu;
                const uint32_t label = (opcode >> 12u) & 0x3FFFu;

                output->op = BRUComparators[comp];
                output->operands[0] = pc + extendSign(label, 14) * 2;

                if(output->op == OPCODE_UNKNOWN)
                {
                    return 0;
                }
            }
            else if(category == 1) //Illegal
            {
                return 0;
            }
            else if(category == 2) //Jumps or calls
            {
                const uint32_t subtype = (opcode >> 8u ) & 0x03u;
                const uint32_t label   = (opcode >> 12u) & 0x3FFFu;

                output->op = BRUJumpsCalls[subtype];
                output->operands[0] = subtype > 1 ? pc + extendSign(label, 14) * 2 //relative
                                                  : label * 2u; //absolute
            }
            else //Ret
            {
                output->op = OPCODE_RET;
            }
        }
    }
    else if(type == 1) //CMPI
    {
        const uint32_t size  = (opcode >> 4u ) & 0x000003;
        const uint32_t value = (opcode >> 6u ) & 0x0FFFFF;
        const uint32_t reg   = (opcode >> 26u) & 0x00003F;

        output->op = OPCODE_CMPI;
        output->size = size;
        output->operands[0] = value;
        output->operands[1] = reg;
    }
    else if(type == 2) //FCMPI
    {
        const uint32_t value = (opcode >> 4u ) & 0x1FFFFF;
        const uint32_t reg   = (opcode >> 25u) & 0x00007F;

        output->op = OPCODE_FCMPI;
        output->operands[0] = value;
        output->operands[1] = reg;
    }
    else //DCMPI
    {
        const uint32_t value = (opcode >> 4u ) & 0x3FFFFF;
        const uint32_t reg   = (opcode >> 26u) & 0x00003F;

        output->op = OPCODE_DCMPI;
        output->operands[0] = value;
        output->operands[1] = reg;
    }

    return 1;
}

static int decodeLSU(uint32_t opcode, Operation* restrict output)
{
    const uint32_t type = (opcode >> 2u) & 0x03u;

    if(type == 0) //LDM/STM
    {
        const uint32_t incr  = (opcode >> 4u ) & 0x0001u;
        const uint32_t store = (opcode >> 5u ) & 0x0001u;
        const uint32_t size  = (opcode >> 6u ) & 0x0003u;
        const uint32_t value = (opcode >> 8u ) & 0x0FFFu;
        const uint32_t src   = (opcode >> 20u) & 0x003Fu;
        const uint32_t reg   = (opcode >> 26u) & 0x003Fu;

        output->data = incr;
        output->op   = store ? OPCODE_STM : OPCODE_LDM;
        output->size = size;
        output->operands[0] = value;
        output->operands[1] = src;
        output->operands[2] = reg;
    }
    else if(type == 1) //Subtypes
    {
        const uint32_t subtype = (opcode >> 4u) & 0x03u;

        if(subtype == 0) // LDMX/STMX
        {
            const uint32_t store = (opcode >> 6u ) & 0x0001u;
            const uint32_t size  = (opcode >> 7u ) & 0x0003u;
            const uint32_t value = (opcode >> 9u ) & 0xFFFFu;
            const uint32_t src   = (opcode >> 25u) & 0x0001u;
            const uint32_t dest  = (opcode >> 26u) & 0x003Fu;

            output->op   = store ? OPCODE_STMX : OPCODE_LDMX;
            output->size = size;
            output->operands[0] = value;
            output->operands[1] = src + 62;
            output->operands[2] = dest;
        }
        else if(subtype == 1) //IN/OUT
        {
            const uint32_t store = (opcode >> 6u ) & 0x01u;
            const uint32_t size  = (opcode >> 7u ) & 0x03u;
            const uint32_t value = (opcode >> 16u) & 0xFFu;
            const uint32_t dest  = (opcode >> 26u) & 0x3Fu;

            output->op   = store ? OPCODE_OUT : OPCODE_IN;
            output->size = size;
            output->operands[0] = value;
            output->operands[2] = dest;
        }
        else if(subtype == 2) //OUTI
        {
            const uint32_t size  = (opcode >> 7u ) & 0x0001u;
            const uint32_t value = (opcode >> 16u) & 0xFFFFu;
            const uint32_t dest  = (opcode >> 26u) & 0x00FFu;

            output->op   = OPCODE_OUTI;
            output->size = size;
            output->operands[0] = value;
            output->operands[2] = dest;
        }
        else //LDMV/STMV or LDCV/STCV
        {
            const uint32_t store = (opcode >> 6u ) & 0x0001u;
            const uint32_t cache = (opcode >> 7u ) & 0x0001u;
            const uint32_t incr  = (opcode >> 8u ) & 0x0001u;
            const uint32_t value = (opcode >> 9u ) & 0x7FFFu;
            const uint32_t src   = (opcode >> 25u) & 0x0007u;
            const uint32_t dest  = (opcode >> 26u) & 0x003Fu;

            if(cache)
            {
                output->data = incr;
                output->op = store ? OPCODE_STCV : OPCODE_LDCV;
                output->operands[0] = value;
                output->operands[1] = src + 56;
                output->operands[2] = dest;
            }
            else
            {
                output->data = incr;
                output->op = store ? OPCODE_STMV : OPCODE_LDMV;
                output->operands[0] = value;
                output->operands[1] = src + 56;
                output->operands[2] = dest;
            }
        }
    }
    else if(type == 2) //LDC/STC
    {
        const uint32_t incr  = (opcode >> 4u ) & 0x0001u;
        const uint32_t store = (opcode >> 5u ) & 0x0001u;
        const uint32_t size  = (opcode >> 6u ) & 0x0003u;
        const uint32_t value = (opcode >> 8u ) & 0x0FFFu;
        const uint32_t src   = (opcode >> 20u) & 0x003Fu;
        const uint32_t dest  = (opcode >> 26u) & 0x003Fu;

        output->data = incr;
        output->op   = store ? OPCODE_STC : OPCODE_LDC;
        output->size = size;
        output->operands[0] = value;
        output->operands[1] = src;
        output->operands[2] = dest;
    }
    else //LDMF/STMF or LDCF/STCF and LDMD/STMD or LDCD/STCD
    {
        const uint64_t size  = (opcode >> 4u) & 0x01u;
        const uint32_t cache = (opcode >> 5u) & 0x01u;
        const uint32_t store = (opcode >> 6u) & 0x01u;
        const uint32_t incr  = (opcode >> 7u) & 0x01u;

        if(size == 0) //LDMF/STMF or LDCF/STCF
        {
            const uint32_t value = (opcode >> 8u ) & 0x7FFFu;
            const uint32_t src   = (opcode >> 23u) & 0x0003u;
            const uint32_t dest  = (opcode >> 25u) & 0x007Fu;

            if(cache)
            {
                output->data = incr;
                output->op = store ? OPCODE_STCF : OPCODE_LDCF;
                output->operands[0] = value;
                output->operands[1] = src + 60;
                output->operands[2] = dest;
            }
            else
            {
                output->data = incr;
                output->op = store ? OPCODE_STMF : OPCODE_LDMF;
                output->operands[0] = value;
                output->operands[1] = src + 60;
                output->operands[2] = dest;
            }
        }
        else
        {
            const uint32_t value = (opcode >> 8u ) & 0xFFFFu;
            const uint32_t src   = (opcode >> 24u) & 0x0003u;
            const uint32_t dest  = (opcode >> 26u) & 0x003Fu;

            if(cache)
            {
                output->data = incr;
                output->op = store ? OPCODE_STCD : OPCODE_LDCD;
                output->operands[0] = value;
                output->operands[1] = src + 60;
                output->operands[2] = dest;
            }
            else
            {
                output->data = incr;
                output->op = store ? OPCODE_STMD : OPCODE_LDMD;
                output->operands[0] = value;
                output->operands[1] = src + 60;
                output->operands[2] = dest;
            }
        }
    }

    return 1;
}

static const Opcode ALURegRegRegOpcodes[16] =
{
    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_MULS,
    OPCODE_MULU,
    OPCODE_DIVS,
    OPCODE_DIVU,
    OPCODE_AND,
    OPCODE_OR,
    OPCODE_XOR,
    OPCODE_ASL,
    OPCODE_LSL,
    OPCODE_ASR,
    OPCODE_LSR,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
};

static const Opcode ALURegRegImmOpcodes[16] =
{
    OPCODE_ADDI,
    OPCODE_SUBI,
    OPCODE_MULSI,
    OPCODE_MULUI,
    OPCODE_DIVSI,
    OPCODE_DIVUI,
    OPCODE_ANDI,
    OPCODE_ORI,
    OPCODE_XORI,
    OPCODE_ASLI,
    OPCODE_LSLI,
    OPCODE_ASRI,
    OPCODE_LSRI,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
};

static const Opcode ALURegImmOpcodes[16] =
{
    OPCODE_ADDQ,
    OPCODE_SUBQ,
    OPCODE_MULSQ,
    OPCODE_MULUQ,
    OPCODE_DIVSQ,
    OPCODE_DIVUQ,
    OPCODE_ANDQ,
    OPCODE_ORQ,
    OPCODE_XORQ,
    OPCODE_ASLQ,
    OPCODE_LSLQ,
    OPCODE_ASRQ,
    OPCODE_LSRQ,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
    OPCODE_UNKNOWN,
};

static int decodeALU(uint32_t opcode, Operation* restrict output)
{
    const uint32_t category = (opcode >> 2u) & 0x03u;

    if(category == 0)
    {
        const uint32_t type = (opcode >> 4u) & 0x07u;

        if(type == 0) // ALU REG-REG-REG (ADD, SUB, ...)
        {
            const uint32_t op   = (opcode >> 8u ) & 0x0Fu;
            const uint32_t size = (opcode >> 12u) & 0x03u;
            const uint32_t src1 = (opcode >> 14u) & 0x3Fu;
            const uint32_t src2 = (opcode >> 20u) & 0x3Fu;
            const uint32_t dest = (opcode >> 26u) & 0x3Fu;

            output->op   = ALURegRegRegOpcodes[op];
            output->size = size;
            output->operands[0] = src1;
            output->operands[1] = src2;
            output->operands[2] = dest;

            if(output->op == OPCODE_UNKNOWN)
            {
                return 0;
            }
        }
        else if(type == 2) // XCHG
        {
            output->op = OPCODE_XCHG;
        }
        else if(type == 6) // NOP NOP.E
        {
            const uint32_t end = (opcode >> 7u) & 0x01u;

            output->data = end;
            output->op   = OPCODE_NOP;
        }
        else
        {
            return 0;
        }
    }
    else if(category == 1) // ALU REG-REG-IMM (ADDI, SUBI, ...)
    {
        const uint32_t op    = (opcode >> 4u ) & 0x000Fu;
        const uint32_t size  = (opcode >> 8u ) & 0x0003u;
        const uint32_t value = (opcode >> 10u) & 0x03FFu;
        const uint32_t src   = (opcode >> 20u) & 0x003Fu;
        const uint32_t dest  = (opcode >> 26u) & 0x003Fu;

        output->op   = ALURegRegImmOpcodes[op];
        output->size = size;
        output->operands[0] = value;
        output->operands[1] = src;
        output->operands[2] = dest;

        if(output->op == OPCODE_UNKNOWN)
        {
            return 0;
        }
    }
    else if(category == 2) // ALU REG-IMM (ADDQ, SUBQ, ...)
    {
        const uint32_t op    = (opcode >> 4u ) & 0x000Fu;
        const uint32_t size  = (opcode >> 8u ) & 0x0003u;
        const uint32_t value = (opcode >> 10u) & 0xFFFFu;
        const uint32_t dest  = (opcode >> 26u) & 0x003Fu;

        output->op   = ALURegImmOpcodes[op];
        output->size = size;
        output->operands[0] = value;
        output->operands[2] = dest;

        if(output->op == OPCODE_UNKNOWN)
        {
            return 0;
        }
    }
    else // MOVEI
    {
        const uint32_t value = (opcode >> 4u ) & 0x3FFFFFu;
        const uint32_t dest  = (opcode >> 26u) & 0x00003Fu;

        output->op = OPCODE_MOVEI;
        output->operands[0] = value;
        output->operands[2] = dest;
    }

    return 1;
}

static int decodeAGU(uint32_t opcode, Operation* restrict output)
{
    const uint32_t category = (opcode >> 2u) & 0x01u;

    if(category == 0) //LDDMA/STDMA
    {
        const uint32_t store = (opcode >> 3u ) & 0x0001u;
        const uint32_t size  = (opcode >> 4u ) & 0x0001u;
        const uint32_t sram  = (opcode >> 5u ) & 0x0003u;
        const uint32_t ram   = (opcode >> 7u ) & 0x0001u;
        const uint32_t sramb = (opcode >> 8u ) & 0x0FFFu;
        const uint32_t ramb  = (opcode >> 20u) & 0x0FFFu;

        output->op = store ? OPCODE_STDMA : OPCODE_LDDMA;
        output->size = size;
        output->operands[0] = sram + 60;
        output->operands[1] = ram + 58;
        output->data = (ramb << 12u) | sramb;
    }
    else //Load/store list
    {
        const uint32_t store = (opcode >> 3u ) & 0x01u;
        const uint32_t type  = (opcode >> 4u ) & 0x0Fu;

        if(type == 0) //LDDMAR/STDMAR
        {
            const uint32_t size = (opcode >> 8u ) & 0x0FFFu;
            const uint32_t ram  = (opcode >> 8u ) & 0x003Fu;
            const uint32_t sram = (opcode >> 20u) & 0x003Fu;

            output->op = store ? OPCODE_STDMAR : OPCODE_LDDMAR;
            output->size = size;
            output->operands[0] = ram;
            output->operands[1] = sram;
        }
        else if(type == 1) //DMAIR
        {
            const uint32_t size = (opcode >> 8u ) & 0x0FFFu;
            const uint32_t ram  = (opcode >> 8u ) & 0x003Fu;
            const uint32_t sram = (opcode >> 20u) & 0x003Fu;

            output->op = OPCODE_DMAIR;
            output->size = size;
            output->operands[0] = ram;
            output->operands[1] = sram;
        }
        else if(type == 15) //WAIT
        {
            output->op = OPCODE_WAIT;
        }
        else
        {
            return 0;
        }
    }

    return 1;
}

static int decodeVFPU(uint32_t opcode, Operation* restrict output)
{
    (void)opcode;
    (void)output;

    return 1;
}

static int decode(uint32_t index, uint32_t pc, uint32_t opcode, Operation* restrict output)
{
#ifndef NDEBUG
    memset(output, 0, sizeof(Operation)); //simplify debug
#endif

    const uint32_t compute_unit = opcode & 0x03;

    if(index == 0)
    {
        if(compute_unit == 0) //BRU
        {
            return decodeBRU(pc, opcode, output);
        }
        else if(compute_unit == 1) //LSU
        {
            return decodeLSU(opcode, output);
        }
        else if(compute_unit == 2) //ALU
        {
            return decodeALU(opcode, output);
        }
        else //VFPU/VDIV
        {
            return decodeVFPU(opcode, output);
        }
    }
    else if(index == 1)
    {
        if(compute_unit == 0) //AGU
        {
            return decodeAGU(opcode, output);
        }
        else if(compute_unit == 1) //LSU
        {
            return decodeLSU(opcode, output);
        }
        else if(compute_unit == 2) //ALU
        {
            return decodeALU(opcode, output);
        }
        else //VFPU
        {
            return decodeVFPU(opcode, output);
        }
    }
    else //2 or 3
    {
        if(compute_unit == 2) //ALU
        {
            return decodeALU(opcode, output);
        }
        else //VFPU
        {
            return 0;
        }
    }
}

static uint32_t opcodeSetSize(ArProcessor restrict processor)
{
    uint32_t size;
    if(processor->flags & 0x01)
    {
        const uint32_t available = processor->pc - (ISRAM_SIZE / 4u); //we may overflow otherwise
        size = MIN(available, 4u);
    }
    else
    {
        size = 2;
    }

    return size;
}

ArResult arDecodeInstruction(ArProcessor processor)
{
    assert(processor);

    const uint32_t size = opcodeSetSize(processor);
    memcpy(processor->opcodes, processor->isram + (processor->pc * 4), size * sizeof(uint32_t));

    for(uint32_t i = 0; i < size; ++i)
    {
        if(!decode(i, processor->pc, processor->opcodes[i], &processor->operations[i]))
        {
            return AR_ERROR_ILLEGAL_INSTRUCTION;
        }
    }

    processor->pc += size;

    return AR_SUCCESS;
}
