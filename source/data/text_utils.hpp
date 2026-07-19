#pragma once

#include <string>

namespace iptv {

// Remove emoji/símbolos decorativos de um texto — alguns provedores IPTV
// colocam isso em nomes de canal/categoria, e a fonte bundled do app não
// tem esses glifos (mostra caixinha/glifo errado). Mais simples do que
// arrumar cobertura de fonte pra isso.
std::string stripEmoji(const std::string& input);

}  // namespace iptv
