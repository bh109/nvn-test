#pragma once
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace nvn_graphics {
inline bool resolution_scale(const std::string &text, double &value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double parsed;
    std::string extra;
    if(!(input >> parsed) || (input >> extra) || !std::isfinite(parsed) || parsed<0.25 || parsed>2.0) return false;
    value=parsed;
    return true;
}
}
