#pragma once

#include <cstddef>

namespace vscript {

struct LuaApiFunction {
    const char* name;
    int required;
    int total;
    const char* params;
};

struct NamePair {
    const char* from;
    const char* to;
};

struct HaxeType {
    const char* name;
    const char* path;
    const char* kind;
};

extern const LuaApiFunction kPsychFunctions[];
extern const size_t kPsychFunctionCount;
extern const NamePair kPsychDeprecated[];
extern const size_t kPsychDeprecatedCount;
extern const char* const kPsychCallbacks[];
extern const size_t kPsychCallbackCount;
extern const char* const kPsychGlobals[];
extern const size_t kPsychGlobalCount;
extern const HaxeType kHaxeTypes[];
extern const size_t kHaxeTypeCount;
extern const NamePair kVSliceCallbacks[];
extern const size_t kVSliceCallbackCount;
extern const NamePair kScriptBases[];
extern const size_t kScriptBaseCount;
extern const char* const kHaxePaths[];
extern const size_t kHaxePathCount;
extern const NamePair kPsychClasses[];
extern const size_t kPsychClassCount;

}
