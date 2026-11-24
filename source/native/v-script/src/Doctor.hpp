#pragma once

#include "Converter.hpp"

#include <string>
#include <vector>

namespace vscript {

struct DoctorOptions {
    std::string engine = "vslice";
    std::string fileName;
    std::string id;
};

struct DoctorResult {
    std::string source;
    bool changed = false;
    std::vector<Issue> fixes;
    std::vector<Issue> problems;
};

DoctorResult fixLuaScript(const std::string& source, const DoctorOptions& options);
DoctorResult fixHaxeScript(const std::string& source, const DoctorOptions& options);

}
