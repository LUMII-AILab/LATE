#include <string>
#include <algorithm>
#include <cctype>
#include <locale>
#include <vector>
#include <stdexcept>
#include <optional>
#include <limits>
#include <cmath>

#include "string_util.hpp"


std::string ltrim(const std::string& s) {
    std::string result = s;
    result.erase(result.begin(), std::find_if(result.begin(), result.end(),
                                              [](unsigned char ch) { return !std::isspace(ch); }));
    return result;
}

std::string rtrim(const std::string& s) {
    std::string result = s;
    result.erase(std::find_if(result.rbegin(), result.rend(),
                              [](unsigned char ch) { return !std::isspace(ch); }).base(),
                 result.end());
    return result;
}

std::string trim(const std::string& s) {
    return ltrim(rtrim(s));
}

void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}

std::string strip_prefix(const std::string& str, const char prefix) {
    if (!str.empty() && str[0] == prefix) {
        return str.substr(1);  // skip the first character
    }
    return str;
}

std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start));
    return tokens;
}

std::string join(const std::vector<std::string>& strings, const std::string& delimiter) {
    std::string result;
    for (const auto& s : strings) {
        if (!result.empty())
            result += delimiter;
        result += s;
    }
    return result;
}

bool _starts_with(std::string_view str, std::string_view prefix) {
    return str.substr(0, prefix.size()) == prefix;
}

bool AcceptHeader::parse(const std::string& header_value) {
    // Accept: text/html, application/xhtml+xml, application/xml;q=0.9, image/webp, */*;q=0.8
    auto accepts = split(header_value, ",");
    for (auto& mimeq : accepts) {
        trim(mimeq);
        auto mime_q = split(mimeq, ";");
        auto& mime = mime_q[0];
        float q = 1;
        if (mime_q.size() >= 2) {
            auto& qeq = mime_q[1];
            if (_starts_with(qeq, "q=")) {
                try {
                    q = std::stof(qeq.substr(2));
                // } catch (const std::invalid_argument& e) {
                //     // no conversion could be performed.
                // } catch (const std::out_of_range& e) {
                //     // the converted value would fall out of the range of the result type or if the
                //     // underlying function (std::strtof, std::strtod or std::strtold) sets errno to ERANGE.
                } catch (const std::exception& e) {
                    parse_failed = true;
                    return false;
                }
            } else {
                // invalid expr
                parse_failed = true;
                return false;
            }
        }
        auto type_subtype = split(mime, "/");
        if (type_subtype.size() != 2) {
            parse_failed = true;
            return false;
        }
        auto& subtypes = mimeSubTypeQ[type_subtype[0]];
        subtypes[type_subtype[1]] = q;
        mimeTypeQ[mime] = q;
    }
    parse_failed = false;
    return true;
}

std::optional<float> AcceptHeader::operator()(const std::string& mimeType) {
    if (auto it = mimeTypeQ.find(mimeType); it != mimeTypeQ.end()) {
        return it->second;
    }
    auto type_subtype = split(mimeType, "/");
    if (type_subtype.size() == 2) {
        if (auto it = mimeSubTypeQ.find(type_subtype[0]); it != mimeSubTypeQ.end()) {
            auto& subtypes = it->second;
            if (auto it = subtypes.find(type_subtype[1]); it != subtypes.end()) {
                return it->second;
            } else if (auto it = subtypes.find(type_subtype[1]); it != subtypes.end()) {
                return it->second;
            }
        } else if (auto it = mimeSubTypeQ.find("*"); it != mimeSubTypeQ.end()) {
            auto& subtypes = it->second;
            if (auto it = subtypes.find(type_subtype[1]); it != subtypes.end()) {
                return it->second;
            } else if (auto it = subtypes.find(type_subtype[1]); it != subtypes.end()) {
                return it->second;
            }
        }
    }
    return std::nullopt;
}

inline constexpr unsigned int str2tag_core(const char *s, size_t l,
                                           unsigned int h) {
  return (l == 0)
             ? h
             : str2tag_core(
                   s + 1, l - 1,
                   // unsets the 6 high bits of h, therefore no overflow happens
                   (((std::numeric_limits<unsigned int>::max)() >> 6) &
                    h * 33) ^
                       static_cast<unsigned char>(*s));
}

inline unsigned int str2tag(const std::string &s) {
  return str2tag_core(s.data(), s.size(), 0);
}

namespace udl {

inline constexpr unsigned int operator"" _t(const char *s, size_t l) {
  return str2tag_core(s, l, 0);
}

} // namespace udl

std::string getMIMEType(const std::string& ext) {
    using udl::operator""_t;
    switch (str2tag(ext.c_str())) {
        case ".css"_t: return "text/css";
        case ".csv"_t: return "text/csv";
        case ".htm"_t:
        case ".html"_t: return "text/html";
        case ".js"_t:
        case ".mjs"_t: return "text/javascript";
        case ".txt"_t: return "text/plain";
        case ".vtt"_t: return "text/vtt";

        case ".apng"_t: return "image/apng";
        case ".avif"_t: return "image/avif";
        case ".bmp"_t: return "image/bmp";
        case ".gif"_t: return "image/gif";
        case ".png"_t: return "image/png";
        case ".svg"_t: return "image/svg+xml";
        case ".webp"_t: return "image/webp";
        case ".ico"_t: return "image/x-icon";
        case ".tif"_t: return "image/tiff";
        case ".tiff"_t: return "image/tiff";
        case ".jpg"_t:
        case ".jpeg"_t: return "image/jpeg";

        case ".mp4"_t: return "video/mp4";
        case ".mpeg"_t: return "video/mpeg";
        case ".webm"_t: return "video/webm";

        case ".mp3"_t: return "audio/mp3";
        case ".mpga"_t: return "audio/mpeg";
        case ".weba"_t: return "audio/webm";
        case ".wav"_t: return "audio/wave";

        case ".otf"_t: return "font/otf";
        case ".ttf"_t: return "font/ttf";
        case ".woff"_t: return "font/woff";
        case ".woff2"_t: return "font/woff2";

        case ".7z"_t: return "application/x-7z-compressed";
        case ".atom"_t: return "application/atom+xml";
        case ".pdf"_t: return "application/pdf";
        case ".json"_t: return "application/json";
        case ".rss"_t: return "application/rss+xml";
        case ".tar"_t: return "application/x-tar";
        case ".xht"_t:
        case ".xhtml"_t: return "application/xhtml+xml";
        case ".xslt"_t: return "application/xslt+xml";
        case ".xml"_t: return "application/xml";
        case ".gz"_t: return "application/gzip";
        case ".zip"_t: return "application/zip";
        case ".wasm"_t: return "application/wasm";
    }
    return "application/octet-stream";
}

std::string human_readable_size(size_t bytes, int decimal_places/*  = 2 */, bool space_before_unit/*  = true */) {
    const char* units[] = {"B", "KB", "MB", "GB"/* , "TB", "PB", "EB", "ZB", "YB" */};
    int unit = 0;
    double size = bytes;

    for (; size >= 1024 && unit < sizeof(units); unit++)
        size /= 1024;

    std::string result = std::to_string(size);

    if (size_t dot_pos = result.find('.'); dot_pos != std::string::npos)
        result = result.substr(0, dot_pos + decimal_places + 1);

    return result + (space_before_unit ? " " : "") + units[unit];
}
