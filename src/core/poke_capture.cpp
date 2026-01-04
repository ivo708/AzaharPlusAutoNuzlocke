#include "poke_capture.h"
#include <memory>
#include <optional>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <set>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <windows.h>

#include "common/logging/log.h"
#include "core/memory.h"
#include "core/core.h"
#include "core/global.h"
#include "poke_export.h"


using namespace Memory;
using namespace PokeExport;

namespace fs = std::filesystem;


namespace PokemonCapture {

MemorySystem& Mem() {
    return Core::Global<Core::System>().Memory();
}

const VAddr POCKET_START = 0x8C6AC80;
const VAddr MEDICINE_POCKET_START = 0x8C6B5F0;

const size_t SLOT_SIZE = 4;

const size_t SLOT_COUNT = 305;
const size_t BLOCK_SIZE = SLOT_SIZE * SLOT_COUNT;

const size_t MEDICINE_SLOT_COUNT = 54;
const size_t MEDICINE_BLOCK_SIZE = SLOT_SIZE * MEDICINE_SLOT_COUNT;

const std::set<uint16_t> legendarySpecies = {
    144, 145, 146, 150, 151,
                                       243, 244, 245, 249, 250, 251,
                                       377, 378, 379, 380, 381, 382, 383, 384, 385, 386,
                                       480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493,
                                       494, 638, 639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649,
                                       716, 717, 718, 719, 720, 721
                                      };

const int evolutionLines[722] = {
    0,
    1,   1,   1,   2,   2,   2,   3,   3,   3,   4,   4,   4,   5,   5,   5,   6,   6,   6,   7,
    7,   8,   8,   9,   9,   10,  10,  11,  11,  12,  12,  12,  13,  13,  13,  14,  14,  15,  15,
    16,  16,  17,  17,  18,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  23,  24,  24,
    25,  25,  26,  26,  26,  27,  27,  27,  28,  28,  28,  29,  29,  29,  30,  30,  31,  31,  31,
    32,  32,  33,  33,  34,  34,  35,  36,  36,  37,  37,  38,  38,  39,  39,  40,  40,  40,  41,
    42,  42,  43,  43,  44,  44,  45,  45,  46,  46,  47,  47,  48,  49,  49,  50,  50,  51,  52,
    53,  54,  54,  55,  55,  56,  56,  57,  58,  59,  60,  61,  62,  63,  64,  64,  65,  66,  67,
    67,  67,  67,  68,  69,  69,  70,  70,  71,  72,  73,  74,  75,  76,  76,  76,  77,  78,  79,
    79,  79,  80,  80,  80,  81,  81,  81,  82,  82,  83,  83,  84,  84,  85,  85,  17,  86,  86,
    10,  14,  16,  87,  87,  88,  88,  89,  89,  89,  18,  90,  90,  91,  26,  92,  92,  92,  93,
    94,  94,  95,  96,  96,  67,  67,  97,  33,  98,  99,  100, 101, 102, 102, 103, 104, 41,  105,
    105, 106, 58,  107, 108, 109, 110, 110, 111, 111, 112, 112, 113, 114, 114, 115, 116, 117, 118,
    118, 54,  119, 119, 68,  120, 121, 47,  47,  59,  60,  61,  122, 51,  123, 124, 125, 126, 126,
    126, 127, 128, 129, 130, 130, 130, 131, 131, 131, 132, 132, 132, 133, 133, 134, 134, 135, 135,
    135, 135, 135, 136, 136, 136, 137, 137, 137, 138, 138, 139, 139, 140, 140, 140, 141, 141, 142,
    142, 143, 143, 143, 144, 144, 144, 145, 145, 145, 146, 146, 90,  147, 148, 148, 149, 150, 151,
    151, 151, 152, 152, 153, 153, 154, 155, 156, 157, 158, 159, 159, 160, 160, 161, 161, 162, 162,
    163, 164, 164, 165, 166, 166, 166, 167, 167, 168, 168, 169, 170, 171, 172, 173, 173, 174, 174,
    175, 175, 176, 176, 177, 177, 178, 178, 179, 180, 181, 181, 182, 182, 183, 184, 185, 100, 186,
    186, 187, 187, 187, 188, 188, 188, 189, 190, 191, 191, 191, 192, 192, 192, 193, 194, 195, 196,
    197, 198, 199, 200, 201, 202, 203, 203, 203, 204, 204, 204, 205, 205, 205, 206, 206, 206, 207,
    207, 208, 208, 209, 209, 209, 158, 158, 211, 211, 212, 212, 213, 213, 213, 214, 214, 215, 216,
    216, 217, 217, 218, 218, 93,  219, 219, 220, 220, 98,  97,  221, 221, 184, 223, 223, 224, 224,
    91,  57,  51,  228, 229, 230, 230, 230, 72,  232, 232, 233, 233, 234, 234, 235, 235, 236, 237,
    237, 116, 239, 239, 109, 34,  48,  50,  52,  60,  61,  87,  95,  67,  67,  104, 112, 68,  140,
    147, 182, 186, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 250, 252, 253, 254, 255,
    256, 256, 256, 257, 257, 257, 258, 258, 258, 259, 259, 260, 260, 260, 261, 261, 262, 262, 263,
    263, 264, 264, 265, 265, 266, 266, 266, 267, 267, 268, 268, 268, 269, 269, 270, 270, 271, 272,
    272, 272, 273, 273, 273, 274, 275, 276, 276, 276, 277, 277, 277, 278, 278, 279, 279, 280, 281,
    281, 281, 282, 282, 283, 284, 284, 285, 285, 286, 287, 287, 288, 288, 289, 289, 290, 290, 291,
    291, 292, 292, 293, 293, 293, 294, 294, 294, 295, 295, 296, 296, 296, 297, 297, 298, 299, 299,
    300, 300, 301, 301, 302, 303, 303, 304, 304, 305, 305, 305, 306, 306, 306, 307, 307, 308, 308,
    308, 309, 309, 309, 310, 310, 311, 312, 312, 313, 314, 314, 315, 316, 316, 317, 317, 318, 319,
    319, 320, 320, 321, 322, 323, 323, 323, 324, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333,
    334, 335, 336, 337, 337, 337, 338, 338, 338, 339, 339, 339, 340, 340, 341, 341, 341, 342, 342,
    342, 343, 343, 344, 344, 344, 345, 345, 346, 346, 347, 348, 348, 349, 349, 349, 350, 350, 351,
    351, 352, 352, 353, 353, 354, 354, 355, 355, 356, 356, 357, 357, 358, 358, 67,  359, 360, 361,
    362, 362, 362, 363, 364, 364, 365, 365, 366, 366, 367, 367, 368, 369, 370, 371, 372, 373};

std::set<uint16_t> g_visitedRoutes;

std::unordered_set<int> g_caught;




struct ItemEntry {
    uint16_t id;
    uint16_t qty;
};
static ItemEntry g_MainPocket[SLOT_COUNT];

static ItemEntry g_RemovedPokeBalls[17];

//Purga slots vacios de un array
void RemoveEmptySlots(ItemEntry arr[], size_t count) {
    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < count; ++readIndex) {
        if (arr[readIndex].id != 0) {
            // Copiar slot válido a la posición correcta
            if (writeIndex != readIndex) {
                arr[writeIndex] = arr[readIndex];
            }
            writeIndex++;
        }
    }

