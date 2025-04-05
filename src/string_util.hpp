#pragma once

#include <string>
#include <map>
#include <optional>


std::string ltrim(const std::string& s);
std::string rtrim(const std::string& s);
std::string trim(const std::string& s);

void ltrim(std::string& s);
void rtrim(std::string& s);
void trim(std::string& s);

std::string strip_prefix(const std::string& str, const char prefix);

std::vector<std::string> split(const std::string& str, const std::string& delimiter);
std::string join(const std::vector<std::string>& strings, const std::string& delimiter);


class AcceptHeader {
public:
    AcceptHeader() {}
    AcceptHeader(const std::string& header_value) { parse(header_value); }
    bool parse(const std::string& header_value);
    std::optional<float> operator()(const std::string& mimeType);
    float operator()(const std::string& mimeType, float default_value) { if (auto r = (*this)(mimeType); r) return r.value(); return default_value; }
    operator bool() { return parse_failed; }
private:
    std::map<std::string, std::map<std::string, float>> mimeSubTypeQ;
    std::map<std::string, float> mimeTypeQ;
    bool parse_failed;
};


std::string getMIMEType(const std::string& ext);

std::string human_readable_size(size_t bytes, int decimal_places = 2, bool space_before_unit = true);
