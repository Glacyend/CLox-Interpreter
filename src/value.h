#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef enum {
    ValBool,
    ValNil,
    ValNumber,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
    } as;
} Value;

#define IS_BOOL(value) ((value).type == ValBool)
#define IS_NIL(value) ((value).type == ValNil)
#define IS_NUMBER(value) ((value).type == ValNumber)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) ((Value){ValBool, {.boolean = value}})
#define NIL_VAL ((Value){ValNil, {.number = 0}})
#define NUMBER_VAL(value) ((Value){ValNumber, {.number = value}})

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
