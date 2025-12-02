#include <memory>
#include <optional>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <set>
#include <cstdint>
#include <iostream>

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

const u32 POCKET_START = 0x8C6EC70;
const size_t SLOT_SIZE = 4;
const size_t SLOT_COUNT = 270;
const size_t BLOCK_SIZE = SLOT_SIZE * SLOT_COUNT;

std::set<uint16_t> legendarySpecies = {144, 145, 146, 150, 151,
                                       243, 244, 245, 249, 250, 251,
                                       377, 378, 379, 380, 381, 382, 383, 384, 385, 386,
                                       480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493,
                                       494, 638, 639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649,
                                       716, 717, 718, 719, 720, 721
                                      };

std::set<uint16_t> g_visitedRoutes;




struct ItemEntry {
    uint16_t id;
    uint16_t qty;
};
static ItemEntry g_MainPocket[270];

static ItemEntry g_RemovedPokeBalls[16];

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
    fs::path filePath = fs::current_path() / "user" / "rtp" / "p" / "visRou.bin";
    fs::create_directories(filePath.parent_path()); // Crear carpetas si no existen

    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile) {
        std::cerr << "Error al abrir el archivo para escribir\n";
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
    fs::path filePath = fs::current_path() / "user" / "rtp" / "p" / "visRou.bin";

    std::ifstream inFile(filePath, std::ios::binary);
    if (!inFile) {
        std::cerr << "Error al abrir el archivo para leer\n";
        return;
    }

    uint64_t size;
    inFile.read(reinterpret_cast<char*>(&size), sizeof(size));

    g_visitedRoutes.clear(); // Limpiar antes de cargar
    for (uint64_t i = 0; i < size; ++i) {
        uint16_t val;
        inFile.read(reinterpret_cast<char*>(&val), sizeof(val));
        g_visitedRoutes.insert(val);
    }

    inFile.close();
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
    //Contra entrenadores NO
    if (PokeExport::isBattleTrainer()) {
        LOG_INFO(HW_Memory, "poke_capture: COMBATE CONTRA ENTRENADOR");
        return false;
    }

    //Si hay legendarios SI (Antes de marcar ruta como explorada)
    for (int i = 0; i < MAX_WILDS; i++) {
        //Comprueba si la especie esta en el set de especies de legendarios
        if (legendarySpecies.find(g_wildData[i].species) != legendarySpecies.end()) {
            LOG_INFO(HW_Memory, "poke_capture: POKEMON LEGENDARIO");
            return true;
        }
    }
    
    loadVisitedRoutes();
    uint16_t currentMapID = PokeExport::ExportMapID();

    //Si hay Shiny NO (Pero se marca la ruta)
    for (int i = 0; i < MAX_WILDS; i++) {
        if (g_wildData[i].isShiny) {
            LOG_INFO(HW_Memory, "poke_capture: POKEMON SALVAJE SHINY");
            g_visitedRoutes.insert(currentMapID);
            saveVisitedRoutes();
            return false;
        }
    }

    // Ruta ya visitada SI
    if (g_visitedRoutes.find(currentMapID) != g_visitedRoutes.end()) {
        LOG_INFO(HW_Memory, "poke_capture: RUTA YA VISITADA");
        return true;
    }

    //Si llega aqui significa que es contra pokemon salvaje, ruta nueva y no hay shiny ni legendario.
    g_visitedRoutes.insert(currentMapID);
    saveVisitedRoutes();
    LOG_INFO(HW_Memory, "poke_capture: NADA RELEVANTE PARA LA CAPTURA");

    return false;
}

//Quita las PokeBall
bool RemovePokeballs() {
    LOG_INFO(HW_Memory, "poke_capture: RemovePokeBalls()");
    if (!ShouldRemove()) {
        LOG_INFO(HW_Memory, "poke_capture: NO SE VAN A QUITAR LAS POKEBALL");
        return false;
    }
    LOG_INFO(HW_Memory, "poke_capture: SI SE VAN A QUITAR LAS POKEBALL");

    std::vector<uint8_t> buffer(BLOCK_SIZE);
    Mem().ReadBlock(POCKET_START, buffer.data(), BLOCK_SIZE);

    // Parsear cada slot en id y cantidad y guardarlo en arrays
    for (size_t i = 0; i < 270; ++i) {
        size_t off = i * 4;
        uint16_t id = buffer[off] | (buffer[off + 1] << 8);
        uint16_t qty = buffer[off + 2] | (buffer[off + 3] << 8);
        if (id != 0 && id <= 16) { // Si es una PokeBall 
            g_RemovedPokeBalls[id].id = id;
            g_RemovedPokeBalls[id].qty = qty;
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
    Mem().WriteBlock(POCKET_START, buffer.data(), BLOCK_SIZE);

    ExportMainPocketToTxt();


    return true;
}


// Devuelve las PokeBalls
bool RestorePokeballs() {
    if (!ShouldRemove())
        return false;

    // Devolver las Pokeball
    size_t insertCount = 0;
    for (size_t i = 0; i < 16; ++i) {
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
    for (size_t i = 0; i < 270; ++i) {
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
            (270 - insertCount) * sizeof(ItemEntry));

    // Insertar los removed balls al inicio
    size_t writeIndex = 0;
    for (size_t i = 0; i < 16; ++i) {
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
    Mem().WriteBlock(POCKET_START, buffer.data(), BLOCK_SIZE);

    //Vaciar los arrays globales
    std::memset(g_MainPocket, 0, sizeof(g_MainPocket));
    std::memset(g_RemovedPokeBalls, 0, sizeof(g_RemovedPokeBalls));

    return true;
}

} // namespace PokemonCapture