    // Vaciar el resto
    for (size_t i = writeIndex; i < count; ++i) {
        arr[i] = ItemEntry{};
    }
}

//Guarda set de rutas a archivo binario
void saveVisitedRoutes() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    fs::path filePath = exeDir / "user" / "rtp" / "p" / "visRou.bin";
    fs::create_directories(filePath.parent_path());

    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile) {
        LOG_INFO(HW_Memory, "poke_capture: ERROR AL ESCRIBIR EL ARCHIVO");
        return;
    }

    uint64_t size = g_visitedRoutes.size();
    outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));

    for (auto val : g_visitedRoutes) {
        outFile.write(reinterpret_cast<const char*>(&val), sizeof(val));
    }
    outFile.close();
}

//Cargar set de rutas desde archivo binario
void loadVisitedRoutes() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    fs::path filePath = exeDir / "user" / "rtp" / "p" / "visRou.bin";

    std::ifstream inFile(filePath, std::ios::binary);
    if (!inFile) {
        LOG_INFO(HW_Memory, "poke_capture: ERROR AL ABRIR EL ARCHIVO");
        return;
    }

    uint64_t size;
    inFile.read(reinterpret_cast<char*>(&size), sizeof(size));

    g_visitedRoutes.clear(); // Limpiar antes de cargar
    for (uint64_t i = 0; i < size; ++i) {
        uint16_t val;
        inFile.read(reinterpret_cast<char*>(&val), sizeof(val));
        g_visitedRoutes.insert(val);
        LOG_INFO(HW_Memory, "poke_capture: CARGANDO VALORES DESDE EL ARCHIVO: {}",val);

    }

    inFile.close();
}




