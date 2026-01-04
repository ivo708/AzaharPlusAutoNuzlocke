#include "poke_reader.h"
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <set>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <windows.h>

#include "common/logging/log.h"
#include "core/memory.h"
#include "core/core.h"
#include "core/global.h"
#include "poke_export.h"




namespace fs = std::filesystem;




namespace PokeReader {

nlohmann::json pointsFile;

namespace fs = std::filesystem;

void getPointsFile() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    std::filesystem::path filePath = exeDir / "user" / "rtp" / "p" / "pts.json";
    auto bytes = ReadFileBytes(filePath);
    auto bytesDecrypted = XorBytes(bytes, "ivo708");

    std::string jsonText(bytesDecrypted.begin(), bytesDecrypted.end());

    pointsFile = nlohmann::json::parse(jsonText);
}

[[maybe_unused]]
void addPoints(int points) {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    std::filesystem::path filePath = exeDir / "user" / "rtp" / "p" / "pts.json";
    getPointsFile();
    int current = pointsFile.value("puntos", 0);
    pointsFile["puntos"] = current + points;
    std::string jsonDump = pointsFile.dump();
    auto bytes = std::vector<uint8_t>(jsonDump.begin(), jsonDump.end());
    auto bytesEncrypted = XorBytes(bytes, "ivo708");
    WriteFileBytes(filePath, bytesEncrypted);
}

[[maybe_unused]]
void addMuerte() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exeDir(buf);
    exeDir = exeDir.parent_path();
    std::filesystem::path filePath = exeDir / "user" / "rtp" / "p" / "pts.json";
    getPointsFile();
    int current = pointsFile.value("muertes", 0);
    pointsFile["muertes"] = current + 1;
    std::string jsonDump = pointsFile.dump();
    auto bytes = std::vector<uint8_t>(jsonDump.begin(), jsonDump.end());
    auto bytesEncrypted = XorBytes(bytes, "ivo708");
    WriteFileBytes(filePath, bytesEncrypted);
}

std::vector<uint8_t> XorBytes(const std::vector<uint8_t>& data, const std::string& key) {
    const uint8_t* keyBytes = reinterpret_cast<const uint8_t*>(key.data());
    size_t keyLen = key.size();

    std::vector<uint8_t> output(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        output[i] = data[i] ^ keyBytes[i % keyLen];
    }

    return output;
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {}; // archivo no existe
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

void WriteFileBytes(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return; // no se pudo abrir
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}


} // namespace PokeReader
