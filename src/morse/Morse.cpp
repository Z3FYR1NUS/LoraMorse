#include "Morse.h"

namespace morse {

char decode(const Marks& marks) {
    if (!marks.length || marks.overflow || marks.length > 5) return '?';
    // Binary Morse tree: start at 1; dot -> 2*i, dash -> 2*i+1.
    static const char tree[] =
        "??ETIANMSURWDKGOHVF?L?PJBXCYZQ??"
        "54?3???2???????16???????7???8?90";
    static_assert(sizeof(tree) == 65, "Morse tree must have 64 entries");
    uint8_t index = 1;
    for (uint8_t i = 0; i < marks.length; ++i) {
        index = uint8_t(index * 2 + (marks.text[i] == '-'));
    }
    return tree[index];
}

}  // namespace morse