void loadCaughtPokemon() {
    g_caught.clear();
    int species;
    for (int i = 0; i < PC_SIZE; i++) {
        species = PokeExport::ReadPk6PCSlot(i);
        if (species != 0) {
            g_caught.insert(species);
        }
    }
    for (int i = 0; i < PARTY_SIZE; i++) {
        species = g_partyData[i].species;
        if (species != 0) {
            g_caught.insert(species);
        }
    }
    
}

bool hasEvolutionLine(int species) {
    int evolutionLine = evolutionLines[species];
    for (int c : g_caught) {
        if (evolutionLines[c] == evolutionLine) {
            return true;
        }
    }
    return false;
}


[[maybe_unused]]
bool ExportMainPocketToTxt() {
    namespace fs = std::filesystem;

    // Construir ruta de exportación

    fs::path export_dir = fs::current_path() / "user" / "rtp" / "p";

    // Crear directorios si no existen
    std::error_code ec;
    fs::create_directories(export_dir, ec);
    if (ec)
        return false; // fallo al crear directorio

    fs::path file_path = export_dir / "MainPocket.txt";

    std::ofstream ofs(file_path);
    if (!ofs.is_open())
        return false;

    // Encabezado opcional
    ofs << "Slot\tItemID\tQuantity\n";

    // Escribir cada slot
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        ofs << i << '\t' << g_MainPocket[i].id << '\t' << g_MainPocket[i].qty << '\n';
    }

    ofs.close();
    return true;
}



bool ShouldRemove() {
    LOG_INFO(HW_Memory, "poke_capture: ShouldRemove()");

    //EPISODIO DELTA NO
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    std::filesystem::path rutaDEp_Map = exeDir / "user" / "rtp" / "p" / "DEp_Map.bin";
    if (fs::exists(rutaDEp_Map)) {
        return false;
    } else {
        LOG_INFO(HW_Memory, "DEp_Map no existe");
    }

    //Contra entrenadores NO
    if (PokeExport::isBattleTrainer()) {
        LOG_INFO(HW_Memory, "poke_capture: COMBATE CONTRA ENTRENADOR");
        return false;
    }

    loadVisitedRoutes();
    uint16_t currentMapID = PokeExport::ExportMapID();
    LOG_INFO(HW_Memory, "poke_capture: MAPA ACTUAL: {}", currentMapID);

    // Si hay Shiny NO (Pero se marca la ruta)
    for (int i = 0; i < MAX_WILDS; i++) {
        if (g_wildData[i].isShiny) {
            LOG_INFO(HW_Memory, "poke_capture: POKEMON SALVAJE SHINY");
            if (PokeExport::isBattle()) {
                LOG_INFO(HW_Memory, "poke_capture: MARCANDO RUTA ACTUAL COMO VISITADA: {}",
                         currentMapID);
                g_visitedRoutes.insert(currentMapID);
                saveVisitedRoutes();
            }
            return false;
        }
    }

    //Si hay legendarios SI (Antes de marcar ruta como explorada)
    for (int i = 0; i < MAX_WILDS; i++) {
        //Comprueba si la especie esta en el set de especies de legendarios
        if (legendarySpecies.find(g_wildData[i].species) != legendarySpecies.end()) {
            LOG_INFO(HW_Memory, "poke_capture: POKEMON LEGENDARIO");
            return true;
        }
    }

    //Si el jugador ya tiene a un pokemon de esa familia evolutiva SI (no se marca la ruta)
    for (int i = 0; i < MAX_WILDS; i++) {
        // Comprueba si la especie esta en el set de especies de legendarios
        if (hasEvolutionLine(g_wildData[i].species) && g_wildData[i].species != 0) {
            LOG_INFO(HW_Memory, "poke_capture: POKEMON YA CAPTURADO (LINEA EVOLUTIVA)");
            return true;
        }
    }

    // Ruta ya visitada SI
    if (g_visitedRoutes.find(currentMapID) != g_visitedRoutes.end()) {
        LOG_INFO(HW_Memory, "poke_capture: RUTA YA VISITADA");
        return true;
    }

    //Si llega aqui significa que es contra pokemon salvaje, ruta nueva y no hay shiny ni legendario.
    std::filesystem::path firstPokeballFlag = exeDir / "user" / "rtp" / "p" / "haspkb.bin";
    if (fs::exists(firstPokeballFlag)) { //Solo marcar las rutas cuando el jugador haya obtenido las primeras PokeBall

        if (PokeExport::isBattle()) {
            LOG_INFO(HW_Memory, "poke_capture: MARCANDO RUTA ACTUAL COMO VISITADA: {}",  currentMapID);
            g_visitedRoutes.insert(currentMapID);
            saveVisitedRoutes();
        }
    }
    saveVisitedRoutes();
    LOG_INFO(HW_Memory, "poke_capture: NADA RELEVANTE PARA LA CAPTURA");
    return false;
}

