#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLASS(value) is_obj_type(value, ObjTyClass)
#define IS_CLOSURE(value) is_obj_type(value, ObjTyClosure)
#define IS_FUNCTION(value) is_obj_type(value, ObjTyFunction)
#define IS_INSTANCE(value) is_obj_type(value, ObjTyInstance)
#define IS_NATIVE(value) is_obj_type(value, ObjTyNative)
#define IS_STRING(value) is_obj_type(value, ObjTyString)

#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    ObjTyClass,
    ObjTyClosure,
    ObjTyFunction,
    ObjTyInstance,
    ObjTyNative,
    ObjTyString,
    ObjTyUpvalue,
} ObjType;

struct Obj {
    ObjType type;
    bool is_marked;
    struct Obj* next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalue_count;
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

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalue_count;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString* name;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass* cls;
    Table fields;
} ObjInstance;

ObjClass* new_class(ObjString* name);
ObjClosure* new_closure(ObjFunction* function);
ObjFunction* new_function();
ObjInstance* new_instance(ObjClass* cls);
ObjNative* new_native(NativeFn function, const char* name);
ObjString* take_string(char* chars, int length);
ObjString* copy_string(const char* chars, int length);
ObjUpvalue* new_upvalue(Value* slot);
void print_object(Value value);

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
