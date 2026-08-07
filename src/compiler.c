#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
} Parser;

typedef enum {
    PrecNone,
    PrecAssignment,  // =
    PrecOr,  // or
    PrecAnd,  // and
    PrecEquality, // == !=
    PrecComparison,  // < > <= >=
    PrecTerm,  // + -
    PrecFactor,  // * /
    PrecUnary,  // ! -
    PrecCall,  // . ()
    PrecPrimary,
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
    bool is_captured;
} Local;

typedef struct {
    uint8_t index;
    bool is_local;
} Upvalue;

typedef enum {
    TypeFunction,
    TypeInitializer,
    TypeMethod,
    TypeScript,
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int local_count;
    Upvalue upvalues[UINT8_COUNT];
    int scope_depth;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* current_class = NULL;

static Chunk* current_chunk() {
    return &current->function->chunk;
}

static void error_at(Token* token, const char* message) {\
    if (parser.panic_mode) {
        return;
    }
    parser.panic_mode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TokenEOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TokenError) {
        // Nothing.
    } else {
        fprintf(stderr, " at `%.*s`", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.had_error = true;
}

static void error(const char* message) {
    error_at(&parser.previous, message);
}

static void error_at_current(const char* message) {
    error_at(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current;

    while (true) {
        parser.current = scan_token();
        if (parser.current.type != TokenError) {
            break;
        }

        error_at_current(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    error_at_current(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

static void emit_byte(uint8_t byte) {
    write_chunk(current_chunk(), byte, parser.previous.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

static void emit_loop(int loop_start) {
    emit_byte(OpLoop);

    int offset = current_chunk()->count - loop_start + 2;
    if (offset > UINT16_MAX) {
        error("Loop body too large.");
    }

    emit_byte((offset >> 8) & 0xFF);
    emit_byte(offset & 0xFF);
}

static int emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    emit_bytes(0xFF, 0xFF);
    return current_chunk()->count - 2;
}

static void emit_return() {
    if (current->type == TypeInitializer) {
        emit_bytes(OpGetLocal, 0);
    } else {
        emit_byte(OpNil);
    }
    emit_byte(OpReturn);
}

static uint8_t make_constant(Value value) {
    int constant = add_constant(current_chunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emit_constant(Value value) {
    emit_bytes(OpConstant, make_constant(value));
}

static void patch_jump(int offset) {
    int jump = current_chunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    current_chunk()->code[offset] = (jump >> 8) & 0xFF;
    current_chunk()->code[offset + 1] = jump & 0xFF;
}

static void init_compiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = new_function();
    current = compiler;
    if (type != TypeScript) {
        current->function->name = copy_string(parser.previous.start, parser.previous.length);
    }

    Local* local = &current->locals[current->local_count++];
    local->depth = 0;
    local->is_captured = false;
    if (type != TypeFunction) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* end_compiler() {
    emit_return();
    ObjFunction* function = current->function;

    #ifdef DEBUG_PRINT_CODE
    if (!parser.had_error) {
        disassemble_chunk(current_chunk(), function->name != NULL ? function->name->chars : "script");
    }
    #endif

    current = current->enclosing;
    return function;
}

static void begin_scope() {
    current->scope_depth++;
}

static void end_scope() {
    current->scope_depth--;

    while (current->local_count > 0 && current->locals[current->local_count - 1].depth > current->scope_depth) {
        if (current->locals[current->local_count - 1].is_captured) {
            emit_byte(OpCloseUpvalue);
        } else {
            emit_byte(OpPop);
        }
        current->local_count--;
    }
}

static void expression();
static void statement();
static void declaration();
static ParseRule* get_rule(TokenType type);
static void parse_precedence(Precedence precedence);

static uint8_t identifier_constant(Token* name) {
    return make_constant(OBJ_VAL(copy_string(name->start, name->length)));
}

static bool identifiers_equal(Token* a, Token* b) {
    if (a->length != b->length) {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler* compiler, Token* name) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int add_upvalue(Compiler* compiler, uint8_t index, bool is_local) {
    int upvalue_count = compiler->function->upvalue_count;

    for (int i = 0; i < upvalue_count; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local) {
            return i;
        }
    }

    if (upvalue_count == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->function->upvalue_count++;
}

static int resolve_upvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) {
        return -1;
    }

    int local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void add_local(Token name) {
    if (current->local_count == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->local_count++];
    local->name = name;
    local->depth = -1;
    local->is_captured = false;
}

static void declare_variable() {
    if (current->scope_depth == 0) {
        return;
    }

    Token* name = &parser.previous;
    for (int i = current->local_count - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth) {
            break;
        }
        if (identifiers_equal(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    add_local(*name);
}

static uint8_t parse_variable(const char* error_message) {
    consume(TokenIdentifier, error_message);

    declare_variable();
    if (current->scope_depth > 0) {
        return 0;
    }

    return identifier_constant(&parser.previous);
}

static void mark_initialized() {
    if (current->scope_depth == 0) {
        return;
    }
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global) {
    if (current->scope_depth > 0) {
        mark_initialized();
        return;
    }

    emit_bytes(OpDefineGlobal, global);
}

static uint8_t argument_list() {
    uint8_t arg_count = 0;
    if (!check(TokenRightParen)) {
        do {
            expression();
            if (arg_count == 255) {
                error("Can't have more than 255 arguments.");
            }
            arg_count++;
        } while (match(TokenComma));
    }
    consume(TokenRightParen, "Expect `)` after arguments.");
    return arg_count;
}

static void and_op([[maybe_unused]] bool can_assign) {
    int end_jump = emit_jump(OpJumpIfFalse);

    emit_byte(OpPop);
    parse_precedence(PrecAnd);

    patch_jump(end_jump);
}

static void binary([[maybe_unused]] bool can_assign) {
    TokenType operator_type = parser.previous.type;
    ParseRule* rule = get_rule(operator_type);
    parse_precedence((Precedence)(rule->precedence + 1));

    switch (operator_type) {
        case TokenBangEqual: {
            emit_bytes(OpEqual, OpNot);
            break;
        }
        case TokenEqualEqual: {
            emit_byte(OpEqual);
            break;
        }
        case TokenGreater: {
            emit_byte(OpGreater);
            break;
        }
        case TokenGreaterEqual: {
            emit_bytes(OpLess, OpNot);
            break;
        }
        case TokenLess: {
            emit_byte(OpLess);
            break;
        }
        case TokenLessEqual: {
            emit_bytes(OpGreater, OpNot);
            break;
        }
        case TokenPlus: {
            emit_byte(OpAdd);
            break;
        }
        case TokenMinus: {
            emit_byte(OpSubtract);
            break;
        }
        case TokenStar: {
            emit_byte(OpMultiply);
            break;
        }
        case TokenSlash: {
            emit_byte(OpDivide);
            break;
        }
        default: {
            return;
        }
    }
}

static void call([[maybe_unused]] bool can_assign) {
    uint8_t arg_count = argument_list();
    emit_bytes(OpCall, arg_count);
}

static void dot(bool can_assign) {
    consume(TokenIdentifier, "Expect property name after `.`.");
    uint8_t name = identifier_constant(&parser.previous);

    if (can_assign && match(TokenEqual)) {
        expression();
        emit_bytes(OpSetProperty, name);
    } else if (match(TokenLeftParen)) {
        uint8_t arg_count = argument_list();
        emit_bytes(OpInvoke, name);
        emit_byte(arg_count);
    } else {
        emit_bytes(OpGetProperty, name);
    }
}

static void literal([[maybe_unused]] bool can_assign) {
    switch (parser.previous.type) {
        case TokenFalse: {
            emit_byte(OpFalse);
            break;
        }
        case TokenNil: {
            emit_byte(OpNil);
            break;
        }
        case TokenTrue: {
            emit_byte(OpTrue);
            break;
        }
        default: {  // Unreachable.
            return;
        }
    }
}

static void grouping([[maybe_unused]] bool can_assign) {
    expression();
    consume(TokenRightParen, "Expect `)` after expression.");
}

static void number([[maybe_unused]] bool can_assign) {
    double value = strtod(parser.previous.start, NULL);
    emit_constant(NUMBER_VAL(value));
}

static void or_op([[maybe_unused]] bool can_assign) {
    int else_jump = emit_jump(OpJumpIfFalse);
    int end_jump = emit_jump(OpJump);

    patch_jump(else_jump);
    emit_byte(OpPop);

    parse_precedence(PrecOr);
    patch_jump(end_jump);
}

static void string([[maybe_unused]] bool can_assign) {
    emit_constant(OBJ_VAL(copy_string(parser.previous.start + 1, parser.previous.length - 2)));
}

static void named_variable(Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);
    if (arg != -1) {
        get_op = OpGetLocal;
        set_op = OpSetLocal;
    } else if ((arg = resolve_upvalue(current, &name)) != -1) {
        get_op = OpGetUpvalue;
        set_op = OpSetUpvalue;
    } else {
        arg = identifier_constant(&name);
        get_op = OpGetGlobal;
        set_op = OpSetGlobal;
    }

    if (can_assign && match(TokenEqual)) {
        expression();
        emit_bytes(set_op, (uint8_t)arg);
    } else {
        emit_bytes(get_op, (uint8_t)arg);
    }
}

static void variable(bool can_assign) {
    named_variable(parser.previous, can_assign);
}

static void this_var([[maybe_unused]] bool can_assign) {
    if (current_class == NULL) {
        error("Can't use `this` outside of a class.");
    }

    variable(false);
}

static void unary([[maybe_unused]] bool can_assign) {
    TokenType operator_type = parser.previous.type;

    parse_precedence(PrecUnary);

    switch (operator_type) {
        case TokenBang: {
            emit_byte(OpNot);
            break;
        }
        case TokenMinus: {
            emit_byte(OpNegate);
            break;
        }
        default: {
            return;
        }
    }
}

ParseRule rules[] = {
    [TokenLeftParen]    = { grouping, call,   PrecCall       },
    [TokenRightParen]   = { NULL,     NULL,   PrecNone       },
    [TokenLeftBrace]    = { NULL,     NULL,   PrecNone       },
    [TokenRightBrace]   = { NULL,     NULL,   PrecNone       },
    [TokenComma]        = { NULL,     NULL,   PrecNone       },
    [TokenDot]          = { NULL,     dot,    PrecCall       },
    [TokenMinus]        = { unary,    binary, PrecTerm       },
    [TokenPlus]         = { NULL,     binary, PrecTerm       },
    [TokenSemicolon]    = { NULL,     NULL,   PrecNone       },
    [TokenSlash]        = { NULL,     binary, PrecFactor     },
    [TokenStar]         = { NULL,     binary, PrecFactor     },
    [TokenBang]         = { unary,    NULL,   PrecNone       },
    [TokenBangEqual]    = { NULL,     binary, PrecEquality   },
    [TokenEqual]        = { NULL,     NULL,   PrecNone       },
    [TokenEqualEqual]   = { NULL,     binary, PrecEquality   },
    [TokenGreater]      = { NULL,     binary, PrecComparison },
    [TokenGreaterEqual] = { NULL,     binary, PrecComparison },
    [TokenLess]         = { NULL,     binary, PrecComparison },
    [TokenLessEqual]    = { NULL,     binary, PrecComparison },
    [TokenIdentifier]   = { variable, NULL,   PrecNone       },
    [TokenString]       = { string,   NULL,   PrecNone       },
    [TokenNumber]       = { number,   NULL,   PrecNone       },
    [TokenAnd]          = { NULL,     and_op, PrecAnd        },
    [TokenClass]        = { NULL,     NULL,   PrecNone       },
    [TokenElse]         = { NULL,     NULL,   PrecNone       },
    [TokenFalse]        = { literal,  NULL,   PrecNone       },
    [TokenFor]          = { NULL,     NULL,   PrecNone       },
    [TokenFun]          = { NULL,     NULL,   PrecNone       },
    [TokenIf]           = { NULL,     NULL,   PrecNone       },
    [TokenNil]          = { literal,  NULL,   PrecNone       },
    [TokenOr]           = { NULL,     or_op,  PrecOr         },
    [TokenPrint]        = { NULL,     NULL,   PrecNone       },
    [TokenReturn]       = { NULL,     NULL,   PrecNone       },
    [TokenSuper]        = { NULL,     NULL,   PrecNone       },
    [TokenThis]         = { this_var, NULL,   PrecNone       },
    [TokenTrue]         = { literal,  NULL,   PrecNone       },
    [TokenVar]          = { NULL,     NULL,   PrecNone       },
    [TokenWhile]        = { NULL,     NULL,   PrecNone       },
    [TokenError]        = { NULL,     NULL,   PrecNone       },
    [TokenEOF]          = { NULL,     NULL,   PrecNone       },
};

static void parse_precedence(Precedence precedence) {
    advance();
    ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;
    if (prefix_rule == NULL) {
        error("Expect expression.");
        return;
    }

    bool can_assign = precedence <= PrecAssignment;
    prefix_rule(can_assign);

    while (precedence <= get_rule(parser.current.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(parser.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TokenEqual)) {
        error("Invalid assignment target.");
    }
}

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parse_precedence(PrecAssignment);
}

static void block() {
    while (!check(TokenRightBrace) && !check(TokenEOF)) {
        declaration();
    }

    consume(TokenRightBrace, "Expect `}` after block.");
}

static void function(FunctionType type) {
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();

    consume(TokenLeftParen, "Expect `(` after function name.");
    if (!check(TokenRightParen)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                error_at_current("Can't have more than 255 parameters.");
            }
            uint8_t constant = parse_variable("Expect parameter name.");
            define_variable(constant);
        } while (match(TokenComma));
    }
    consume(TokenRightParen, "Expect `)` after parameters.");
    consume(TokenLeftBrace, "Expect `{` before function body.");
    block();

    ObjFunction* function = end_compiler();
    emit_bytes(OpClosure, make_constant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalue_count; i++) {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void method() {
    consume(TokenIdentifier, "Expect method name.");
    uint8_t constant = identifier_constant(&parser.previous);

    FunctionType type = TypeMethod;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
        type = TypeInitializer;
    }

    function(type);
    emit_bytes(OpMethod, constant);
}

static void class_declaration() {
    consume(TokenIdentifier, "Expect class name.");
    Token class_name = parser.previous;
    uint8_t name_constant = identifier_constant(&parser.previous);
    declare_variable();

    emit_bytes(OpClass, name_constant);
    define_variable(name_constant);

    ClassCompiler class_compiler;
    class_compiler.enclosing = current_class;
    current_class = &class_compiler;

    named_variable(class_name, false);
    consume(TokenLeftBrace, "Expect `{` before class body.");
    while (!check(TokenRightBrace) && !check(TokenEOF)) {
        method();
    }
    consume(TokenRightBrace, "Expect `}` after class body.");
    emit_byte(OpPop);

    current_class = current_class->enclosing;
}

static void fun_declaration() {
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized();
    function(TypeFunction);
    define_variable(global);
}

static void var_declaration() {
    uint8_t global = parse_variable("Expect variable name.");

    if (match(TokenEqual)) {
        expression();
    } else {
        emit_byte(OpNil);
    }
    consume(TokenSemicolon, "Expect `;` after variable declaration.");

    define_variable(global);
}

static void expression_statement() {
    expression();
    consume(TokenSemicolon, "Expect `;` after expression.");
    emit_byte(OpPop);
}

static void for_statement() {
    begin_scope();

    consume(TokenLeftParen, "Expect `(` after `for`.");
    if (match(TokenSemicolon)) {
        // No initializer.
    } else if (match(TokenVar)) {
        var_declaration();
    } else {
        expression_statement();
    }

    int loop_start = current_chunk()->count;
    int exit_jump = -1;
    if (!match(TokenSemicolon)) {
        expression();
        consume(TokenSemicolon, "Expect `;` after loop condition.");

        exit_jump = emit_jump(OpJumpIfFalse);
        emit_byte(OpPop);
    }

    if (!match(TokenRightParen)) {
        int body_jump = emit_jump(OpJump);
        int increment_start = current_chunk()->count;
        expression();
        emit_byte(OpPop);
        consume(TokenRightParen, "Expect `)` after for clauses.");

        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OpPop);
    }

    end_scope();
}

static void if_statement() {
    consume(TokenLeftParen, "Expect `(` after `if`.");
    expression();
    consume(TokenRightParen, "Expect `)` after condition.");

    int then_jump = emit_jump(OpJumpIfFalse);
    emit_byte(OpPop);
    statement();

    int else_jump = emit_jump(OpJump);

    patch_jump(then_jump);
    emit_byte(OpPop);

    if (match(TokenElse)) {
        statement();
    }
    patch_jump(else_jump);
}

static void print_statement() {
    expression();
    consume(TokenSemicolon, "Expect `;` after value.");
    emit_byte(OpPrint);
}

static void return_statement() {
    if (current->type == TypeScript) {
        error("Can't return from top-level code.");
    }

    if (match(TokenSemicolon)) {
        emit_return();
    } else {
        if (current->type == TypeInitializer) {
            error("Can't return a value from an initializer.");
        }

        expression();
        consume(TokenSemicolon, "Expect `;` after return value.");
        emit_byte(OpReturn);
    }
}

static void while_statement() {
    int loop_start = current_chunk()->count;
    consume(TokenLeftParen, "Expect `(` after `while`.");
    expression();
    consume(TokenRightParen, "Expect `)` after condition.");

    int exit_jump = emit_jump(OpJumpIfFalse);
    emit_byte(OpPop);
    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OpPop);
}

static void synchronize() {
    parser.panic_mode = false;

    while (parser.current.type != TokenEOF) {
        if (parser.previous.type == TokenSemicolon) {
            return;
        }
        switch (parser.current.type) {
            case TokenClass:
            case TokenFun:
            case TokenVar:
            case TokenFor:
            case TokenIf:
            case TokenWhile:
            case TokenPrint:
            case TokenReturn:
                return;
            default:
                // Do nothing.
                break;
        }
        advance();
    }
}

static void declaration() {
    if (match(TokenClass)) {
        class_declaration();
    } else if (match(TokenFun)) {
        fun_declaration();
    } else if (match(TokenVar)) {
        var_declaration();
    } else {
        statement();
    }

    if (parser.panic_mode) {
        synchronize();
    }
}

static void statement() {
    if (match(TokenPrint)) {
        print_statement();
    } else if (match(TokenFor)) {
        for_statement();
    } else if (match(TokenIf)) {
        if_statement();
    } else if (match(TokenReturn)) {
        return_statement();
    } else if (match(TokenWhile)) {
        while_statement();
    } else if (match(TokenLeftBrace)) {
        begin_scope();
        block();
        end_scope();
    } else {
        expression_statement();
    }
}

ObjFunction* compile(const char* source) {
    init_scanner(source);
    Compiler compiler;
    init_compiler(&compiler, TypeScript);

    parser.had_error = false;
    parser.panic_mode = false;

    advance();

    while (!match(TokenEOF)) {
        declaration();
    }

    ObjFunction* function = end_compiler();
    return parser.had_error ? NULL : function;
}

void mark_compiler_roots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        mark_object((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
