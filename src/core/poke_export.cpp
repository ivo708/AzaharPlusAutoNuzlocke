#include "poke_export.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "common/logging/log.h"
#include "core/core.h"
#include "core/global.h"
#include "core/memory.h"
#include "poke_capture.h"
#include <nlohmann/json.hpp>

Pk6FullData g_partyData[PARTY_SIZE];
BattleSlotData g_battleData[PARTY_SIZE];
Pk6FullData g_wildData[MAX_WILDS];

using namespace Memory;

namespace {

MemorySystem& Mem() {
    return Core::Global<Core::System>().Memory();
}

inline uint16_t ReadLE16(const uint8_t* b, std::size_t off) {
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

inline void WriteLE16(uint8_t* b, std::size_t off, uint16_t v) {
    b[off] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline uint32_t ReadLE32(const uint8_t* b, std::size_t off) {
    return static_cast<uint32_t>(b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) |
                                 (b[off + 3] << 24));
}

inline void WriteLE32(uint8_t* b, std::size_t off, uint32_t v) {
    b[off] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Función de descifrado PK6
static bool DecryptPk6InPlace(std::vector<uint8_t>& buf, std::size_t size, bool is_party) {
    if (size < 232)
        return false;

    constexpr std::size_t PKM_ENCRYPTED_START = 0x08;
    constexpr std::size_t PKM_MAIN_ENCRYPTED_SIZE = 224;
    constexpr std::size_t PKM_BLOCK_SIZE = 56;
    constexpr std::array<std::array<int, 4>, 24> BLOCK_ORDERS = {
        {{{0, 1, 2, 3}}, {{0, 1, 3, 2}}, {{0, 2, 1, 3}}, {{0, 2, 3, 1}}, {{0, 3, 1, 2}},
         {{0, 3, 2, 1}}, {{1, 0, 2, 3}}, {{1, 0, 3, 2}}, {{1, 2, 0, 3}}, {{1, 2, 3, 0}},
         {{1, 3, 0, 2}}, {{1, 3, 2, 0}}, {{2, 0, 1, 3}}, {{2, 0, 3, 1}}, {{2, 1, 0, 3}},
         {{2, 1, 3, 0}}, {{2, 3, 0, 1}}, {{2, 3, 1, 0}}, {{3, 0, 1, 2}}, {{3, 0, 2, 1}},
         {{3, 1, 0, 2}}, {{3, 1, 2, 0}}, {{3, 2, 0, 1}}, {{3, 2, 1, 0}}}};
    constexpr uint32_t LCG_A = 0x41C64E6Du;
    constexpr uint32_t LCG_C = 0x6073u;
    auto ReadLE16_local = [](const uint8_t* b, std::size_t off) -> uint16_t {
        return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
    };
    auto WriteLE16_local = [](uint8_t* b, std::size_t off, uint16_t v) {
        b[off] = static_cast<uint8_t>(v & 0xFF);
        b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };

    auto LCG_next_u16 = [](uint32_t& state) -> uint16_t {
        state = state * LCG_A + LCG_C;
        return static_cast<uint16_t>((state >> 16) & 0xFFFF);
    };

    uint32_t enc_key = ReadLE32(buf.data(), 0);
    uint32_t lcg_state = enc_key;

    // IMPORTANTE: solo desencriptamos el MAIN (0x08..0x87 = 224 bytes). Los Battle Stats
    // los tratará DecryptBattleStats que avanza el RNG a la posición correcta.
    std::size_t bytes_to_decrypt = PKM_MAIN_ENCRYPTED_SIZE;
    for (std::size_t off = PKM_ENCRYPTED_START; off < PKM_ENCRYPTED_START + bytes_to_decrypt;
         off += 2) {
        if (off + 1 >= size)
            break;
        uint16_t w = ReadLE16_local(buf.data(), off);
        w ^= LCG_next_u16(lcg_state);
        WriteLE16_local(buf.data(), off, w);
    }

    // Deshacer el "block shuffle" sobre el MAIN (como antes)
    if (size >= PKM_ENCRYPTED_START + PKM_MAIN_ENCRYPTED_SIZE) {
        uint32_t shift = ((enc_key & 0x3E000u) >> 13) % 24u;
        std::array<uint8_t, PKM_MAIN_ENCRYPTED_SIZE> encrypted_main;
        std::memcpy(encrypted_main.data(), buf.data() + PKM_ENCRYPTED_START,
                    PKM_MAIN_ENCRYPTED_SIZE);
        std::array<uint8_t, PKM_MAIN_ENCRYPTED_SIZE> unshuffled_main;

        const auto& order = BLOCK_ORDERS[shift];
        for (int j = 0; j < 4; j++) {
            int found_index = -1;
            for (int k = 0; k < 4; k++) {
                if (order[k] == j) {
                    found_index = k;
                    break;
                }
            }
            if (found_index < 0)
                return false;
            std::memcpy(unshuffled_main.data() + j * PKM_BLOCK_SIZE,
                        encrypted_main.data() + found_index * PKM_BLOCK_SIZE, PKM_BLOCK_SIZE);
        }
        std::memcpy(buf.data() + PKM_ENCRYPTED_START, unshuffled_main.data(),
                    PKM_MAIN_ENCRYPTED_SIZE);
    }

    return true;
}

// Funcion de encriptado para el blank
static bool EncryptPk6InPlace(std::vector<uint8_t>& buf, std::size_t size, bool is_party) {
    // Validación de tamaño (party o box)
    const std::size_t PKM_MAIN_ENCRYPTED_SIZE = 224;
    const std::size_t PKM_ENCRYPTED_START = 0x08;
    const std::size_t PKM_BLOCK_SIZE = 56;
    if ((is_party && size < PKM_PARTY_SIZE) || (!is_party && size < PKM_BOX_SIZE))
        return false;

    // Tabla de orden de bloques (idéntica a la usada en DecryptPk6InPlace)
    constexpr std::array<std::array<int, 4>, 24> BLOCK_ORDERS = {
        {{{0, 1, 2, 3}}, {{0, 1, 3, 2}}, {{0, 2, 1, 3}}, {{0, 2, 3, 1}}, {{0, 3, 1, 2}},
         {{0, 3, 2, 1}}, {{1, 0, 2, 3}}, {{1, 0, 3, 2}}, {{1, 2, 0, 3}}, {{1, 2, 3, 0}},
         {{1, 3, 0, 2}}, {{1, 3, 2, 0}}, {{2, 0, 1, 3}}, {{2, 0, 3, 1}}, {{2, 1, 0, 3}},
         {{2, 1, 3, 0}}, {{2, 3, 0, 1}}, {{2, 3, 1, 0}}, {{3, 0, 1, 2}}, {{3, 0, 2, 1}},
         {{3, 1, 0, 2}}, {{3, 1, 2, 0}}, {{3, 2, 0, 1}}, {{3, 2, 1, 0}}}};
    constexpr uint32_t LCG_A = 0x41C64E6Du;
    constexpr uint32_t LCG_C = 0x6073u;

    // --- 1) Recalcular checksum sobre bytes EN CLARO (0x08..0x08+224-1) y escribirlo ---
    uint16_t checksum = 0;
    for (std::size_t off = PKM_ENCRYPTED_START; off < PKM_ENCRYPTED_START + PKM_MAIN_ENCRYPTED_SIZE;
         off += 2) {
        checksum = static_cast<uint16_t>(checksum + ReadLE16(buf.data(), off));
    }
    WriteLE16(buf.data(), 0x06, checksum);

    // --- 2) Preparar arrays para shuffle ---
    std::array<uint8_t, PKM_MAIN_ENCRYPTED_SIZE> main_clear;
    std::memcpy(main_clear.data(), buf.data() + PKM_ENCRYPTED_START, PKM_MAIN_ENCRYPTED_SIZE);

    // Determinar shift a partir del Encryption Key (offset 0x00..0x03)
    uint32_t enc_key = ReadLE32(buf.data(), 0x00);
    uint32_t shift = ((enc_key & 0x3E000u) >> 13) % 24u;
    const auto& order = BLOCK_ORDERS[shift];

    // Construir la versión "shuffled" (la que luego será XOR-eada)
    std::array<uint8_t, PKM_MAIN_ENCRYPTED_SIZE> main_shuffled;
    for (int k = 0; k < 4; ++k) {
        // main_shuffled[k] = main_clear[ order[k] ]
        std::memcpy(main_shuffled.data() + k * PKM_BLOCK_SIZE,
                    main_clear.data() + order[k] * PKM_BLOCK_SIZE, PKM_BLOCK_SIZE);
    }

    // Copiar el main shuffled AL BUFFER (antes del XOR)
    std::memcpy(buf.data() + PKM_ENCRYPTED_START, main_shuffled.data(), PKM_MAIN_ENCRYPTED_SIZE);

    // --- 3) XOR (LCG) sobre la parte principal (ya shuffleada) ---
    uint32_t state = enc_key;
    auto next_u16 = [&]() -> uint16_t {
        state = state * LCG_A + LCG_C;
        return static_cast<uint16_t>((state >> 16) & 0xFFFF);
    };

    for (std::size_t off = PKM_ENCRYPTED_START; off < PKM_ENCRYPTED_START + PKM_MAIN_ENCRYPTED_SIZE;
         off += 2) {
        if (off + 1 >= size)
            break;
        uint16_t w = ReadLE16(buf.data(), off);
        w ^= next_u16();
        WriteLE16(buf.data(), off, w);
    }

    // --- 4) Si es party: encriptar Battle Stats (0xE8..0x103) continuando la secuencia RNG ---
    if (is_party) {
        for (std::size_t off = 0xE8; off < 0x104; off += 2) {
            if (off + 1 >= size)
                break;
            uint16_t w = ReadLE16(buf.data(), off);
            w ^= next_u16();
            WriteLE16(buf.data(), off, w);
        }
    }

    return true;
}


namespace fs = std::filesystem;
static nlohmann::json readJson(const fs::path& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo abrir el archivo: " + filepath.string());
    }
    nlohmann::json j;
    file >> j;
    return j;
}


static int getExperienceForLevel(const fs::path& pokemonPath, const fs::path& curvesPath, int pokemon, int level) {
    fs::path pokemonFile = pokemonPath / (std::to_string(pokemon) + ".json");
    nlohmann::json pokemonData = readJson(pokemonFile);
    int growthRateId = pokemonData["growth_rate_id"];

    fs::path curveFile = curvesPath / (std::to_string(growthRateId) + ".json");
    nlohmann::json curveData = readJson(curveFile);

    for (auto& lvl : curveData["levels"]) {
        if (lvl["level"] == level) {
            return lvl["experience"];
        }
    }

    throw std::runtime_error("Nivel no encontrado en la curva: " + std::to_string(level));
}

bool isBattle() {
    VAddr slot_addr = WILD_ORAS;
    std::vector<uint8_t> data(PKM_BOX_SIZE);
    Mem().ReadBlock(slot_addr, data.data(), data.size());
    // intentar desencriptar; si falla asumimos no-battle (función usada solo para detección)
    if (!DecryptPk6InPlace(data, data.size(), false))
        return false;

    int passed = 0;

    // 1) Especie válida
    uint16_t species = ReadLE16(data.data(), OFF_SPECIES);
    if (species != 0 && species < 750)
        ++passed;

    // 2) Nature válido 0..30
    uint8_t nature = data[0x1C];
    if (nature <= 30)
        ++passed;

    // 3) Suma de EVs <= 520
    int ev_sum = 0;
    for (size_t off = 0x1E; off <= 0x23; ++off)
        ev_sum += data[off];
    if (ev_sum <= 520)
        ++passed;

    // 4) Ability number válido 0..5
    uint8_t ability_number = data[0x15];
    if (ability_number <= 5)
        ++passed;

    // 5) Pokéball válido 0..30
    uint8_t pokeball = data[0xDC];
    if (pokeball <= 30)
        ++passed;

    // 6) Original Language válido 1..15
    uint8_t lang = data[0xE3];
    if (lang >= 1 && lang <= 15)
        ++passed;

    // Si pasan 5 tests (suficiente), consideramos que es un slot válido
    return (passed >= 5);
}

// ------------------  BORRADO PENDIENTE Y TRANSICIÓN DE BATALLA ------------------
// Variables estáticas para gestionar transición
static bool g_wasInBattle = false;
static std::array<bool, PARTY_SIZE> g_pending_delete = {false, false, false, false, false, false};

constexpr int MAX_WRITE_ATTEMPTS = 6;

// Escribe un bloque (de tamaño STRIDE) en addr y verifica leyendo de vuelta; reintenta hasta
// MAX_WRITE_ATTEMPTS. Devuelve true si la memoria coincide exactamente con 'data' después de la(s)
// escritura(s).
static bool WriteBlockStrideWithVerify(VAddr addr, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> verify(data.size());
    for (int attempt = 0; attempt < MAX_WRITE_ATTEMPTS; ++attempt) {
        Mem().WriteBlock(addr, data.data(), data.size());
        Mem().ReadBlock(addr, verify.data(), verify.size());
        if (memcmp(verify.data(), data.data(), data.size()) == 0) {
            return true;
        }
        LOG_DEBUG(HW_Memory, "poke_export: WriteBlockStrideWithVerify intento %d falló en 0x%X",
                  attempt, addr);
        // Pequeña pausa entre intentos
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Log detallado del error
    LOG_ERROR(HW_Memory,
              "poke_export: FALLA CRÍTICA - No se pudo escribir en 0x%X después de %d intentos",
              addr, MAX_WRITE_ATTEMPTS);
    return false;
}

// Función para encriptar específicamente los Battle Stats
static bool EncryptBattleStats(std::vector<uint8_t>& buf, std::size_t size, bool is_party) {
    if (size < PKM_PARTY_SIZE || !is_party)
        return false;

    // Battle Stats: 0xE8-0x103 (28 bytes)
    uint32_t enc_key = ReadLE32(buf.data(), 0);
    uint32_t state = enc_key;

    auto next_u16 = [&]() -> uint16_t {
        state = state * 0x41C64E6D + 0x6073;
        return static_cast<uint16_t>((state >> 16) & 0xFFFF);
    };

    // AVANZAR el RNG para saltar la parte principal (112 u16 = 224 bytes)
    for (int i = 0; i < 112; ++i)
        (void)next_u16();

    // Ahora encriptar los Battle Stats (0xE8-0x103)
    for (std::size_t off = 0xE8; off < 0x104; off += 2) {
        if (off + 1 >= size)
            break;
        uint16_t w = ReadLE16(buf.data(), off);
        uint16_t r = next_u16();
        w ^= r;
        WriteLE16(buf.data(), off, w);
    }

    return true;
}

// Función para desencriptar específicamente los Battle Stats
static bool DecryptBattleStats(std::vector<uint8_t>& buf, std::size_t size, bool is_party) {
    if (size < PKM_PARTY_SIZE || !is_party)
        return false;

    uint32_t enc_key = ReadLE32(buf.data(), 0);
    uint32_t state = enc_key;

    auto next_u16 = [&]() -> uint16_t {
        state = state * 0x41C64E6D + 0x6073;
        return static_cast<uint16_t>((state >> 16) & 0xFFFF);
    };

    // AVANZAR el RNG 112 veces para alcanzar la posición de Battle Stats
    for (int i = 0; i < 112; ++i)
        (void)next_u16();

    // Desencriptar solo los Battle Stats (0xE8-0x103)
    for (std::size_t off = 0xE8; off < 0x104; off += 2) {
        if (off + 1 >= size)
            break;
        uint16_t w = ReadLE16(buf.data(), off);
        uint16_t r = next_u16();
        w ^= r;
        WriteLE16(buf.data(), off, w);
    }

    return true;
}

bool ReadPk6Slot(int party_slot, Pk6FullData* outPk6, bool isWild) {
    if (!outPk6 || party_slot < 0 || party_slot >= PARTY_SIZE)
        return false;

    VAddr slot_addr;
    std::vector<uint8_t> buf(PKM_PARTY_SIZE);
    if (isWild) {
        slot_addr = WILD_ORAS + static_cast<VAddr>(party_slot * POKEMON_SLOT_STRIDE);
    }
    else {
        slot_addr = PARTY_ORAS + static_cast<VAddr>(party_slot * POKEMON_SLOT_STRIDE);
    }
    Mem().ReadBlock(slot_addr, buf.data(), buf.size());

    if (!DecryptPk6InPlace(buf, buf.size(), true))
        return false;

    // PID
    outPk6->pid = ReadLE32(buf.data(), 0x18);

    // Species, item, ability
    outPk6->species = ReadLE16(buf.data(), 0x08);
    outPk6->item = ReadLE16(buf.data(), 0x0A);
    outPk6->ability = buf[0x14];

    // Lugar donde se conoció/capturó
    outPk6->metLocation = buf[0xDB];

    // Movimientos y PP
    outPk6->moves[0] = ReadLE16(buf.data(), 0x5A);
    outPk6->moves[1] = ReadLE16(buf.data(), 0x5C);
    outPk6->moves[2] = ReadLE16(buf.data(), 0x5E);
    outPk6->moves[3] = ReadLE16(buf.data(), 0x60);

    outPk6->currentPP[0] = buf[0x62];
    outPk6->currentPP[1] = buf[0x63];
    outPk6->currentPP[2] = buf[0x64];
    outPk6->currentPP[3] = buf[0x65];

    // Leer OT ID y OT Secret ID
    uint16_t OT_TID;
    uint16_t OT_SID;
    // Calcular si es shiny
    uint16_t pidHigh = outPk6->pid >> 16;
    uint16_t pidLow = outPk6->pid & 0xFFFF;

    if (ReadLE16(buf.data(), 0x08) != 0) {
        if (isWild) {
            // Para salvajes: usar TID/SID del jugador real
            Mem().ReadBlock(TID_ORAS, &OT_TID, sizeof(uint16_t));
            Mem().ReadBlock(SID_ORAS, &OT_SID, sizeof(uint16_t));

        } else {
            OT_TID = ReadLE16(buf.data(), 0x0C);
            OT_SID = ReadLE16(buf.data(), 0x0E);
        }

        outPk6->isShiny = ((OT_TID ^ OT_SID ^ pidHigh ^ pidLow) < 16);
    } else {
        outPk6->isShiny = false;
    }

    return true;
}


static uint16_t ReadBattleHPFromSlot(int slot) {

    if (slot < 0 || slot >= PARTY_SIZE)
        return 0xFFFFu;

    uint16_t hp = 0xFFFFu;
    VAddr addr = BATTLE_HP_SLOT_WILD;

    if (PokeExport::isBattleTrainer()) {
        addr = BATTLE_HP_SLOT_TRAINER + slot * BATTLE_HP_SLOT_STRIDE;
    } else {
        addr = BATTLE_HP_SLOT_WILD + slot * BATTLE_HP_SLOT_STRIDE;
    }

    Mem().ReadBlock(addr, reinterpret_cast<uint8_t*>(&hp), sizeof(hp));
    return hp;
}

// Leer un bloque de la memoria de batalla para la ranura 'slot' (usa la dirección detectada)
void ReadBattleSlot(int slot, BattleSlotData* outData) {
    VAddr base = BATTLE_HP_SLOT_WILD;

    if (PokeExport::isBattleTrainer()) {
        base = BATTLE_HP_SLOT_TRAINER + slot * BATTLE_HP_SLOT_STRIDE;

    } else {
        base = BATTLE_HP_SLOT_WILD + slot * BATTLE_HP_SLOT_STRIDE;
    }

    // Leer ability (2 bytes)
    uint16_t ability;
    Mem().ReadBlock(base + 0x006, reinterpret_cast<uint8_t*>(&ability), sizeof(ability));
    outData->ability = ability;

    // Leer species (2 bytes)
    uint16_t species;
    Mem().ReadBlock(base + 0x0E4, reinterpret_cast<uint8_t*>(&species), sizeof(species));
    outData->species = species;

    // Leer level (2 bytes)
    uint16_t level;
    Mem().ReadBlock(base + 0x008, reinterpret_cast<uint8_t*>(&level), sizeof(level));
    outData->level = level;

    // Leer item (2 bytes)
    uint16_t item;
    Mem().ReadBlock(base + 0x002, reinterpret_cast<uint8_t*>(&item), sizeof(item));
    outData->item = item;

    // Leer movimientos (1 byte cada uno)
    VAddr moveOffsets[4] = {0x106, 0x114, 0x122, 0x130};
    VAddr ppOffsets[4] = {0x108, 0x116, 0x124, 0x132};
    for (int i = 0; i < 4; ++i) {
        uint8_t move = 0, pp = 0;
        Mem().ReadBlock(base + moveOffsets[i], &move, 1);
        Mem().ReadBlock(base + ppOffsets[i], &pp, 1);
        outData->moveIDs[i] = move;
        outData->movePP[i] = pp;
    }
}

bool MatchBattleSlotToPk6(const BattleSlotData& b, const Pk6FullData& p, int party_slot, int battle_slot) {
    // Comparar especie, objeto y habilidad
    if (b.species != p.species || b.item != p.item || b.ability != p.ability) {
        LOG_INFO(HW_Memory,
                 "poke_export: No han coincidido en species item o ability party {} con battle {}",
                 party_slot, battle_slot);
        return false;
    }
    // Comparar movimientos y PP actuales
    for (int i = 0; i < 4; ++i) {
        if (b.moveIDs[i] != p.moves[i] || b.movePP[i] != p.currentPP[i]) {
            LOG_INFO(HW_Memory,
                     "poke_export: No han coincidido en movimientos party {} con battle {}",
                     party_slot, battle_slot);
            return false;
        }
    }

    LOG_INFO(HW_Memory, "poke_export: MATCH ENCONTRADO party {} con battle {}", party_slot,
             battle_slot);
    return true;
}

// Función de conversión manual (DEBE DEFINIRSE ANTES DE USARSE)
static bool ConvertToDeadShedinjaInPlaceManual(std::vector<uint8_t>& pkm_data, std::size_t size, bool is_party) {
    if (size < PKM_PARTY_SIZE)
        return false;

    // Primero desencriptamos la parte principal
    if (!DecryptPk6InPlace(pkm_data, size, is_party)) {
        LOG_ERROR(HW_Memory, "poke_export: Error desencriptando PK6 principal");
        return false;
    }

    // Luego desencriptamos los Battle Stats por separado
    if (!DecryptBattleStats(pkm_data, size, is_party)) {
        LOG_ERROR(HW_Memory, "poke_export: Error desencriptando Battle Stats");
        return false;
    }

    // Log para debug - ver datos antes de la conversión
    uint16_t original_species = ReadLE16(pkm_data.data(), OFF_SPECIES);
    uint32_t original_exp = ReadLE32(pkm_data.data(), 0x10);
    uint8_t original_level = pkm_data[0xEC];
    uint16_t original_max_hp = ReadLE16(pkm_data.data(), 0xF2);

    LOG_DEBUG(HW_Memory, "poke_export: Antes - Especie: %u, Exp: %u, Nivel: %u, MaxHP: %u",
              original_species, original_exp, original_level, original_max_hp);

    // === MODIFICAR PARTE PRINCIPAL ===

    // Cambiar PID a 1
    WriteLE32(pkm_data.data(), OFF_PID, 1);

    // Cambiar especie a Shedinja (292)
    WriteLE16(pkm_data.data(), OFF_SPECIES, 292);

    // Habilidad TRUANT (54) - la habilidad de Slaking/Slakoth
    pkm_data[0x14] = 54; // Ability = 54 (Truant)
    pkm_data[0x15] = 1;  // Ability Number = 1

    // EXPERIENCIA A 0 (offset 0x10, 4 bytes)
    WriteLE32(pkm_data.data(), 0x10, 0);

    // Eliminar TODOS los movimientos
    WriteLE16(pkm_data.data(), 0x5A, 0); // Move 1 ID
    WriteLE16(pkm_data.data(), 0x5C, 0); // Move 2 ID
    WriteLE16(pkm_data.data(), 0x5E, 0); // Move 3 ID
    WriteLE16(pkm_data.data(), 0x60, 0); // Move 4 ID

    // PP de movimientos a 0
    pkm_data[0x62] = 0; // Move 1 PP
    pkm_data[0x63] = 0; // Move 2 PP
    pkm_data[0x64] = 0; // Move 3 PP
    pkm_data[0x65] = 0; // Move 4 PP

    // PP Ups a 0
    pkm_data[0x66] = 0;

    // Eliminar movimientos de relearn
    WriteLE16(pkm_data.data(), 0x6A, 0); // Relearn Move 1
    WriteLE16(pkm_data.data(), 0x6C, 0); // Relearn Move 2
    WriteLE16(pkm_data.data(), 0x6E, 0); // Relearn Move 3
    WriteLE16(pkm_data.data(), 0x70, 0); // Relearn Move 4

    // Cambiar nickname a "SHEDINJA"
    std::array<uint8_t, 12> shedinja_name = {0x8C, 0x87, 0x84, 0x83, 0x92, 0x8C,
                                             0x8D, 0x80, 0x00, 0x00, 0x00, 0x00};
    std::copy(shedinja_name.begin(), shedinja_name.end(), pkm_data.begin() + 0x40);

    // OT Name a "AZAHAR"
    std::array<uint8_t, 12> ot_name = {0x80, 0x99, 0x80, 0x87, 0x80, 0x8A,
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::copy(ot_name.begin(), ot_name.end(), pkm_data.begin() + 0xB0);

    // Asegurar que el Pokémon se muestre como genderless
    pkm_data[0x1D] = 0x04; // Gender: genderless (bit 2)

    // Forma alterna a 0
    pkm_data[0x1D] &= 0xF8;

    // Fateful encounter a 0
    pkm_data[0x1D] &= 0xFE;

    // EVs a 0
    for (int i = 0x1E; i <= 0x23; i++) {
        pkm_data[i] = 0;
    }

    // Contest stats a 0
    for (int i = 0x24; i <= 0x29; i++) {
        pkm_data[i] = 0;
    }

    // Pokéball - Poké Ball (0x01)
    pkm_data[0xDC] = 0x01;

    // Met location - Route 7 (0x7C)
    WriteLE16(pkm_data.data(), OFF_METLOCATION, 0x7C);

    // Encounter level 1
    pkm_data[0xDD] = 0x01; // Level 1, OT Female flag = 0

    // IVs - todos 0
    WriteLE32(pkm_data.data(), 0x74, 0);

    // Nature - Hardy (0x00)
    pkm_data[0x1C] = 0x00;

    // Friendship a 0
    pkm_data[0xCA] = 0;

    // === MODIFICAR BATTLE STATS ===

    // NIVEL 1 - Battle Stats section (offset 0xEC)
    pkm_data[0xEC] = 1;

    // HP actual a 0 (muerto) - Battle Stats section (offset 0xF0)
    WriteLE16(pkm_data.data(), OFF_CURRENT_HP, 0);

    // HP máximo a 1 - Battle Stats section (offset 0xF2)
    WriteLE16(pkm_data.data(), 0xF2, 1);

    // También actualizar los stats de batalla restantes para consistencia
    WriteLE16(pkm_data.data(), 0xF4, 1); // Attack
    WriteLE16(pkm_data.data(), 0xF6, 1); // Defense
    WriteLE16(pkm_data.data(), 0xF8, 1); // Speed
    WriteLE16(pkm_data.data(), 0xFA, 1); // Special Attack
    WriteLE16(pkm_data.data(), 0xFC, 1); // Special Defense

    // Recalcular checksum (SOLO para la parte principal 0x08-0x87)
    uint16_t checksum = 0;
    for (std::size_t off = 0x08; off < 0x08 + 224; off += 2) {
        checksum = static_cast<uint16_t>(checksum + ReadLE16(pkm_data.data(), off));
    }
    WriteLE16(pkm_data.data(), 0x06, checksum);

    LOG_DEBUG(HW_Memory, "poke_export: Checksum recalculado: %u", checksum);

    // Primero encriptar la parte principal
    if (!EncryptPk6InPlace(pkm_data, size, is_party)) {
        LOG_ERROR(HW_Memory, "poke_export: Error encriptando parte principal");
        return false;
    }

    // Luego encriptar los Battle Stats por separado
    if (!EncryptBattleStats(pkm_data, size, is_party)) {
        LOG_ERROR(HW_Memory, "poke_export: Error encriptando Battle Stats");
        return false;
    }

    LOG_DEBUG(HW_Memory, "poke_export: Conversión manual completada");
    return true;
}

// Función para cargar Shedinja desde archivo
static std::vector<uint8_t> LoadShedinjaFromFile(const std::filesystem::path& base_path) {
    std::filesystem::path shedinja_path = base_path / "shd.pk6";
    std::ifstream file(shedinja_path, std::ios::binary);

    if (!file.is_open()) {
        LOG_ERROR(HW_Memory, "poke_export: No se pudo abrir el archivo shd.pk6 en {}",
                  shedinja_path.string());
        return {};
    }

    // Leer todo el contenido del archivo
    std::vector<uint8_t> shedinja_data;
    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    shedinja_data.resize(file_size);
    if (!file.read(reinterpret_cast<char*>(shedinja_data.data()), file_size)) {
        LOG_ERROR(HW_Memory, "poke_export: Error leyendo el archivo shd.pk6");
        return {};
    }

    LOG_INFO(HW_Memory, "poke_export: Shedinja cargado desde archivo, tamaño: {} bytes",
             shedinja_data.size());

    return shedinja_data;
}

// Función para preparar Shedinja desde archivo
static bool PrepareShedinjaFromFile(std::vector<uint8_t>& pkm_data, std::size_t size, bool is_party, const std::filesystem::path& base_path) {
    if (size < PKM_PARTY_SIZE)
        return false;

    // Cargar Shedinja desde archivo
    std::vector<uint8_t> shedinja_data = LoadShedinjaFromFile(base_path);
    if (shedinja_data.empty()) {
        LOG_ERROR(HW_Memory, "poke_export: No se pudo cargar Shedinja desde archivo");
        return false;
    }

    // Verificar que el archivo tenga un tamaño válido
    if (shedinja_data.size() != PKM_PARTY_SIZE && shedinja_data.size() != PKM_BOX_SIZE) {
        LOG_ERROR(HW_Memory, "poke_export: Tamaño de archivo shd.pk6 inválido: {} bytes",
                  shedinja_data.size());
        return false;
    }

    // Si el archivo es de box y necesitamos party, usar conversión manual
    if (shedinja_data.size() == PKM_BOX_SIZE && is_party) {
        LOG_INFO(HW_Memory,
                 "poke_export: Archivo de box detectado, usando conversión manual para party");
        return ConvertToDeadShedinjaInPlaceManual(pkm_data, size, is_party);
    }

    // Copiar los datos del Shedinja DESENCRIPTADO directamente
    if (pkm_data.size() != shedinja_data.size()) {
        pkm_data.resize(shedinja_data.size());
    }
    std::memcpy(pkm_data.data(), shedinja_data.data(), shedinja_data.size());

    // === MODIFICAR SOLO LOS CAMPOS ESENCIALES EN DATOS DESENCRIPTADOS ===

    // Battle Stats - estos son críticos
    if (is_party) {
        pkm_data[0xEC] = 1;                  // Nivel 1
        WriteLE16(pkm_data.data(), 0xF0, 0); // HP actual 0 (muerto)
        WriteLE16(pkm_data.data(), 0xF2, 1); // HP máximo 1

        // Stats de batalla básicos
        WriteLE16(pkm_data.data(), 0xF4, 1); // Ataque
        WriteLE16(pkm_data.data(), 0xF6, 1); // Defensa
        WriteLE16(pkm_data.data(), 0xF8, 1); // Velocidad
        WriteLE16(pkm_data.data(), 0xFA, 1); // Ataque Especial
        WriteLE16(pkm_data.data(), 0xFC, 1); // Defensa Especial
    }

    // Campos principales esenciales
    WriteLE16(pkm_data.data(), OFF_SPECIES, 292); // Especie Shedinja
    pkm_data[0x14] = 54;                          // Habilidad Truant
    pkm_data[0x15] = 1;                           // Ability Number

    // Eliminar movimientos
    WriteLE16(pkm_data.data(), 0x5A, 0); // Move 1
    WriteLE16(pkm_data.data(), 0x5C, 0); // Move 2
    WriteLE16(pkm_data.data(), 0x5E, 0); // Move 3
    WriteLE16(pkm_data.data(), 0x60, 0); // Move 4

    // PP de movimientos a 0
    pkm_data[0x62] = 0;
    pkm_data[0x63] = 0;
    pkm_data[0x64] = 0;
    pkm_data[0x65] = 0;

    // Recalcular checksum (SOLO para la parte principal 0x08-0x87)
    uint16_t checksum = 0;
    for (std::size_t off = 0x08; off < 0x08 + 224; off += 2) {
        checksum = static_cast<uint16_t>(checksum + ReadLE16(pkm_data.data(), off));
    }
    WriteLE16(pkm_data.data(), 0x06, checksum);

    LOG_DEBUG(HW_Memory, "poke_export: Checksum recalculado: %u", checksum);

    // NO encriptamos aquí - el juego espera que los datos estén encriptados
    // pero vamos a dejar que el proceso normal de exportación se encargue de eso

    LOG_DEBUG(HW_Memory, "poke_export: Shedinja preparado desde archivo exitosamente");
    return true;
}

// Función principal - VERSIÓN QUE USA ARCHIVO
static bool ConvertToDeadShedinjaInPlace(std::vector<uint8_t>& pkm_data, std::size_t size, bool is_party, const std::filesystem::path& base_path) {
    // Primero intentar con el archivo
    if (PrepareShedinjaFromFile(pkm_data, size, is_party, base_path)) {
        LOG_INFO(HW_Memory, "poke_export: Shedinja inyectado desde archivo exitosamente");
        return true;
    }

    // Si falla, usar conversión manual
    LOG_INFO(HW_Memory, "poke_export: Falló inyección desde archivo, usando conversión manual");
    return ConvertToDeadShedinjaInPlaceManual(pkm_data, size, is_party);
}

// Exporta datos de toda la party a party.txt
[[maybe_unused]]
void ExportPartyDataTxt(const std::string& out_dir) {
    // Crear directorio si no existe
    std::filesystem::create_directories(out_dir);

    // Construir path del archivo
    std::filesystem::path file_path = std::filesystem::path(out_dir) / "party.txt";

    std::ofstream fout(file_path);
    if (!fout.is_open()) {
        LOG_ERROR(HW_Memory, "No se pudo abrir archivo {}", file_path.string());
        return;
    }

    for (int slot = 0; slot < PARTY_SIZE; ++slot) {
        const Pk6FullData& pk6 = g_partyData[slot];

        fout << "===SLOT " << slot << "===\n";
        fout << "PID: " << pk6.pid << "\n";
        fout << "Species: " << pk6.species << "\n";
        fout << "MetLocation: " << static_cast<int>(pk6.metLocation) << "\n";
        fout << "Item: " << pk6.item << "\n";
        fout << "Ability: " << static_cast<int>(pk6.ability) << "\n";

        fout << "Moves: ";
        for (int i = 0; i < 4; ++i) {
            fout << pk6.moves[i];
            if (i < 3)
                fout << ", ";
        }
        fout << "\n";

        fout << "Current PP: ";
        for (int i = 0; i < 4; ++i) {
            fout << static_cast<int>(pk6.currentPP[i]);
            if (i < 3)
                fout << ", ";
        }
        fout << "\n";

        fout << "isShiny: " << std::boolalpha << pk6.isShiny << "\n ";

        fout << "\n\n";
    }

    fout.close();
    LOG_INFO(HW_Memory, "Exportación de party completa -> {}", file_path.string());
}

// Exporta los datos de un Battle Slot a un archivo txt
[[maybe_unused]]
void ExportBattleSlotDataTxt(int battleSlot, const std::filesystem::path& out_dir) {
    if (!isBattle())
        return;
    std::filesystem::create_directories(out_dir);
    std::filesystem::path file_path =
        out_dir / ("battle_slot_" + std::to_string(battleSlot) + ".txt");

    std::ofstream fout(file_path);
    if (!fout.is_open()) {
        LOG_ERROR(HW_Memory, "No se pudo abrir archivo {}", file_path.string());
        return;
    }

    BattleSlotData slotData{};
    ReadBattleSlot(battleSlot, &slotData);

    fout << "===BATTLE SLOT " << battleSlot << "===\n";
    fout << "Species: " << slotData.species << "\n";
    fout << "Item: " << slotData.item << "\n";
    fout << "Ability: " << slotData.ability << "\n";

    fout << "Moves: ";
    for (int i = 0; i < 4; ++i) {
        fout << static_cast<int>(slotData.moveIDs[i]);
        if (i < 3)
            fout << ", ";
    }
    fout << "\n";

    fout << "Current PP: ";
    for (int i = 0; i < 4; ++i) {
        fout << static_cast<int>(slotData.movePP[i]);
        if (i < 3)
            fout << ", ";
    }
    fout << "\n\n";

    fout.close();
    LOG_INFO(HW_Memory, "Exportación de battle slot {} -> {}", battleSlot, file_path.string());
}

static void DeleteFaintedPokemon(const std::filesystem::path& base_path) {
    for (int i = 0; i < 6; ++i) {
        if (g_pending_delete[i]) {
            LOG_INFO(HW_Memory, "poke_export: SLOT %d{} PENDING DELETE ", i);
        } else {
            LOG_INFO(HW_Memory, "poke_export: SLOT %d{} SIGUE VIVO ", i);
        }
    }

    const VAddr party_base = PARTY_ORAS;
    int converted_count = 0;

    for (int battle_slot = 0; battle_slot < PARTY_SIZE; ++battle_slot) {
        if (!g_pending_delete[battle_slot])
            continue;
        // Comparar con los Pokémon en party
        for (int party_slot = 0; party_slot < PARTY_SIZE; ++party_slot) {
            // Leer la PK6 directamente desde memoria
            if (MatchBattleSlotToPk6(g_battleData[battle_slot], g_partyData[party_slot], party_slot, battle_slot)) {
                LOG_INFO(HW_Memory, "poke_export: Convirtiendo slot %d{} en Shedinja muerto",
                         party_slot);

                VAddr slot_addr = party_base + static_cast<VAddr>(party_slot * POKEMON_SLOT_STRIDE);
                std::vector<uint8_t> pkm_data(PKM_PARTY_SIZE);
                Mem().ReadBlock(slot_addr, pkm_data.data(), pkm_data.size());

                if (ConvertToDeadShedinjaInPlace(pkm_data, pkm_data.size(), true, base_path)) {
                    if (WriteBlockStrideWithVerify(slot_addr, pkm_data)) {
                        LOG_DEBUG(HW_Memory,
                                  "poke_export: Slot %d convertido a Shedinja exitosamente",
                                  party_slot);
                        converted_count++;
                    }
                }
                break; // Ya convertimos este Pokémon
            }
        }
    }

    g_pending_delete = {false, false, false, false, false, false};
    LOG_INFO(HW_Memory, "poke_export: OnBattleEnded - %d Pokémon convertidos a Shedinja", converted_count);

}

static void LevelCapAdjustment() {
    fs::path pokemonGrowths = fs::current_path() / "user" / "rtp" / "GrowthRates" / "pokemon";
    fs::path levelCurves = fs::current_path() / "user" / "rtp" / "GrowthRates" / "curves";

    int currentLevelCap = 8;

    for (int battle_slot = 0; battle_slot < PARTY_SIZE; ++battle_slot) {
        if (g_battleData[battle_slot].level >= currentLevelCap) {
            for (int party_slot = 0; party_slot < PARTY_SIZE; ++party_slot) {
                if (MatchBattleSlotToPk6(g_battleData[battle_slot], g_partyData[party_slot],
                                         party_slot, battle_slot)) {
                    int experience =
                        getExperienceForLevel(pokemonGrowths, levelCurves,
                                              g_partyData[party_slot].species, currentLevelCap);

                    VAddr slot_addr =
                        PARTY_ORAS + static_cast<VAddr>(party_slot * POKEMON_SLOT_STRIDE);
                    std::vector<uint8_t> slot_data(POKEMON_SLOT_STRIDE);
                    Mem().ReadBlock(slot_addr, slot_data.data(), slot_data.size());

                    // Trabajar sobre TODO el buffer de party (260 bytes)
                    std::vector<uint8_t> pk6_data(slot_data.begin(),
                                                  slot_data.begin() + PKM_PARTY_SIZE);


                    if (!DecryptPk6InPlace(pk6_data, pk6_data.size(), true)) {
                        continue;
                    }

                    // Modificar experiencia
                    WriteLE32(pk6_data.data(), 0x10, static_cast<uint32_t>(experience));

                    // Recalcular checksum sobre los 224 bytes cifrados (0x08..0xE7)
                    uint16_t checksum = 0;
                    for (size_t off = 0x08; off < 0x08 + 224; off += 2)
                        checksum = static_cast<uint16_t>(checksum + ReadLE16(pk6_data.data(), off));
                    WriteLE16(pk6_data.data(), 0x06, checksum);

                    if (!EncryptPk6InPlace(pk6_data, pk6_data.size(), true)) {
                        continue;
                    }

                    // Copiar TODO el buffer de vuelta a slot_data
                    std::copy(pk6_data.begin(), pk6_data.end(), slot_data.begin());

                    // Escribir de vuelta a memoria
                    Mem().WriteBlock(slot_addr, slot_data.data(), slot_data.size());
                    break;
                }
            }
        }
    }
}

// OnBattleEnded
static void OnBattleEnded(const std::filesystem::path& base_path) {
    PokemonCapture::RestorePokeballs();
    LOG_INFO(HW_Memory, "poke_export: OnBattleEnded - convirtiendo Pokémon muertos en Shedinja");
    DeleteFaintedPokemon(base_path);
    LevelCapAdjustment();
    
}

static void OnBattleStarted(const std::filesystem::path& base_path) {
    LOG_INFO(HW_Memory, "poke_export: BattleStarted");
    PokeExport::ExportWild(PokeExport::Game::ORAS);
    PokemonCapture::RemovePokeballs();
}

// Comprueba transición batalla -> fuera de batalla y llama a OnBattleEnded si toca.
static void CheckBattleTransition(const std::filesystem::path& base_path) {
    bool isBattleNow = isBattle();

    if (!g_wasInBattle && isBattleNow) {
        OnBattleStarted(base_path);
    }

    if (g_wasInBattle && !isBattleNow) {
        OnBattleEnded(base_path);
    }

    g_wasInBattle = isBattleNow;
}

} // namespace

namespace PokeExport {

// Exporta los datos de la party usando g_partyData
bool ExportParty(Game game, const std::string& out_dir) {
    std::filesystem::create_directories(out_dir);
    bool all_ok = true;
    for (int slot = 0; slot < PARTY_SIZE; ++slot) {
        ReadPk6Slot(slot, &g_partyData[slot], false);
        if (isBattle()) {
            ReadBattleSlot(slot, &g_battleData[slot]);
            //ExportBattleSlotDataTxt(slot, out_dir);
            uint16_t currenthp = ReadBattleHPFromSlot(slot);
            if (currenthp == 0 && g_partyData[slot].pid != 0 && g_partyData[slot].species <= 721) {
                g_pending_delete[slot] = true;
            }
        }
    }
    //ExportPartyDataTxt(out_dir);
    CheckBattleTransition(out_dir);
    return all_ok;
}

// Exporta los datos de los Pokémon salvajes usando g_wildData
bool ExportWild(Game game) {
    bool all_ok = true;
    for (int slot = 0; slot < MAX_WILDS; ++slot) {
        ReadPk6Slot(slot, &g_wildData[slot], true);
    }
    return all_ok;
}

// Devuelve Species de un slot del PC
int ReadPk6PCSlot(int pc_slot) {
    std::vector<uint8_t> buf(PKM_BOX_SIZE);
    VAddr slot_addr = PC_ORAS + static_cast<VAddr>(pc_slot * PKM_BOX_SIZE);
    Mem().ReadBlock(slot_addr, buf.data(), buf.size());

    if (!DecryptPk6InPlace(buf, buf.size(), false))
        return 0;
    uint16_t species = ReadLE16(buf.data(), 0x08);
    return species;
}

// Export mapid
uint16_t ExportMapID() {
    const VAddr map_addr = MAPID_ORAS;
    uint8_t buf[2] = {0};
    Mem().ReadBlock(map_addr, buf, 2);
    uint16_t map_id = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
    return map_id;
}

bool isBattleTrainer() {
    VAddr addr = BATTLE_HP_SLOT_WILD;
    // Leer ability (2 bytes)
    uint16_t ability;
    Mem().ReadBlock(addr + 0x006, reinterpret_cast<uint8_t*>(&ability), sizeof(ability));

    // Leer species (2 bytes)
    uint16_t species;
    Mem().ReadBlock(addr + 0x0E4, reinterpret_cast<uint8_t*>(&species), sizeof(species));
    return (species == 0 && ability == 0);
}

} // namespace PokeExport