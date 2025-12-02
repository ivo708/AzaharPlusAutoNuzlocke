#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PokeAntiRq {

// Devuelve el Title ID actual en formato string hexadecimal (16 chars)
std::string GetCurrentTitleID();

// Devuelve la ruta física completa del SaveData del título en ejecución
std::string GetCurrentSaveDataPath();

std::string GetCurrentSaveStatePath();

void onClose();

void onStart();


} // namespace PokeAntiRiq
