#pragma once
#include <cstdint>
#include <vector>

#include "sim.h"

struct Color { float r, g, b; };

// 種類 ID ごとの色: 色相 30 度刻み、彩度・明度は全種類共通 (13 種類目以降は周回ごとに明度を下げる)
Color TypeColor(int t, float sat, float val);

class Renderer {
public:
    bool init();
    // fbX, fbY, fbW, fbH: プレビュー領域 (フレームバッファ座標・左下原点)
    // world 正方形は領域内に中央寄せでフィットさせる
    void draw(const Simulation& sim, int fbX, int fbY, int fbW, int fbH, float pointPx, bool showBonds,
              float sat, float val);

private:
    struct Vtx { float x, y; uint8_t r, g, b, a; };
    unsigned prog_ = 0, vao_ = 0, vbo_ = 0;
    int uPointSize_ = -1, uRound_ = -1, uInvWorld_ = -1;
    std::vector<Vtx> pts_, lines_;
};
