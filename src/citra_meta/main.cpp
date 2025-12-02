#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <locale>
#include <codecvt>

#ifdef ENABLE_QT
#include "citra_qt/citra_qt.h"
#endif
#ifdef ENABLE_ROOM
#include "citra_room/citra_room.h"
#endif
#ifdef ENABLE_SDL2_FRONTEND
#include "citra_sdl/citra_sdl.h"
#endif

#ifdef _WIN32
#include <chrono>
#include <iostream>
#include <thread>
#include <windows.h>
#endif

#if CITRA_HAS_SSE42
#include <intrin.h>
static bool CpuSupportsSSE42() {
    int cpu_info[4];
    __cpuid(cpu_info, 1);
    uint32_t ecx = static_cast<uint32_t>(cpu_info[2]);
    return (ecx & (1 << 20)) != 0;
}

static bool CheckAndReportSSE42() {
    if (!CpuSupportsSSE42()) {
        const std::string error_msg =
            "This application requires a CPU with SSE4.2 support or higher.\n";
#ifdef _WIN32
        MessageBoxA(nullptr, error_msg.c_str(), "Incompatible CPU", MB_OK | MB_ICONERROR);
#endif
        std::cerr << "Error: " << error_msg << std::endl;
        return false;
    }
    return true;
}
#endif


int main(int argc, char* argv[]) {



#if CITRA_HAS_SSE42
    if (!CheckAndReportSSE42())
        return 1;
#endif


#if ENABLE_ROOM
    bool launch_room = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--room") == 0)
            launch_room = true;
    }
    if (launch_room) {
        LaunchRoom(argc, argv, true);
        return 0;
    }
#endif

#if ENABLE_QT
    bool no_gui = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-gui") == 0 || strcmp(argv[i], "-n") == 0)
            no_gui = true;
    }
    if (!no_gui) {
        LaunchQtFrontend(argc, argv);
        return 0;
    }
#endif

#if ENABLE_SDL2_FRONTEND
    LaunchSdlFrontend(argc, argv);
#else
    std::cout << "Cannot use SDL frontend as it was disabled at compile time. Exiting."
              << std::endl;
    return -1;
#endif

    return 0;
}