//Quita las PokeBall
bool RemovePokeballs() {
    LOG_INFO(HW_Memory, "poke_capture: RemovePokeBalls()");

    //Cargar todos los pokemon capturados
    loadCaughtPokemon();

    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    std::filesystem::path firstPokeballFlag = exeDir / "user" / "rtp" / "p" / "haspkb.bin";

    //si no existe la flag, comprobar si hay que crearla
    if (!fs::exists(firstPokeballFlag)) {
        std::vector<uint8_t> buffer(BLOCK_SIZE);
        Mem().ReadBlock(POCKET_START, buffer.data(), BLOCK_SIZE);
        for (size_t i = 0; i < SLOT_COUNT; ++i) {
            size_t off = i * 4;
            uint16_t id = buffer[off] | (buffer[off + 1] << 8);
            uint16_t qty = buffer[off + 2] | (buffer[off + 3] << 8);
            if (id != 0 && id <= 16) {// Pokeball
                if (qty != 0) {
                    std::ofstream file(firstPokeballFlag, std::ios::binary);
                    break;
                }
            }
        }
    }

    if (!ShouldRemove()) {
        LOG_INFO(HW_Memory, "poke_capture: NO SE VAN A QUITAR LAS POKEBALL");
        return false;
    }
    LOG_INFO(HW_Memory, "poke_capture: SI SE VAN A QUITAR LAS POKEBALL");

    std::vector<uint8_t> buffer(BLOCK_SIZE);
    Mem().ReadBlock(POCKET_START, buffer.data(), BLOCK_SIZE);

    // Parsear cada slot en id y cantidad y guardarlo en arrays
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        size_t off = i * 4;
        uint16_t id = buffer[off] | (buffer[off + 1] << 8);
        uint16_t qty = buffer[off + 2] | (buffer[off + 3] << 8);
        //LOG_INFO(HW_Memory, "poke_capture: ITEM->  ID: {} QTY: {}", id, qty);

        if (id != 0 && id <= 16) { // Si es una PokeBall 
            g_RemovedPokeBalls[id].id = id;
            g_RemovedPokeBalls[id].qty = qty;
            g_MainPocket[i].id = 0;
            g_MainPocket[i].qty = 0;
        }
        else if (id == 576) { // Si es una PokeBall
            g_RemovedPokeBalls[16].id = id;
            g_RemovedPokeBalls[16].qty = qty;
            g_MainPocket[i].id = 0;
            g_MainPocket[i].qty = 0;
        } else {
            g_MainPocket[i].id = id;
            g_MainPocket[i].qty = qty;
        }
    }
    RemoveEmptySlots(g_MainPocket, SLOT_COUNT);
    //Devolver a memoria con los cambios hechos 
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        size_t off = i * SLOT_SIZE;
        buffer[off + 0] = g_MainPocket[i].id & 0xFF;
        buffer[off + 1] = g_MainPocket[i].id >> 8;
        buffer[off + 2] = g_MainPocket[i].qty & 0xFF;
        buffer[off + 3] = g_MainPocket[i].qty >> 8;
    }
    // Escribir el bloque completo en memoria
    LOG_INFO(HW_Memory, "poke_export: Escribiendo en memoria removePokeball");
    Mem().WriteBlock(POCKET_START, buffer.data(), BLOCK_SIZE);
    //ExportMainPocketToTxt();
    return true;
}

