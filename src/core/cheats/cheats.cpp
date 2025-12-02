// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fstream>
#include <functional>
#include <fmt/format.h>
#include "common/file_util.h"
#include "core/cheats/cheats.h"
#include "core/cheats/gateway_cheat.h"
#include "core/core.h"
#include "core/core_timing.h"

namespace Cheats {

// Luma3DS uses this interval for applying cheats, so to keep consistent behavior
// we use the same value
constexpr u64 run_interval_ticks = 50'000'000;

CheatEngine::CheatEngine(Core::System& system_) : system{system_} {}

CheatEngine::~CheatEngine() {
    if (system.IsPoweredOn()) {
        system.CoreTiming().UnscheduleEvent(event, 0);
    }
}

void CheatEngine::Connect(u32 process_id) {
    this->process_id = process_id;
    event = system.CoreTiming().RegisterEvent(
        "CheatCore::run_event",
        [this](u64 thread_id, s64 cycle_late) { RunCallback(thread_id, cycle_late); });
    system.CoreTiming().ScheduleEvent(run_interval_ticks, event);
}

std::span<const std::shared_ptr<CheatBase>> CheatEngine::GetCheats() const {
    std::shared_lock lock{cheats_list_mutex};
    return cheats_list;
}

void CheatEngine::AddCheat(std::shared_ptr<CheatBase>&& cheat) {
}

void CheatEngine::RemoveCheat(std::size_t index) {
}

void CheatEngine::UpdateCheat(std::size_t index, std::shared_ptr<CheatBase>&& new_cheat) {
}

void CheatEngine::SaveCheatFile(u64 title_id) const {
}

void CheatEngine::LoadCheatFile(u64 title_id) {
}

void CheatEngine::RunCallback([[maybe_unused]] std::uintptr_t user_data, s64 cycles_late) {
    {}
}

} // namespace Cheats