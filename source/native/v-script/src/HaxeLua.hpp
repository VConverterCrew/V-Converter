#pragma once

#include "Converter.hpp"

#include <set>
#include <string>
#include <vector>

namespace vscript {

struct HaxeLuaOptions {
    std::string engine = "vslice";
    std::string fileName;
    std::string id;
    std::set<std::string> stageProps;
    std::string eventName;
    std::string noteType;
    int characterLayer = 300;
};

struct HaxeLuaResult {
    bool ok = false;
    std::string source;
    std::string kind;
    std::string className;
    std::string targetId;
    std::string title;
    std::vector<std::string> schema;
    std::vector<std::string> callbacks;
    std::vector<Issue> issues;
    int fallbacks = 0;
};

HaxeLuaResult convertHaxeToLua(const std::string& source, const HaxeLuaOptions& options);

}
