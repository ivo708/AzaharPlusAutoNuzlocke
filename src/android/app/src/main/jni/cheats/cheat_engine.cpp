// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <utility>
#include <vector>

#include <jni.h>

#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
#include "core/core.h"
#include "jni/cheats/cheat.h"
#include "jni/id_cache.h"

extern "C" {

static Cheats::CheatEngine& GetEngine() {
    Core::System& system{Core::System::GetInstance()};
    return system.CheatEngine();
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_loadCheatFile(
    JNIEnv* env, jclass, jlong title_id) {
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_saveCheatFile(
    JNIEnv* env, jclass, jlong title_id) {
}

JNIEXPORT jobjectArray JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_getCheats(JNIEnv* env, jclass) {}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_addCheat(
    JNIEnv* env, jclass, jobject j_cheat) {

}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_removeCheat(
    JNIEnv* env, jclass, jint index) {
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_updateCheat(
    JNIEnv* env, jclass, jint index, jobject j_new_cheat) {
}
}
