#include "data/text_utils.hpp"

namespace iptv {

namespace {

bool isEmojiCodepoint(char32_t cp) {
    // Faixas comuns de emoji/símbolos decorativos + seletores de variação +
    // letras regionais (bandeiras). A fonte bundled (CJK) não tem esses
    // glifos, então melhor remover do que mostrar caixinha/glifo errado.
    if (cp >= 0x1F300 && cp <= 0x1FAFF) return true;  // emoji, símbolos diversos
    if (cp >= 0x2600 && cp <= 0x27BF) return true;    // símbolos diversos + dingbats
    if (cp >= 0x2190 && cp <= 0x21FF) return true;    // setas
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;    // símbolos diversos adicionais
    if (cp == 0xFE0F || cp == 0xFE0E) return true;    // seletores de variação
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return true;  // letras regionais (bandeiras)
    if (cp >= 0x2300 && cp <= 0x23FF) return true;    // símbolos técnicos diversos
    return false;
}

}  // namespace

std::string stripEmoji(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    size_t i = 0;
    size_t n = input.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        size_t len = 1;
        char32_t cp = c;

        if ((c & 0x80) == 0x00) {
            len = 1;
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            len = 2;
            cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(input[i + 1]) & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            len = 3;
            cp = (c & 0x0F) << 12 | (static_cast<unsigned char>(input[i + 1]) & 0x3F) << 6 |
                 (static_cast<unsigned char>(input[i + 2]) & 0x3F);
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            len = 4;
            cp = (c & 0x07) << 18 | (static_cast<unsigned char>(input[i + 1]) & 0x3F) << 12 |
                 (static_cast<unsigned char>(input[i + 2]) & 0x3F) << 6 |
                 (static_cast<unsigned char>(input[i + 3]) & 0x3F);
        }

        if (!isEmojiCodepoint(cp)) {
            output.append(input, i, len);
        }
        i += len;
    }

    return output;
}

}  // namespace iptv
