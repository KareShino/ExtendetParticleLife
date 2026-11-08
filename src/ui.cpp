#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "imgui.h"
#include "preset.h"
#include "render.h"

namespace {

ImVec4 TypeColV4(const AppState& st, int t) {
    Color c = TypeColor(t, st.sat, st.val);
    return ImVec4(c.r, c.g, c.b, 1.0f);
}

void Swatch(const AppState& st, int t, float size = 0.0f) {
    if (size <= 0.0f) size = ImGui::GetFrameHeight();
    char id[16];
    std::snprintf(id, sizeof(id), "##sw%d", t);
    ImGui::ColorButton(id, TypeColV4(st, t),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoBorder,
                       ImVec2(size, size));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("種類 %d", t);
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

void RefreshPresets(AppState& st) {
    st.presets = ListPresets();
    if (st.presetSel >= (int)st.presets.size()) st.presetSel = -1;
}

// ---- スライダー + 自由入力欄 ----
// スライダーは目安の範囲、右の入力欄には範囲外の値も直接書ける (hmin..hmax の安全範囲だけで制限)。
// 戻り値: 値が変わった。committed: 編集の確定 (スライダーを離した / 入力欄でEnter・フォーカス外し)
bool FreeFloat(const char* label, float* v, float smin, float smax, float hmin, float hmax, const char* fmt,
               ImGuiSliderFlags sflags = 0, bool* committed = nullptr, float width = 0.0f) {
    ImGui::PushID(label);
    const float fs = ImGui::GetFontSize();
    const float total = width > 0.0f ? width : ImGui::CalcItemWidth();
    const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
    const float iw = std::max(fs * 4.5f, total * 0.32f);
    ImGui::SetNextItemWidth(std::max(fs * 2.0f, total - iw - sp));
    bool ch = ImGui::SliderFloat("##s", v, smin, smax, fmt, sflags | ImGuiSliderFlags_NoInput);
    bool com = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine(0, sp);
    ImGui::SetNextItemWidth(iw);
    ch |= ImGui::InputFloat("##i", v, 0.0f, 0.0f, "%.6g");
    com |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine(0, sp);
    ImGui::TextUnformatted(label);
    if (!(*v >= hmin)) *v = hmin;  // NaN も含めて安全範囲に収める
    if (*v > hmax) *v = hmax;
    if (committed) *committed = com;
    ImGui::PopID();
    return ch;
}

bool FreeInt(const char* label, int* v, int smin, int smax, int hmin, int hmax, bool* committed = nullptr,
             float width = 0.0f, ImGuiSliderFlags sflags = 0) {
    ImGui::PushID(label);
    const float fs = ImGui::GetFontSize();
    const float total = width > 0.0f ? width : ImGui::CalcItemWidth();
    const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
    const float iw = std::max(fs * 4.5f, total * 0.32f);
    ImGui::SetNextItemWidth(std::max(fs * 2.0f, total - iw - sp));
    bool ch = ImGui::SliderInt("##s", v, smin, smax, "%d", sflags | ImGuiSliderFlags_NoInput);
    bool com = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine(0, sp);
    ImGui::SetNextItemWidth(iw);
    ch |= ImGui::InputInt("##i", v, 0, 0);
    com |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine(0, sp);
    ImGui::TextUnformatted(label);
    *v = std::clamp(*v, hmin, hmax);
    if (committed) *committed = com;
    ImGui::PopID();
    return ch;
}

// 種類数が多いときは列幅を確保して横スクロールにする
bool BeginTypeTable(const char* id, int n, float minColW, ImGuiTableFlags extra) {
    const ImGuiStyle& s = ImGui::GetStyle();
    const float headW = ImGui::GetFrameHeight() + s.CellPadding.x * 2 + 4;
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool scroll = headW + n * (minColW + s.CellPadding.x * 2 + 1) > avail;
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_NoPadOuterX | extra;
    ImVec2 size(0, 0);
    if (scroll) {
        flags |= ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit;
        const float rowH = ImGui::GetFrameHeight() + s.CellPadding.y * 2 + 1;
        size.y = rowH * (n + 1) + s.ScrollbarSize + 4;
    } else {
        flags |= ImGuiTableFlags_SizingStretchSame;
    }
    if (!ImGui::BeginTable(id, n + 1, flags, size)) return false;
    ImGui::TableSetupScrollFreeze(1, 0);
    ImGui::TableSetupColumn("##h", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 4);
    for (int b = 0; b < n; ++b)
        ImGui::TableSetupColumn(nullptr, scroll ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch,
                                scroll ? minColW : 0.0f);
    return true;
}

void ApplyNumTypes(Simulation& sim, AppState& st, int n) {
    const int before = sim.numTypes;
    sim.setNumTypes(n);
    for (int t = before; t < sim.numTypes; ++t) st.ratios[t] = 1.0f;
    st.brushType = std::min(st.brushType, sim.numTypes - 1);
    st.numTypesEdit = sim.numTypes;
}

void ParamsSection(Simulation& sim, AppState& st) {
    if (!ImGui::CollapsingHeader("共通パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& p = sim.p;
    if (ImGui::SmallButton("ランダム##params")) sim.randomizeParams();
    ImGui::SameLine();
    if (ImGui::SmallButton("初期値に戻す")) p = SimParams{};
    HelpMarker("ランダム: 各値を目安範囲内で生成 (結合距離 ≥ ボンド長、切断距離 > 結合距離 になるよう調整)。\n"
               "世界の広さ・再生速度は変えません");
    const float maxR = sim.world * 0.45f;  // ループ空間で最短像を保てる上限
    const ImGuiSliderFlags LOG = ImGuiSliderFlags_Logarithmic;
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
    FreeFloat("最大距離", &p.rMax, 0.01f, std::min(0.3f, maxR), 0.0005f, maxR, "%.4f", LOG);
    FreeFloat("β", &p.beta, 0.05f, 0.9f, 0.01f, 0.99f, "%.3f");
    HelpMarker("r/最大距離 が β 未満では全種類共通の斥力 (0.01〜0.99)");
    FreeFloat("摩擦(半減時間)", &p.frictionHalfLife, 0.005f, 0.5f, 0.0001f, 1e6f, "%.4f", LOG);
    HelpMarker("速度が半分になるまでの時間。小さいほど摩擦が強い");
    FreeFloat("力の倍率", &p.forceFactor, 0.0f, 50.0f, -1e5f, 1e5f, "%.2f");
    FreeFloat("ボンド長", &p.bondLength, 0.001f, 0.08f, 0.0f, 1e3f, "%.4f", LOG);
    FreeFloat("バネ強度", &p.springK, 0.0f, 2000.0f, 0.0f, 1e7f, "%.1f");
    HelpMarker("大きすぎると数値的に不安定になります (目安: 2000 程度まで)");
    FreeFloat("結合距離", &p.bondDist, 0.001f, 0.1f, 0.0f, maxR, "%.4f", LOG);
    FreeFloat("切断距離", &p.breakDist, 0.002f, 0.2f, 0.0f, 1e3f, "%.4f", LOG);
    FreeFloat("結合確率", &p.bondProb, 0.0f, 1.0f, 0.0f, 1.0f, "%.5f", LOG);
    FreeFloat("切断確率", &p.breakProb, 0.0f, 0.1f, 0.0f, 1.0f, "%.6f", LOG);
    HelpMarker("結合確率・切断確率は 1 ステップあたり (0〜1)");
    FreeFloat("熱ゆらぎ強度", &p.thermal, 0.0f, 2.0f, 0.0f, 1e4f, "%.3f");
    ImGui::PopItemWidth();
    ImGui::TextDisabled("右の入力欄にはスライダー範囲外の値も直接入力できます");
    p.breakDist = std::max(p.breakDist, p.bondDist);
}

void ViewSection(AppState& st) {
    if (!ImGui::CollapsingHeader("表示")) return;
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
    FreeFloat("彩度", &st.sat, 0.0f, 1.0f, 0.0f, 1.0f, "%.2f");
    FreeFloat("明度", &st.val, 0.1f, 1.0f, 0.0f, 1.0f, "%.2f");
    FreeFloat("粒子サイズ(px)", &st.pointPx, 1.0f, 16.0f, 0.5f, 256.0f, "%.1f");
    ImGui::Checkbox("結合を表示", &st.showBonds);
    ImGui::PopItemWidth();
}

void WeakTable(Simulation& sim, AppState& st) {
    if (!ImGui::CollapsingHeader("弱い力テーブル", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (ImGui::Button("ランダム生成")) sim.randomizeWeak();
    ImGui::SameLine();
    if (ImGui::Button("全て0")) {
        for (int a = 0; a < sim.numTypes; ++a)
            for (int b = 0; b < sim.numTypes; ++b) sim.weak[a][b] = 0.0f;
    }
    HelpMarker("行 = 力を受ける種類、列 = 相手の種類。正 = 引力、負 = 斥力。\n"
               "セルを左右にドラッグ (-1〜1)、またはダブルクリック/Ctrl+クリックで数値を直接入力 (-10〜10)");

    // プリセット
    const float btnW = ImGui::GetFontSize() * 8.0f;
    ImGui::SetNextItemWidth(-btnW);
    ImGui::InputTextWithHint("##pname", "プリセット名", st.presetName, sizeof(st.presetName));
    ImGui::SameLine();
    if (ImGui::Button("名前を付けて保存")) {
        SavePreset(st.presetName, sim, st.msg);
        RefreshPresets(st);
    }
    ImGui::SetNextItemWidth(-btnW);
    const char* preview = st.presetSel >= 0 ? st.presets[st.presetSel].c_str() : "(プリセットを選択)";
    if (ImGui::BeginCombo("##plist", preview)) {
        RefreshPresets(st);
        for (int i = 0; i < (int)st.presets.size(); ++i)
            if (ImGui::Selectable(st.presets[i].c_str(), i == st.presetSel)) st.presetSel = i;
        if (st.presets.empty()) ImGui::TextDisabled("保存済みプリセットなし");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(st.presetSel < 0);
    if (ImGui::Button("読込") && st.presetSel >= 0) {
        const int before = sim.numTypes;
        if (LoadPreset(st.presets[st.presetSel], sim, st.msg)) {
            std::snprintf(st.presetName, sizeof(st.presetName), "%s", st.presets[st.presetSel].c_str());
            for (int t = before; t < sim.numTypes; ++t) st.ratios[t] = 1.0f;
            st.brushType = std::min(st.brushType, sim.numTypes - 1);
            st.numTypesEdit = sim.numTypes;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("種類数はプリセットに合わせて増減します\n(減る場合は末尾の種類と粒子・結合を削除)\n保存先: %s",
                          PresetDirUtf8().c_str());

    const int n = sim.numTypes;
    if (BeginTypeTable("weak", n, ImGui::GetFontSize() * 2.6f, 0)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        for (int b = 0; b < n; ++b) {
            ImGui::TableNextColumn();
            Swatch(st, b);
        }
        for (int a = 0; a < n; ++a) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            Swatch(st, a);
            for (int b = 0; b < n; ++b) {
                ImGui::TableNextColumn();
                float v = std::clamp(sim.weak[a][b], -1.0f, 1.0f);
                ImU32 bg = v >= 0 ? ImGui::GetColorU32(ImVec4(0.1f, 0.55f, 0.2f, 0.15f + 0.6f * v))
                                  : ImGui::GetColorU32(ImVec4(0.65f, 0.12f, 0.12f, 0.15f - 0.6f * v));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, bg);
                ImGui::PushID(a * kMaxTypes + b);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::DragFloat("##w", &sim.weak[a][b], 0.01f, -1.0f, 1.0f, "%.2f");
                sim.weak[a][b] = std::clamp(sim.weak[a][b], -10.0f, 10.0f);
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void BondTable(Simulation& sim, AppState& st) {
    if (!ImGui::CollapsingHeader("結合可否テーブル", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const int n = sim.numTypes;
    if (ImGui::SmallButton("ランダム##bond")) sim.randomizeCanBond();
    ImGui::SameLine();
    if (ImGui::SmallButton("全て可")) for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) sim.canBond[a][b] = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("全て不可")) for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) sim.canBond[a][b] = false;
    HelpMarker("対称。チェックした種類の組み合わせが結合しうる\nランダム: 各組み合わせを 50% で可にする");
    if (BeginTypeTable("bond", n, ImGui::GetFrameHeight(), 0)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        for (int b = 0; b < n; ++b) {
            ImGui::TableNextColumn();
            Swatch(st, b);
        }
        for (int a = 0; a < n; ++a) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            Swatch(st, a);
            for (int b = 0; b < n; ++b) {
                ImGui::TableNextColumn();
                ImGui::PushID(a * kMaxTypes + b);
                bool v = sim.canBond[a][b];
                if (ImGui::Checkbox("##b", &v)) sim.setCanBond(a, b, v);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void ValenceTable(Simulation& sim, AppState& st) {
    if (!ImGui::CollapsingHeader("価数テーブル", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (ImGui::SmallButton("ランダム##val")) sim.randomizeValence();
    HelpMarker("価数は粒子あたりの結合スロット数 (0〜4)\nランダム: 種類ごとに 0〜4 の一様乱数");
    int removeT = -1;
    if (ImGui::BeginTable("val", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("色");
        ImGui::TableSetupColumn("種類");
        ImGui::TableSetupColumn("価数", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("操作");
        ImGui::TableHeadersRow();
        for (int t = 0; t < sim.numTypes; ++t) {
            ImGui::PushID(t);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            Swatch(st, t);
            ImGui::TableNextColumn();
            ImGui::Text("種類 %d", t);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##v", &sim.valence[t], 0, kMaxBonds, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(sim.numTypes <= 1);
            if (ImGui::SmallButton("削除")) removeT = t;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeT >= 0) {
        sim.removeType(removeT);
        for (int t = removeT; t < kMaxTypes - 1; ++t) st.ratios[t] = st.ratios[t + 1];
        st.ratios[kMaxTypes - 1] = 1.0f;
        st.brushType = std::min(st.brushType, sim.numTypes - 1);
        st.numTypesEdit = sim.numTypes;
    }
}

void RightPanel(Simulation& sim, AppState& st, float x, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("右パネル", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Text("粒子 %d / %d   結合 %d   種類 %d", sim.count(), st.cap, sim.totalBonds(), sim.numTypes);
    ImGui::Text("%.0f FPS   計算 %.2f ms/フレーム", ImGui::GetIO().Framerate, st.simMs);
    if (ImGui::Button(st.paused ? "再開" : "一時停止")) st.paused = !st.paused;
    ImGui::SameLine();
    if (ImGui::Button("1x")) st.speed = 1.0f;
    ImGui::SameLine();
    FreeFloat("再生速度(倍)", &st.speed, 0.1f, 10.0f, 0.01f, (float)AppState::kMaxStepsPerFrame, "%.2f",
              ImGuiSliderFlags_Logarithmic, nullptr, ImGui::GetFontSize() * 14.0f);
    HelpMarker("1倍 = 1フレームに1ステップ。1未満は数フレームに1ステップ。\n入力欄では 0.01〜200 倍まで指定可");
    if (ImGui::Button("すべてランダム")) {
        sim.randomizeWeak();
        sim.randomizeCanBond();
        sim.randomizeValence();
        sim.randomizeParams();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("弱い力・結合可否・価数・共通パラメータをまとめてランダム化 (粒子はそのまま)");
    if (!st.msg.empty()) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", st.msg.c_str());
    ImGui::Separator();
    WeakTable(sim, st);
    BondTable(sim, st);
    ValenceTable(sim, st);
    ParamsSection(sim, st);
    ViewSection(st);
    ImGui::End();
}

void BottomPanel(Simulation& sim, AppState& st, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(0, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("下パネル", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float fs = ImGui::GetFontSize();
    const float gap = fs * 1.5f;

    FreeInt("粒子数の上限", &st.cap, 0, 100000, 0, AppState::kMaxParticles, nullptr, fs * 14.0f,
            ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine(0, gap);
    bool typesCommitted = false;
    FreeInt("種類数", &st.numTypesEdit, 1, kMaxTypes, 1, kMaxTypes, &typesCommitted, fs * 10.0f);
    if (typesCommitted && st.numTypesEdit != sim.numTypes) ApplyNumTypes(sim, st, st.numTypesEdit);
    if (!ImGui::IsAnyItemActive() && st.numTypesEdit != sim.numTypes) st.numTypesEdit = sim.numTypes;
    HelpMarker("確定 (スライダーを離す / Enter) で即反映。\n減らすと末尾の種類とその粒子・結合を削除、増やした種類は力0・結合不可・価数0で追加");
    ImGui::SameLine(0, gap);
    bool worldCommitted = false;
    FreeFloat("世界の広さ", &st.worldEdit, 0.5f, 5.0f, 0.05f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic,
              &worldCommitted, fs * 12.0f);
    if (worldCommitted && st.worldEdit != sim.world) {
        sim.world = st.worldEdit;
        sim.p.rMax = std::min(sim.p.rMax, sim.world * 0.45f);
        sim.p.bondDist = std::min(sim.p.bondDist, sim.world * 0.45f);
        sim.randomFill(st.cap, st.ratios);  // 広さ変更時は全消去して再生成
    }
    HelpMarker("ループ空間の一辺の長さ。最大距離・ボンド長などはそのまま。\n確定すると全消去し、ランダム配置の比率で上限数まで再生成します");

    ImGui::RadioButton("手動配置", &st.mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ランダム配置", &st.mode, 1);
    ImGui::SameLine(0, gap);
    if (ImGui::Button("全消去")) sim.clearParticles();
    ImGui::Separator();

    if (st.mode == 0) {
        ImGui::TextUnformatted("配置する種類:");
        for (int t = 0; t < sim.numTypes; ++t) {
            ImGui::SameLine();
            ImGui::PushID(t);
            bool sel = st.brushType == t;
            ImGui::PushStyleColor(ImGuiCol_Border, sel ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, sel ? 3.0f : 0.0f);
            if (ImGui::ColorButton("##t", TypeColV4(st, t), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(ImGui::GetFrameHeight() * 1.2f, ImGui::GetFrameHeight())))
                st.brushType = t;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("種類 %d", t);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        FreeFloat("ブラシ半径", &st.brushRadius, 0.002f, 0.15f, 0.0001f, 1000.0f, "%.4f", ImGuiSliderFlags_Logarithmic,
                  nullptr, fs * 13.0f);
        ImGui::SameLine(0, gap);
        FreeInt("ドラッグ時の配置数/フレーム", &st.brushRate, 1, 50, 1, 100000, nullptr, fs * 11.0f);
        ImGui::TextDisabled("左クリック: 1個配置 / 左ドラッグ: ブラシ範囲に連続配置 / 右ドラッグ: 消去");
    } else {
        ImGui::TextUnformatted("種類ごとの比率:");
        ImGui::SameLine();
        if (ImGui::SmallButton("全て1")) for (float& r : st.ratios) r = 1.0f;
        for (int t = 0; t < sim.numTypes; ++t) {
            if (t % 8 != 0) ImGui::SameLine(0, fs * 0.8f);
            ImGui::PushID(t);
            Swatch(st, t);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(fs * 3.6f);
            ImGui::DragFloat("##r", &st.ratios[t], 0.05f, 0.0f, 0.0f, "%.2f");
            st.ratios[t] = std::clamp(st.ratios[t], 0.0f, 1e6f);
            ImGui::PopID();
        }
        if (ImGui::Button("一括生成 (全消去して上限数まで)")) sim.randomFill(st.cap, st.ratios);
    }
    ImGui::End();
}

}  // namespace

void DrawUI(Simulation& sim, AppState& st) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    const float rightW = std::clamp(ds.x * 0.36f, fs * 26.0f, fs * 40.0f);
    const float bottomH = fs * 10.5f;
    st.pvX = 0;
    st.pvY = 0;
    st.pvW = std::max(0.0f, ds.x - rightW);
    st.pvH = std::max(0.0f, ds.y - bottomH);
    RightPanel(sim, st, st.pvW, rightW, ds.y);
    BottomPanel(sim, st, st.pvH, st.pvW, bottomH);
}

void HandlePreviewInput(Simulation& sim, AppState& st) {
    ImGuiIO& io = ImGui::GetIO();
    const float side = std::min(st.pvW, st.pvH);
    if (side <= 0 || st.mode != 0) return;
    const float ox = st.pvX + (st.pvW - side) * 0.5f, oy = st.pvY + (st.pvH - side) * 0.5f;
    const ImVec2 mp = io.MousePos;
    const bool inside = mp.x >= ox && mp.x < ox + side && mp.y >= oy && mp.y < oy + side;
    if (!inside || io.WantCaptureMouse) return;

    const float wx = (mp.x - ox) / side * sim.world, wy = (mp.y - oy) / side * sim.world;
    // ブラシ表示
    ImGui::GetForegroundDrawList()->AddCircle(mp, st.brushRadius / sim.world * side, IM_COL32(255, 255, 255, 120), 48);

    static std::mt19937 rng{12345};
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        sim.addParticle(wx, wy, st.brushType, st.cap);
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        for (int k = 0; k < st.brushRate; ++k) {
            float a = U(rng) * 6.2831853f, r = std::sqrt(U(rng)) * st.brushRadius;
            if (!sim.addParticle(wx + std::cos(a) * r, wy + std::sin(a) * r, st.brushType, st.cap)) break;
        }
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) sim.removeInRadius(wx, wy, st.brushRadius);
}
