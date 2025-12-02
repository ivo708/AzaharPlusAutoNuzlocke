#include "poke_antirq.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/archive_source_sd_savedata.h"
#include "core/global.h"
#include "core/loader/ncch.h"
#include "core/savestate.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace PokeAntiRq {

std::string GetCurrentTitleID() {
    return Loader::getProgramId();
}

uint64_t GetCurrentTitleID_u64() {
    std::string id = GetCurrentTitleID();
    if (id.empty())
        return 0;

    return std::stoull(id, nullptr, 16);
}

std::string GetCurrentSaveDataPath() {

    uint64_t program_id = GetCurrentTitleID_u64();
    if (program_id == 0)
        return "";

    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
        return "";

    std::wstring pathW(exePath);

    // Carpeta base del emulador
    size_t pos = pathW.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        pathW = pathW.substr(0, pos);

    // Añadir "user/sdmc"
    pathW += L"\\user\\sdmc";

    // Obtener ruta relativa dentro de sdmc (sin /sdmc inicial)
    std::string relativePath =
        FileSys::ArchiveSource_SDSaveData::GetSaveDataPathFor("/", program_id);

    // Unir rutas
    std::filesystem::path fullPath(pathW);
    fullPath /= std::filesystem::path(relativePath).relative_path();

    std::string finalPath = fullPath.string();

    LOG_INFO(HW_Memory, "poke_antirq: Archivo de guardado: {}", finalPath);
    return finalPath;
}


std::string GetCurrentSaveStatePath() {
    std::string titleID = GetCurrentTitleID();
    if (titleID.empty())
        return "";

    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
        return "";

    fs::path saveStatePath(exePath);
    saveStatePath = saveStatePath.parent_path(); // quitar el exe
    saveStatePath /= L"user/states";
    saveStatePath /= std::wstring(titleID.begin(), titleID.end()) + L".00.cst";

    LOG_INFO(HW_Memory, "poke_antirq: Archivo de saveState: {}", saveStatePath.string());
    return saveStatePath.string();

}

void onStart() {
    // Obtener ruta del savestate

    LOG_INFO(HW_Memory, "poke_antirq:TitleID: {}", GetCurrentTitleID());
    std::string saveStatePath = GetCurrentSaveStatePath();

    if (!fs::exists(saveStatePath)) {
        LOG_INFO(HW_Memory, "poke_antirq: No existe savestate en {}", saveStatePath);
        return;
    }

    LOG_INFO(HW_Memory, "poke_antirq: Savestate encontrado en {}, enviando señal Load...", saveStatePath);

    auto& system = Core::Global<Core::System>();

    // Intentar enviar la señal Load
    if (!system.SendSignal(Core::System::Signal::Load, 0)) {
        LOG_ERROR(HW_Memory, "poke_antirq: No se pudo enviar la señal Load (otro signal en curso)");
        return;
    }
}


void onClose() {
    LOG_INFO(HW_Memory, "poke_antirq: ===== INICIO onClose() =====");

    fs::path saveFilePath = fs::path(GetCurrentSaveDataPath()) / "main";

    if (!fs::exists(saveFilePath)) {
        LOG_INFO(HW_Memory, "poke_antirq: No existe archivo de guardado: {}",
                 saveFilePath.string());
        LOG_INFO(HW_Memory, "poke_antirq: ===== FIN onClose() =====");
        return;
    }
    // Obtener la última fecha de modificación (file_time_type)
    auto last_write = fs::last_write_time(saveFilePath);

    // Obtener "ahora" en ambas referencias de tiempo
    auto now_sys = std::chrono::system_clock::now();
    auto now_file = fs::file_time_type::clock::now();

    // Diferencia entre last_write y el "ahora" del clock de file_time_type
    auto delta_file = last_write - now_file; // duration en file clock

    // Convertir esa duración al tipo de duración de system_clock
    auto delta_sys = std::chrono::duration_cast<std::chrono::system_clock::duration>(delta_file);

    // Construir time_point en system_clock equivalente a last_write
    auto file_time_as_system = std::chrono::system_clock::time_point(now_sys.time_since_epoch() + delta_sys);

    // Tiempo transcurrido desde la última modificación
    auto elapsed = now_sys - file_time_as_system;
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

    LOG_INFO(HW_Memory, "poke_antirq: Última modificación hace {} segundos", elapsed_seconds);

    const std::chrono::seconds permittedTime(45);

    if (elapsed < permittedTime) {

        LOG_INFO(HW_Memory,
                 "poke_antirq: No se guarda savestate (ultima modificacion hace menos de {} s).",
                 permittedTime.count());

    } else {

        LOG_INFO(HW_Memory, "poke_antirq: Guardando Savestate ...");
        auto& system = Core::Global<Core::System>();

        if (!system.SendSignal(Core::System::Signal::Save, 0)) {
            LOG_ERROR(HW_Memory, "No se pudo enviar la señal Save");
        } else {
            std::string saveStatePath = GetCurrentSaveStatePath();
            const int timeout_ms = 5000;
            int waited = 0;
            using namespace std::chrono_literals;

            while (!fs::exists(saveStatePath) && waited < timeout_ms) {
                std::this_thread::sleep_for(10ms);
                waited += 10;
            }
        }
    }
    LOG_INFO(HW_Memory, "poke_antirq:TitleID: {}", GetCurrentTitleID());

    LOG_INFO(HW_Memory, "poke_antirq: ===== FIN onClose() =====");
}

} // namespace PokeAntiRq
