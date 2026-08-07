#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, object_type) (type*)allocate_object(sizeof(type), object_type)

static Obj* allocate_object(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->is_marked = false;

    object->next = vm.objects;
    vm.objects = object;

    #ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
    #endif

    return object;
}

ObjBoundMethod* new_bound_method(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, ObjTyBoundMethod);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass* new_class(ObjString* name) {
    ObjClass* cls = ALLOCATE_OBJ(ObjClass, ObjTyClass);
    cls->name = name;
    init_table(&cls->methods);
    return cls;
}

ObjClosure* new_closure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalue_count);
    for (int i = 0; i < function->upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, ObjTyClosure);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;
    return closure;
}

ObjFunction* new_function() {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, ObjTyFunction);
    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    init_chunk(&function->chunk);
    return function;
}

ObjInstance* new_instance(ObjClass* cls) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, ObjTyInstance);
    instance->cls = cls;
    init_table(&instance->fields);
    return instance;
}

ObjNative* new_native(NativeFn function, const char* name) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, ObjTyNative);
    native->function = function;
    native->name = name;
    return native;
}

static ObjString* allocate_string(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, ObjTyString);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    push(OBJ_VAL(string));
    table_set(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

static uint32_t hash_string(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* take_string(char* chars, int length) {
    uint32_t hash = hash_string(chars, length);
    ObjString* interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocate_string(chars, length, hash);
}

ObjString* copy_string(const char* chars, int length) {
    uint32_t hash = hash_string(chars, length);
    ObjString* interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        return interned;
    }
    char* heap_chars = ALLOCATE(char, length + 1);
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return allocate_string(heap_chars, length, hash);
}

ObjUpvalue* new_upvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, ObjTyUpvalue);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

static void print_function(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn `%s`>", function->name->chars);
}

void print_object(Value value) {
    switch (OBJ_TYPE(value)) {
        case ObjTyBoundMethod: {
            print_function(AS_BOUND_METHOD(value)->method->function);
            break;
        }
        case ObjTyClass: {
            printf("<class `%s`>", AS_CLASS(value)->name->chars);
            break;
        }
        case ObjTyClosure: {
            print_function(AS_CLOSURE(value)->function);
            break;
        }
        case ObjTyFunction: {
            print_function(AS_FUNCTION(value));
            break;
        }
        case ObjTyInstance: {
            printf("<instance of `%s`>", AS_INSTANCE(value)->cls->name->chars);
            break;
        }
        case ObjTyNative: {
            printf("<native fn `%s`>", AS_NATIVE(value)->name);
            break;
        }
        case ObjTyString: {
            printf("%s", AS_CSTRING(value));
            break;
        }
        case ObjTyUpvalue: {
            printf("<upvalue>");
            break;
        }
    }
}
