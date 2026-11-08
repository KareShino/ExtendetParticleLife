#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

class ThreadPool;

constexpr int kMaxTypes = 12;
constexpr int kMaxBonds = 4;  // 粒子あたりの結合スロット数 (価数の上限)

struct SimParams {
    float rMax = 0.08f;              // 最大距離 (ワールドは 1x1 のループ空間)
    float beta = 0.3f;               // 近距離斥力の範囲 (rMax に対する比)
    float frictionHalfLife = 0.04f;  // 摩擦: 速度が半分になる時間
    float forceFactor = 10.0f;       // 弱い力の倍率
    float bondLength = 0.015f;       // ボンドの自然長
    float springK = 300.0f;          // バネ強度
    float bondDist = 0.02f;          // この距離以内で結合を形成しうる
    float breakDist = 0.045f;        // この距離を超えると切断
    float bondProb = 0.05f;          // 1ステップあたりの結合確率
    float breakProb = 0.0005f;       // 1ステップあたりの切断確率
    float thermal = 0.1f;            // 熱ゆらぎ強度
    float dt = 0.02f;
};

class Simulation {
public:
    explicit Simulation(ThreadPool& pool);

    // ---- 種類 ----
    int numTypes = 0;
    float weak[kMaxTypes][kMaxTypes] = {};    // weak[i][j]: 種類 i が j から受ける力 (非対称可)
    bool canBond[kMaxTypes][kMaxTypes] = {};  // 対称
    int valence[kMaxTypes] = {};              // 0..kMaxBonds
    SimParams p;

    // ---- 粒子 (配列ベース) ----
    std::vector<float> x, y, vx, vy;
    std::vector<uint8_t> type;
    std::vector<std::array<int32_t, kMaxBonds>> bonds;  // -1 = 空き
    std::vector<uint8_t> bondCount;

    int count() const { return (int)x.size(); }
    int totalBonds() const;
    bool addParticle(float px, float py, int t, int cap);
    void clearParticles();
    void removeParticles(const std::vector<uint8_t>& mask);  // mask[i]!=0 を削除
    void removeInRadius(float cx, float cy, float r);
    void randomFill(int total, const float* ratios);  // 全消去して比率で total 個生成

    bool addType();
    void removeType(int t);
    void setNumTypes(int n);
    void randomizeWeak();
    void setCanBond(int a, int b, bool v) { canBond[a][b] = canBond[b][a] = v; }

    void step();
    uint64_t stepCount() const { return stepCounter_; }

private:
    void buildGrid();
    void formBonds();
    void breakBonds();
    void unlink(int i, int slot);
    bool isBonded(int i, int j) const {
        const auto& b = bonds[i];
        return b[0] == j || b[1] == j || b[2] == j || b[3] == j;
    }

    ThreadPool& pool_;
    std::mt19937 rng_;
    uint64_t stepCounter_ = 0;

    // グリッド
    int gridN_ = 1;
    std::vector<int> cellStart_, cellOf_, sorted_;
    std::vector<float> sx_, sy_;
    std::vector<uint8_t> st_;

    // 作業領域
    std::vector<float> ax_, ay_;
    std::vector<std::vector<std::pair<int, int>>> candidates_;
};
