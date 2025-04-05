#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <utf8.h>

#include "utf8_util.hpp"

// https://stackoverflow.com/questions/36897781/how-to-uppercase-lowercase-utf-8-characters-in-c
// https://www.alphabet.se/download/UtfConv.c
// $ curl -O https://www.alphabet.se/download/UtfConv.c
//
// possible alternative approach: https://pastebin.com/fuw4Uizk

extern "C" {
    unsigned char* Utf8StrMakeLwrUtf8Str(const unsigned char* s);
}

std::string toLowerCase(const std::string& s) {
    if(s.size() == 0)
        return "";
    auto lower = Utf8StrMakeLwrUtf8Str((unsigned const char*)s.c_str());
    std::string output((const char*)lower);
    free(lower);
    return output;
}

std::vector<uint32_t> utf8ToCodePoints(const std::string& text, bool exclude_newlines) {

    // https://stackoverflow.com/a/2856241
    char *start = (char*)text.c_str();    // utf-8 string
    char *end = start + text.size();      // end iterator
    char *cur = start;                    // string iterator

    std::vector<uint32_t> codepoints;
    codepoints.reserve(text.size());      // at most

    while(cur < end) {
        uint32_t code = utf8::next(cur, end); // get 32 bit code of a utf-8 symbol
        if (code == 0)
            continue;
        if (exclude_newlines && (code == 13 || code == 10))
            continue;
        codepoints.push_back(code);
    }

    return codepoints;
}
