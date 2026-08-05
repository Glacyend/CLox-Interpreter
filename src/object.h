#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) is_obj_type(value, ObjTyFunction)
#define IS_NATIVE(value) is_obj_type(value, ObjTyNative)
#define IS_STRING(value) is_obj_type(value, ObjTyString)

#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    ObjTyFunction,
    ObjTyNative,
    ObjTyString,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
};

typedef struct {
    Obj obj;
    int arity;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int arg_count, Value* args, bool* no_error);

typedef struct {
    Obj obj;
    NativeFn function;
    const char* name;
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

ObjFunction* new_function();
ObjNative* new_native(NativeFn function, const char* name);
ObjString* take_string(char* chars, int length);
ObjString* copy_string(const char* chars, int length);
void print_object(Value value);

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
