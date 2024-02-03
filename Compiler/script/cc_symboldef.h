#ifndef __CC_SYMBOLDEF_H
#define __CC_SYMBOLDEF_H

#include "cs_parser_common.h"   // macro definitions

#define STYPE_DYNARRAY  (0x10000000)
#define STYPE_CONST     (0x20000000)
#define STYPE_POINTER   (0x40000000)

#define STYPE_MASK       (0xFFFFFFF)

#define SYM_TEMPORARYTYPE -99

struct SymbolDef {
    short stype;
    int32_t  flags;
    int32_t  ssize;  // or return type size for function
    short sscope;  // or num arguments for function
    int32_t  arrsize;
    uint32_t funcparamtypes[MAX_FUNCTION_PARAMETERS+1];
    int32_t funcParamDefaultValues[MAX_FUNCTION_PARAMETERS+1];
    bool funcParamHasDefaultValues[MAX_FUNCTION_PARAMETERS+1];
};

#endif // __CC_SYMBOLDEF_H
