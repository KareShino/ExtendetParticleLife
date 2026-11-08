#pragma once
#include <string>
#include <vector>

#include "sim.h"

struct AppState {
    // 配置
    int cap = 4000;           // 粒子数の上限
    int mode = 1;             // 0: 手動配置, 1: ランダム配置
    int brushType = 0;
    float brushRadius = 0.02f;
    int brushRate = 4;        // ドラッグ中 1 フレームあたりの配置数
    float ratios[kMaxTypes];  // ランダム配置の種類ごとの比率

    // 実行
    bool paused = false;
    float speed = 1.0f;       // 再生速度 (1 フレームあたりの平均ステップ数)
    float stepAcc = 0.0f;     // 端数ステップの蓄積
    float worldEdit = 1.0f;   // 世界の広さ (スライダー編集中の値)
    double simMs = 0.0;

    // 表示
    float sat = 0.75f, val = 0.95f;
    float pointPx = 4.0f;
    bool showBonds = true;

    // プリセット
    char presetName[128] = "";
    std::vector<std::string> presets;
    int presetSel = -1;
    std::string msg;

    // プレビュー領域 (ウィンドウ座標, 左上原点)
    float pvX = 0, pvY = 0, pvW = 0, pvH = 0;

    AppState() {
        for (float& r : ratios) r = 1.0f;
    }
};

void DrawUI(Simulation& sim, AppState& st);
void HandlePreviewInput(Simulation& sim, AppState& st);
