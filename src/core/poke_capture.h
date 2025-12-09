#pragma once
#include <cstddef>

namespace PokemonCapture {

// Inyecta el cheat de captura
bool RemovePokeballs();

// Quita el cheat de captura
bool RestorePokeballs();

constexpr VAddr PARTY_ORAS = 0x08CFB26Cu;
constexpr VAddr PC_ORAS = 0x08C9E134u;


} // namespace PokemonCapture
