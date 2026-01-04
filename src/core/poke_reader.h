#pragma once
#include <cstddef>
#include <nlohmann/json.hpp>


namespace PokeReader {

	extern nlohmann::json pointsFile;

	void getPointsFile();
    void addPoints(int points);
    void addMuerte();

	std::vector<uint8_t> XorBytes(const std::vector<uint8_t>& data, const std::string& key);

	std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path);
    void WriteFileBytes(const std::filesystem::path& path, const std::vector<uint8_t>& data);


} // namespace PokeReader
