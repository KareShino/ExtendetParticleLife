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

void ParamsSection(Simulation& sim, AppState& st) {
    if (!ImGui::CollapsingHeader("共通パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& p = sim.p;
    ImGui::PushItemWidth(-130.0f);
    ImGui::SliderFloat("最大距離", &p.rMax, 0.01f, 0.3f, "%.3f");
    ImGui::SliderFloat("β", &p.beta, 0.05f, 0.9f, "%.2f");
    HelpMarker("r/最大距離 が β 未満では全種類共通の斥力");
    ImGui::SliderFloat("摩擦(半減時間)", &p.frictionHalfLife, 0.005f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
    HelpMarker("速度が半分になるまでの時間。小さいほど摩擦が強い");
    ImGui::SliderFloat("力の倍率", &p.forceFactor, 0.0f, 50.0f, "%.1f");
    ImGui::SliderFloat("ボンド長", &p.bondLength, 0.001f, 0.08f, "%.4f");
    ImGui::SliderFloat("バネ強度", &p.springK, 0.0f, 2000.0f, "%.0f");
    ImGui::SliderFloat("結合距離", &p.bondDist, 0.001f, 0.1f, "%.4f");
    ImGui::SliderFloat("切断距離", &p.breakDist, 0.002f, 0.2f, "%.4f");
    ImGui::SliderFloat("結合確率", &p.bondProb, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("切断確率", &p.breakProb, 0.0f, 0.1f, "%.5f", ImGuiSliderFlags_Logarithmic);
    HelpMarker("結合確率・切断確率は 1 ステップあたり");
    ImGui::SliderFloat("熱ゆらぎ強度", &p.thermal, 0.0f, 2.0f, "%.3f");
    ImGui::SliderInt("ステップ/フレーム", &st.stepsPerFrame, 1, 10);
    ImGui::PopItemWidth();
    p.breakDist = std::max(p.breakDist, p.bondDist);
}

void ViewSection(AppState& st) {
    if (!ImGui::CollapsingHeader("表示")) return;
    ImGui::PushItemWidth(-130.0f);
    ImGui::SliderFloat("彩度", &st.sat, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("明度", &st.val, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("粒子サイズ(px)", &st.pointPx, 1.0f, 16.0f, "%.1f");
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
    HelpMarker("行 = 力を受ける種類、列 = 相手の種類。正 = 引力、負 = 斥力。\nセルをドラッグ、またはダブルクリック/Ctrl+クリックで数値入力");

    // プリセット
    ImGui::SetNextItemWidth(-170.0f);
    ImGui::InputTextWithHint("##pname", "プリセット名", st.presetName, sizeof(st.presetName));
    ImGui::SameLine();
    if (ImGui::Button("名前を付けて保存")) {
        SavePreset(st.presetName, sim, st.msg);
        RefreshPresets(st);
    }
    ImGui::SetNextItemWidth(-170.0f);
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
        if (LoadPreset(st.presets[st.presetSel], sim, st.msg))
            std::snprintf(st.presetName, sizeof(st.presetName), "%s", st.presets[st.presetSel].c_str());
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("種類数はプリセットに合わせて増減します\n(減る場合は末尾の種類と粒子・結合を削除)\n保存先: %s",
                          PresetDirUtf8().c_str());

    const int n = sim.numTypes;
    if (ImGui::BeginTable("weak", n + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("##h", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 4);
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
                float v = sim.weak[a][b];
                ImU32 bg = v >= 0 ? ImGui::GetColorU32(ImVec4(0.1f, 0.55f, 0.2f, 0.15f + 0.6f * v))
                                  : ImGui::GetColorU32(ImVec4(0.65f, 0.12f, 0.12f, 0.15f - 0.6f * v));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, bg);
                ImGui::PushID(a * kMaxTypes + b);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::DragFloat("##w", &sim.weak[a][b], 0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void BondTable(Simulation& sim, AppState& st) {
    if (!ImGui::CollapsingHeader("結合可否テーブル", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::SameLine();
    const int n = sim.numTypes;
    HelpMarker("対称。チェックした種類の組み合わせが結合しうる");
    if (ImGui::SmallButton("全て可")) for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) sim.canBond[a][b] = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("全て不可")) for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) sim.canBond[a][b] = false;
    if (ImGui::BeginTable("bond", n + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
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
            ImGui::SliderInt("##v", &sim.valence[t], 0, kMaxBonds);
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
    ImGui::BeginDisabled(sim.numTypes >= kMaxTypes);
    if (ImGui::Button("種類を追加")) {
        sim.addType();
        st.ratios[sim.numTypes - 1] = 1.0f;
    }
    ImGui::EndDisabled();
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
    ImGui::SetNextItemWidth(260.0f);
    ImGui::SliderInt("粒子数の上限", &st.cap, 0, 30000, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine(0, 30);
    ImGui::RadioButton("手動配置", &st.mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ランダム配置", &st.mode, 1);
    ImGui::SameLine(0, 30);
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
                                   ImVec2(ImGui::GetFrameHeight() * 1.4f, ImGui::GetFrameHeight())))
                st.brushType = t;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("ブラシ半径", &st.brushRadius, 0.002f, 0.15f, "%.3f");
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("ドラッグ時の配置数/フレーム", &st.brushRate, 1, 50);
        ImGui::TextDisabled("左クリック: 1個配置 / 左ドラッグ: ブラシ範囲に連続配置 / 右ドラッグ: 消去");
    } else {
        ImGui::TextUnformatted("種類ごとの比率:");
        for (int t = 0; t < sim.numTypes; ++t) {
            if (t % 6 != 0) ImGui::SameLine(0, 16);
            ImGui::PushID(t);
            Swatch(st, t);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragFloat("##r", &st.ratios[t], 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
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
    const float bottomH = fs * 9.5f;
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

    const float wx = (mp.x - ox) / side, wy = (mp.y - oy) / side;
    // ブラシ表示
    ImGui::GetForegroundDrawList()->AddCircle(mp, st.brushRadius * side, IM_COL32(255, 255, 255, 120), 48);

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
