#include "preset.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "sim.h"

namespace fs = std::filesystem;

static fs::path PresetDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD len = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    fs::path exe(std::wstring(buf, len));
    return exe.parent_path() / L"presets";
}

static std::string ToUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
static fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string PresetDirUtf8() { return ToUtf8(PresetDir()); }

static bool ValidName(const std::string& n) {
    if (n.empty() || n.size() > 120) return false;
    for (unsigned char c : n)
        if (c < 32 || std::string("<>:\"/\\|?*").find((char)c) != std::string::npos) return false;
    return n != "." && n != "..";
}

std::vector<std::string> ListPresets() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(PresetDir(), ec)) {
        if (e.is_regular_file() && e.path().extension() == L".json") out.push_back(ToUtf8(e.path().stem()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool SavePreset(const std::string& name, const Simulation& sim, std::string& msg) {
    if (!ValidName(name)) {
        msg = "名前が不正です (空、または \\ / : * ? \" < > | を含む)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(PresetDir(), ec);
    nlohmann::json j;
    j["name"] = name;
    j["types"] = sim.numTypes;
    auto& m = j["weak"] = nlohmann::json::array();
    for (int a = 0; a < sim.numTypes; ++a) {
        auto row = nlohmann::json::array();
        for (int b = 0; b < sim.numTypes; ++b) row.push_back(sim.weak[a][b]);
        m.push_back(row);
    }
    std::ofstream f(PresetDir() / FromUtf8(name + ".json"), std::ios::binary);
    if (!f) {
        msg = "保存に失敗しました";
        return false;
    }
    f << j.dump(2);
    msg = "保存しました: " + name;
    return true;
}

bool LoadPreset(const std::string& name, Simulation& sim, std::string& msg) {
    std::ifstream f(PresetDir() / FromUtf8(name + ".json"), std::ios::binary);
    if (!f) {
        msg = "読込に失敗しました: " + name;
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        const auto& m = j.at("weak");
        int n = (int)m.size();
        if (n < 1 || n > kMaxTypes) throw std::runtime_error("種類数が範囲外");
        for (auto& row : m)
            if ((int)row.size() != n) throw std::runtime_error("行列が正方でない");
        sim.setNumTypes(n);
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) sim.weak[a][b] = std::clamp(m[a][b].get<float>(), -1.0f, 1.0f);
        msg = "読み込みました: " + name + " (" + std::to_string(n) + "種類)";
        return true;
    } catch (const std::exception& e) {
        msg = std::string("読込エラー: ") + e.what();
        return false;
    }
}
