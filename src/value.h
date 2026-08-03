#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
    ValBool,
    ValNil,
    ValNumber,
    ValObj,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

#define IS_BOOL(value) ((value).type == ValBool)
#define IS_NIL(value) ((value).type == ValNil)
#define IS_NUMBER(value) ((value).type == ValNumber)
#define IS_OBJ(value) ((value).type == ValObj)

#define AS_OBJ(value) ((value).as.obj)
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) ((Value){ValBool, {.boolean = value}})
#define NIL_VAL ((Value){ValNil, {.number = 0}})
#define NUMBER_VAL(value) ((Value){ValNumber, {.number = value}})
#define OBJ_VAL(value) ((Value){ValObj, {.obj = (Obj*)value}})

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

bool values_equal(Value a, Value b);
void init_value_array(ValueArray* array);
void write_value_array(ValueArray* array, Value value);
void free_value_array(ValueArray* array);
void print_value(Value value);

#endif
