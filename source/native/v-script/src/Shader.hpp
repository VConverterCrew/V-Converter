#pragma once

#include "Converter.hpp"

#include <string>
#include <vector>

namespace vscript {

struct ShaderOptions {
    std::string from;
    std::string to;
    std::string name;
    bool vertex = false;
};

struct ShaderResult {
    std::string source;
    bool changed = false;
    std::vector<Issue> fixes;
    std::vector<Issue> problems;
    std::vector<std::string> uniforms;
};

ShaderResult convertShader(const std::string& source, const ShaderOptions& options);

}
