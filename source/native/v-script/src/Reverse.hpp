#pragma once

#include "Json.hpp"

#include <string>
#include <utility>
#include <vector>

namespace vscript {

using NamedTexts = std::vector<std::pair<std::string, std::string>>;

Json convertVSliceToPsychCharts(const NamedTexts& inputs, const Json& options, NamedTexts& files, std::string& error);

Json convertFiles(const std::string& kind, const NamedTexts& inputs, const Json& options, NamedTexts& files, std::string& error);

}
