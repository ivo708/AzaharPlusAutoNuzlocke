#pragma once
#include <cstddef>

namespace PokemonCapture {

// Inyecta el cheat de captura
bool RemovePokeballs();

// Quita el cheat de captura
bool RestorePokeballs();

void RemoveCandies();

constexpr VAddr PARTY_ORAS = 0x8C7D4CCu;
constexpr VAddr PC_ORAS = 0x8C9A144u;


} // namespace PokemonCapture