void RemoveCandies() {
    LOG_INFO(HW_Memory, "poke_capture: RemoveCandies()");

    std::vector<uint8_t> buffer(MEDICINE_BLOCK_SIZE);
    Mem().ReadBlock(MEDICINE_POCKET_START, buffer.data(), MEDICINE_BLOCK_SIZE);
    ItemEntry bolsillo_medicina[MEDICINE_SLOT_COUNT];
    // Parsear cada slot en id y cantidad y guardarlo en arrays
    for (size_t i = 0; i < MEDICINE_SLOT_COUNT; ++i) {
        size_t off = i * SLOT_SIZE;
        uint16_t id = buffer[off] | (buffer[off + 1] << 8);
        uint16_t qty = buffer[off + 2] | (buffer[off + 3] << 8);
        LOG_INFO(HW_Memory, "poke_capture: id: {}, qty: {}", id, qty);

        if (id == 50) {
            bolsillo_medicina[i].id = 0;
            bolsillo_medicina[i].qty = 0;
        } else {
            bolsillo_medicina[i].id = id;
            bolsillo_medicina[i].qty = qty;
        }
    }
    RemoveEmptySlots(bolsillo_medicina, MEDICINE_SLOT_COUNT);
    // Devolver a memoria con los cambios hechos
    for (size_t i = 0; i < MEDICINE_SLOT_COUNT; ++i) {

        size_t off = i * SLOT_SIZE;
        buffer[off + 0] = bolsillo_medicina[i].id & 0xFF;
        buffer[off + 1] = bolsillo_medicina[i].id >> 8;
        buffer[off + 2] = bolsillo_medicina[i].qty & 0xFF;
        buffer[off + 3] = bolsillo_medicina[i].qty >> 8;
    }
    LOG_INFO(HW_Memory, "poke_export: Escribiendo en memoria removeCandies");
    Mem().WriteBlock(MEDICINE_POCKET_START, buffer.data(), MEDICINE_BLOCK_SIZE);
    return;
}

// Devuelve las PokeBalls
bool RestorePokeballs() {
    // Devolver las Pokeball
    size_t insertCount = 0;
    for (size_t i = 0; i < 17; ++i) {
        if (g_RemovedPokeBalls[i].qty != 0) {
            insertCount++;
        }
    }

    if (insertCount == 0) {
        std::memset(g_MainPocket, 0, sizeof(g_MainPocket));
        std::memset(g_RemovedPokeBalls, 0, sizeof(g_RemovedPokeBalls));
        return true;
    }

    std::vector<uint8_t> buffer(BLOCK_SIZE);
    Mem().ReadBlock(POCKET_START, buffer.data(), BLOCK_SIZE);

    // Parsear cada slot en id y cantidad y guardarlo en arrays (por si se han gastado items)
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        size_t off = i * 4;
        uint16_t id = buffer[off] | (buffer[off + 1] << 8);
        uint16_t qty = buffer[off + 2] | (buffer[off + 3] << 8);
        g_MainPocket[i].id = id;
        g_MainPocket[i].qty = qty;
    }


    // Desplazar MainPocket hacia abajo para dejar espacio
    // memmove permite solapamiento sin problemas
    memmove(&g_MainPocket[insertCount], // destino
            &g_MainPocket[0],           // origen
            (SLOT_COUNT - insertCount) * sizeof(ItemEntry));

    // Insertar los removed balls al inicio
    size_t writeIndex = 0;
    for (size_t i = 0; i < 17; ++i) {
        if (g_RemovedPokeBalls[i].qty != 0) {
            g_MainPocket[writeIndex++] = g_RemovedPokeBalls[i];
        }
    }


    // Devolver a memoria con los cambios hechos
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        size_t off = i * SLOT_SIZE;
        buffer[off + 0] = g_MainPocket[i].id & 0xFF;
        buffer[off + 1] = g_MainPocket[i].id >> 8;
        buffer[off + 2] = g_MainPocket[i].qty & 0xFF;
        buffer[off + 3] = g_MainPocket[i].qty >> 8;
    }
    // Escribir el bloque completo en memoria
    LOG_INFO(HW_Memory, "poke_export: Escribiendo en memoria restorePokeball");
    Mem().WriteBlock(POCKET_START, buffer.data(), BLOCK_SIZE);

    //Vaciar los arrays globales
    std::memset(g_MainPocket, 0, sizeof(g_MainPocket));
    std::memset(g_RemovedPokeBalls, 0, sizeof(g_RemovedPokeBalls));

    return true;
}

} // namespace PokemonCapture
