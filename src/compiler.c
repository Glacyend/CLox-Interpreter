#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
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
    PrecAn,  // and
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
} Local;

typedef struct {
    Local locals[UINT8_COUNT];
    int local_count;
    int scope_depth;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compiling_chunk;

static Chunk* current_chunk() {
    return compiling_chunk;
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

static void emit_return() {
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

static void init_compiler(Compiler* compiler) {
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    current = compiler;
}

static void end_compiler() {
    emit_return();

    #ifdef DEBUG_PRINT_CODE
    if (!parser.had_error) {
        disassemble_chunk(current_chunk(), "code");
    }
    #endif
}

static void begin_scope() {
    current->scope_depth++;
}

static void end_scope() {
    current->scope_depth--;

    while (current->local_count > 0 && current->locals[current->local_count - 1].depth > current->scope_depth) {
        emit_byte(OpPop);
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

static void add_local(Token name) {
    if (current->local_count == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->local_count++];
    local->name = name;
    local->depth = -1;
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
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global) {
    if (current->scope_depth > 0) {
        mark_initialized();
        return;
    }

    emit_bytes(OpDefineGlobal, global);
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

static void string([[maybe_unused]] bool can_assign) {
    emit_constant(OBJ_VAL(copy_string(parser.previous.start + 1, parser.previous.length - 2)));
}

static void named_variable(Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);
    if (arg != -1) {
        get_op = OpGetLocal;
        set_op = OpSetLocal;
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
    [TokenLeftParen]    = { grouping, NULL,   PrecNone       },
    [TokenRightParen]   = { NULL,     NULL,   PrecNone       },
    [TokenLeftBrace]    = { NULL,     NULL,   PrecNone       },
    [TokenRightBrace]   = { NULL,     NULL,   PrecNone       },
    [TokenComma]        = { NULL,     NULL,   PrecNone       },
    [TokenDot]          = { NULL,     NULL,   PrecNone       },
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
    [TokenAnd]          = { NULL,     NULL,   PrecNone       },
    [TokenClass]        = { NULL,     NULL,   PrecNone       },
    [TokenElse]         = { NULL,     NULL,   PrecNone       },
    [TokenFalse]        = { literal,  NULL,   PrecNone       },
    [TokenFor]          = { NULL,     NULL,   PrecNone       },
    [TokenFun]          = { NULL,     NULL,   PrecNone       },
    [TokenIf]           = { NULL,     NULL,   PrecNone       },
    [TokenNil]          = { literal,  NULL,   PrecNone       },
    [TokenOr]           = { NULL,     NULL,   PrecNone       },
    [TokenPrint]        = { NULL,     NULL,   PrecNone       },
    [TokenReturn]       = { NULL,     NULL,   PrecNone       },
    [TokenSuper]        = { NULL,     NULL,   PrecNone       },
    [TokenThis]         = { NULL,     NULL,   PrecNone       },
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

static void print_statement() {
    expression();
    consume(TokenSemicolon, "Expect `;` after value.");
    emit_byte(OpPrint);
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
    if (match(TokenVar)) {
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
    } else if (match(TokenLeftBrace)) {
        begin_scope();
        block();
        end_scope();
    } else {
        expression_statement();
    }
}

bool compile(const char* source, Chunk* chunk) {
    init_scanner(source);
    Compiler compiler;
    init_compiler(&compiler);
    compiling_chunk = chunk;

    parser.had_error = false;
    parser.panic_mode = false;

    advance();

    while (!match(TokenEOF)) {
        declaration();
    }

    end_compiler();
    return !parser.had_error;
}
