#pragma once
#include <string>
#include <vector>

class Simulation;

// 弱い力テーブルのプリセット (exe と同じフォルダの presets/ に JSON で保存)
std::vector<std::string> ListPresets();  // UTF-8 の名前 (拡張子なし)
bool SavePreset(const std::string& nameUtf8, const Simulation& sim, std::string& msg);
// 読込時、種類数はプリセットに合わせて増減する
bool LoadPreset(const std::string& nameUtf8, Simulation& sim, std::string& msg);
std::string PresetDirUtf8();
