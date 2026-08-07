#include <stdio.h>

#include "object.h"
#include "memory.h"
#include "value.h"

bool values_equal(Value a, Value b) {
    #ifdef NAN_BOXING

    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    return a == b;

    #else

    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
        case ValBool: {
            return AS_BOOL(a) == AS_BOOL(b);
        }
        case ValNil: {
            return true;
        }
        case ValNumber: {
            return AS_NUMBER(a) == AS_NUMBER(b);
        }
        case ValObj: {
            return AS_OBJ(a) == AS_OBJ(b);
        }
    }

    #endif
}

void init_value_array(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void write_value_array(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int old_capacity = array->capacity;
        array->capacity = GROW_CAPACITY(old_capacity);
        array->values = GROW_ARRAY(Value, array->values, old_capacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void free_value_array(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    init_value_array(array);
}

void print_value(Value value) {
    #ifdef NAN_BOXING

    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        print_object(value);
    }

    #else

    switch (value.type) {
        case ValBool: {
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        }
        case ValNil: {
            printf("nil");
            break;
        }
        case ValNumber: {
            printf("%g", AS_NUMBER(value));
            break;
        }
        case ValObj: {
            print_object(value);
            break;
        }
    }

    #endif
}
