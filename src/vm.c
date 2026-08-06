#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static void runtime_error(const char* format, ...);

static Value clock_native(int arg_count, [[maybe_unused]] Value* args, bool* no_error) {
    if (arg_count != 0) {
        *no_error = false;
        runtime_error("<native fn `clock`> Expected 0 arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    *no_error = true;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value readline_native(int arg_count, [[maybe_unused]] Value* args, bool* no_error) {
    if (arg_count != 0) {
        *no_error = false;
        runtime_error("<native fn `readline`> Exepcted 0 arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    char* line = ALLOCATE(char, 1);
    int capacity = 1;
    int length = 0;

    char ch = getchar();
    while (ch != '\n') {
        if (length + 1 > capacity) {
            int new_capacity = GROW_CAPACITY(capacity);
            line = GROW_ARRAY(char, line, capacity, new_capacity);
            capacity = new_capacity;
        }
        line[length++] = ch;
        ch = getchar();
    }

    char* result = ALLOCATE(char, length + 1);
    memcpy(result, line, length);
    result[length] = '\0';
    FREE_ARRAY(char, line, capacity);

    *no_error = true;
    return OBJ_VAL(take_string(result, length));
}

static void reset_stack() {
    vm.stack_top = vm.stack;
    vm.frame_count = 0;
    vm.open_upvalues = NULL;
}

static void runtime_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    reset_stack();
}

static void define_native(const char* name, NativeFn function) {
    push(OBJ_VAL(copy_string(name, (int)strlen(name))));
    push(OBJ_VAL(new_native(function, name)));
    table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void init_vm() {
    reset_stack();
    vm.objects = NULL;
    init_table(&vm.globals);
    init_table(&vm.strings);

    define_native("clock", clock_native);
    define_native("readline", readline_native);
}

void free_vm() {
    free_table(&vm.globals);
    free_table(&vm.strings);
    free_objects();
}

void push(Value value) {
    *vm.stack_top = value;
    vm.stack_top++;
}

Value pop() {
    vm.stack_top--;
    return *vm.stack_top;
}

static Value peek(int distance) {
    return vm.stack_top[-1 - distance];
}

static bool call(ObjClosure* closure, int arg_count) {
    if (arg_count != closure->function->arity) {
        runtime_error("Expected %d arguments but got %d.", closure->function->arity, arg_count);
        return false;
    }

    if (vm.frame_count == FRAMES_MAX) {
        runtime_error("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack_top - arg_count - 1;
    return true;
}

static bool call_value(Value callee, int arg_count) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case ObjTyClosure: {
                return call(AS_CLOSURE(callee), arg_count);
            }
            case ObjTyNative: {
                NativeFn native = AS_NATIVE(callee)->function;
                bool no_error;
                Value result = native(arg_count, vm.stack_top - arg_count, &no_error);
                vm.stack_top -= arg_count + 1;
                push(result);
                return no_error;
            }
            default: {
                break;  // Non-callable object type.
            }
        }
    }
    runtime_error("Can only call functions and classes.");
    return false;
}

static ObjUpvalue* capture_upvalue(Value* local) {
    ObjUpvalue* prev_upvalue = NULL;
    ObjUpvalue* upvalue = vm.open_upvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prev_upvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* created_upvalue = new_upvalue(local);
    created_upvalue->next = NULL;

    if (prev_upvalue == NULL) {
        vm.open_upvalues = created_upvalue;
    } else {
        prev_upvalue->next = created_upvalue;
    }

    return created_upvalue;
}

static void close_upvalues(Value* last) {
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
        ObjUpvalue* upvalue = vm.open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.open_upvalues = upvalue->next;
    }
}

static bool is_falsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = take_string(chars, length);
    push(OBJ_VAL(result));
}

static InterpretResult run() {
    CallFrame* frame = &vm.frames[vm.frame_count - 1];

    #define READ_BYTE() (*frame->ip++)
    #define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
    #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define BINARY_OP(value_type, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtime_error("Operands must be numbers."); \
                return InterpretRuntimeError; \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(value_type(a op b)); \
        } while (false)

    while (true) {
        #ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stack_top; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
        #endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OpConstant: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OpNil: {
                push(NIL_VAL);
                break;
            }
            case OpTrue: {
                push(BOOL_VAL(true));
                break;
            }
            case OpFalse: {
                push(BOOL_VAL(false));
                break;
            }
            case OpPop: {
                pop();
                break;
            }
            case OpGetLocal: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OpSetLocal: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OpGetGlobal: {
                ObjString* name = READ_STRING();
                Value value;
                if (!table_get(&vm.globals, name, &value)) {
                    runtime_error("Undefined variable `%s`.", name->chars);
                    return InterpretRuntimeError;
                }
                push(value);
                break;
            }
            case OpDefineGlobal: {
                ObjString* name = READ_STRING();
                table_set(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OpSetGlobal: {
                ObjString* name = READ_STRING();
                if (table_set(&vm.globals, name, peek(0))) {
                    table_delete(&vm.globals, name);
                    runtime_error("Undefined variable `%s`.", name->chars);
                    return InterpretRuntimeError;
                }
                break;
            }
            case OpGetUpvalue: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OpSetUpvalue: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OpEqual: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(values_equal(a, b)));
                break;
            }
            case OpGreater: {
                BINARY_OP(BOOL_VAL, >);
                break;
            }
            case OpLess: {
                BINARY_OP(BOOL_VAL, <);
                break;
            }
            case OpAdd: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtime_error("Operands must be two numbers or two strings.");
                    return InterpretRuntimeError;
                }
                break;
            }
            case OpSubtract: {
                BINARY_OP(NUMBER_VAL, -);
                break;
            }
            case OpMultiply: {
                BINARY_OP(NUMBER_VAL, *);
                break;
            }
            case OpDivide: {
                BINARY_OP(NUMBER_VAL, /);
                break;
            }
            case OpNot: {
                push(BOOL_VAL(is_falsey(pop())));
                break;
            }
            case OpNegate: {
                if (!IS_NUMBER(peek(0))) {
                    runtime_error("Operand must be a number.");
                    return InterpretRuntimeError;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            }
            case OpPrint: {
                print_value(pop());
                printf("\n");
                break;
            }
            case OpJump: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OpJumpIfFalse: {
                uint16_t offset = READ_SHORT();
                if (is_falsey(peek(0))) {
                    frame->ip += offset;
                }
                break;
            }
            case OpLoop: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OpCall: {
                int arg_count = READ_BYTE();
                if (!call_value(peek(arg_count), arg_count)) {
                    return InterpretRuntimeError;
                }
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OpClosure: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = new_closure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalue_count; i++) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (is_local) {
                        closure->upvalues[i] = capture_upvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OpCloseUpvalue: {
                close_upvalues(vm.stack_top - 1);
                pop();
                break;
            }
            case OpReturn: {
                Value result = pop();
                close_upvalues(frame->slots);
                vm.frame_count--;
                if (vm.frame_count == 0) {
                    pop();
                    return InterpretOk;
                }
                vm.stack_top = frame->slots;
                push(result);
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
        }
    }

    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) {
        return InterpretCompileError;
    }

    push(OBJ_VAL(function));
    ObjClosure* closure = new_closure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}
