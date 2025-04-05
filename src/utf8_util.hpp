#pragma once

#include <string>

std::string toLowerCase(const std::string& s);

std::vector<uint32_t> utf8ToCodePoints(const std::string& text, bool exclude_newlines = false);
