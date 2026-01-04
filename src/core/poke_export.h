// =========================
// File: src/core/poke_export.h
// =========================

#pragma once

#include <string>
#include <cstdint>

struct Pk6FullData {
    uint32_t pid;        // PID
    uint16_t species;    // Especie
    uint8_t metLocation; // Lugar de captura
    uint16_t item;       // Objeto equipado
    uint8_t ability;     // Habilidad
    uint32_t experience; // Experiencia

    uint16_t moves[4];    // IDs de los movimientos
    uint8_t currentPP[4]; // PP actuales

    bool isShiny;
};

struct BattleSlotData {
    uint16_t species;   // 0x0E4
    uint16_t item;      // 0x002
    uint16_t ability;   // 0x006
    uint16_t level;   // 0x008
    uint16_t moveIDs[4]; // 0x104, 0x110, 0x122, 0x12E
    uint8_t movePP[4];  // PP actual de cada movimiento 0x106, 0x112, 0x124, 0x130 (aprox)
};
//VALORES DE PUNTOS
constexpr int PUNTOS_POR_MUERTE = 50;
constexpr int PUNTOS_POR_WIPE = 100;

// Level caps por medalla 0,1,2,3,4,5,6,7,8 y alto mando, luego sin cap
constexpr int LEVEL_CAPS[11] = {15, 18, 23, 31, 33, 39, 50, 51, 65, 87, 100};


// Party y box
constexpr VAddr PARTY_XY = 0x08CE1CF8u;

constexpr VAddr PARTY_ORAS_V14 = 0x08CFB26Cu;
constexpr VAddr PARTY_ORAS = 0x8cf727cu;

//No cambia con la 1.4
constexpr VAddr WILD_XY = 0x081FF744u;
constexpr VAddr WILD_ORAS = 0x081FFA6Cu;



constexpr VAddr MAPID_ORAS_V14 = 0x8D3A764u;
constexpr VAddr MAPID_ORAS = 0x8D36774u;


constexpr VAddr PC_ORAS = 0x8C9A144u;
constexpr VAddr PC_ORAS_V14 = 0x08C9E134u;



constexpr VAddr TID_ORAS = 0x8C7D350u;
constexpr VAddr TID_ORAS_V14 = 0x8C81340u;
constexpr VAddr SID_ORAS = 0x8C7D352u;
constexpr VAddr SID_ORAS_V14 = 0x8C81342u;




constexpr std::size_t PKM_PARTY_SIZE = 260;
constexpr std::size_t PKM_BOX_SIZE = 232;
constexpr std::size_t POKEMON_SLOT_STRIDE = 484;

constexpr int MAX_WILDS = 5;
constexpr int PARTY_SIZE = 6;
constexpr int PC_SIZE = (31 * 30);

// Offsets PK6
constexpr std::size_t OFF_PID = 0x18;
constexpr std::size_t OFF_SPECIES = 0x08;
constexpr std::size_t OFF_METLOCATION = 0xDA;
constexpr std::size_t OFF_CURRENT_HP = 0xF0;


// Memoria de battle HP (detectadas) //No cambia con la 1.4
constexpr VAddr BATTLE_HP_SLOT_WILD = 0x8204204;
constexpr VAddr BATTLE_HP_SLOT_TRAINER = 0x8205D14;
constexpr uintptr_t BATTLE_HP_SLOT_STRIDE = 580;

// Array para los Pokémon de la party
extern Pk6FullData g_partyData[PARTY_SIZE];

// Array para los datos de batalla de los Pokémon de la party
extern BattleSlotData g_battleData[PARTY_SIZE];

// Array para los Pokémon salvajes
extern Pk6FullData g_wildData[MAX_WILDS];

extern int pokemonCarried;

namespace PokeExport {



enum class Game { XY, ORAS };

bool ExportParty(Game game, const std::string& out_dir);

void CheckDeadPokemon();

bool ExportWild(Game game);

int ReadPk6PCSlot(int pc_slot);

uint16_t ExportMapID();

bool isBattleTrainer();

bool isBattle();




bool SearchMemoryValue(uint16_t value, const std::string& description);
bool ReadCurrentHPValuesOnly(const std::string& description);
bool ForceSearchMemoryValue(uint16_t value, const std::string& description);
void SetHPAddress(uint32_t address);
uint32_t GetConfirmedHPAddress();
void ClearHPHistory();
void DisplayCurrentHPValues();

} // namespace PokeExport