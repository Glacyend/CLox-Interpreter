#include <stdio.h>

#include "debug.h"
#include "object.h"
#include "value.h"

void disassemble_chunk(Chunk* chunk, const char* name) {
    printf("===== %s =====\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassemble_instruction(chunk, offset);
    }
}

static int constant_instruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d `", name, constant);
    print_value(chunk->constants.values[constant]);
    printf("`\n");
    return offset + 2;
}

static int simple_instruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byte_instruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int jump_instruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

static int closure_instruction(const char* name, Chunk* chunk, int offset) {
    offset++;
    uint8_t constant = chunk->code[offset++];
    printf("%-16s %4d ", name, constant);
    print_value(chunk->constants.values[constant]);
    printf("\n");

    ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
    for (int j = 0; j < function->upvalue_count; j++) {
        int is_local = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d      |                    %s %d\n", offset - 2, is_local ? "local" : "upvalue", index);
    }

    return offset;
}

int disassemble_instruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OpConstant:
            return constant_instruction("Constant", chunk, offset);
        case OpNil:
            return simple_instruction("Nil", offset);
        case OpTrue:
            return simple_instruction("True", offset);
        case OpFalse:
            return simple_instruction("False", offset);
        case OpPop:
            return simple_instruction("Pop", offset);
        case OpGetLocal:
            return byte_instruction("GetLocal", chunk, offset);
        case OpSetLocal:
            return byte_instruction("SetLocal", chunk, offset);
        case OpGetGlobal:
            return constant_instruction("GetGlobal", chunk, offset);
        case OpDefineGlobal:
            return constant_instruction("DefineGlobal", chunk, offset);
        case OpSetGlobal:
            return constant_instruction("SetGlobal", chunk, offset);
        case OpGetUpvalue:
            return byte_instruction("GetUpvalue", chunk, offset);
        case OpSetUpvalue:
            return byte_instruction("SetUpvalue", chunk, offset);
        case OpEqual:
            return simple_instruction("Equal", offset);
        case OpGreater:
            return simple_instruction("Greater", offset);
        case OpLess:
            return simple_instruction("Less", offset);
        case OpAdd:
            return simple_instruction("Add", offset);
        case OpSubtract:
            return simple_instruction("Subtract", offset);
        case OpMultiply:
            return simple_instruction("Multiply", offset);
        case OpDivide:
            return simple_instruction("Devide", offset);
        case OpNot:
            return simple_instruction("Not", offset);
        case OpNegate:
            return simple_instruction("Negate", offset);
        case OpPrint:
            return simple_instruction("Print", offset);
        case OpJump:
            return jump_instruction("Jump", 1, chunk, offset);
        case OpJumpIfFalse:
            return jump_instruction("JumpIfFalse", 1, chunk, offset);
        case OpLoop:
            return jump_instruction("Loop", -1, chunk, offset);
        case OpCall:
            return byte_instruction("Call", chunk, offset);
        case OpClosure:
            return closure_instruction("Closure", chunk, offset);
        case OpCloseUpvalue:
            return simple_instruction("CloseUpvalue", offset);
        case OpReturn:
            return simple_instruction("Return", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
