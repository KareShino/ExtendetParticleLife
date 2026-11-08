#include "sim.h"

#include <algorithm>
#include <cmath>

#include "parallel.h"

namespace {

inline uint32_t hash32(uint32_t a) {
    a ^= a >> 16;
    a *= 0x7feb352du;
    a ^= a >> 15;
    a *= 0x846ca68bu;
    a ^= a >> 16;
    return a;
}
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    return hash32(a ^ hash32(b + 0x9e3779b9u + hash32(c)));
}
inline float u01(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }


}  // namespace

Simulation::Simulation(ThreadPool& pool) : pool_(pool), rng_(std::random_device{}()) {}

int Simulation::totalBonds() const {
    int s = 0;
    for (auto c : bondCount) s += c;
    return s / 2;
}

bool Simulation::addParticle(float px, float py, int t, int cap) {
    if (count() >= cap || t < 0 || t >= numTypes) return false;
    x.push_back(wp(px));
    y.push_back(wp(py));
    vx.push_back(0.0f);
    vy.push_back(0.0f);
    type.push_back((uint8_t)t);
    bonds.push_back({-1, -1, -1, -1});
    bondCount.push_back(0);
    return true;
}

void Simulation::clearParticles() {
    x.clear(); y.clear(); vx.clear(); vy.clear();
    type.clear(); bonds.clear(); bondCount.clear();
}

void Simulation::removeParticles(const std::vector<uint8_t>& mask) {
    const int n = count();
    std::vector<int> newIdx(n, -1);
    int w = 0;
    for (int i = 0; i < n; ++i)
        if (!mask[i]) newIdx[i] = w++;
    if (w == n) return;
    for (int i = 0; i < n; ++i) {
        int d = newIdx[i];
        if (d < 0) continue;
        x[d] = x[i]; y[d] = y[i]; vx[d] = vx[i]; vy[d] = vy[i];
        type[d] = type[i];
        auto b = bonds[i];
        uint8_t c = 0;
        for (auto& j : b) {
            if (j >= 0) j = newIdx[j];
            if (j >= 0) ++c;
        }
        bonds[d] = b;
        bondCount[d] = c;
    }
    x.resize(w); y.resize(w); vx.resize(w); vy.resize(w);
    type.resize(w); bonds.resize(w); bondCount.resize(w);
}

void Simulation::removeInRadius(float cx, float cy, float r) {
    const int n = count();
    std::vector<uint8_t> mask(n, 0);
    const float r2 = r * r;
    bool any = false;
    for (int i = 0; i < n; ++i) {
        float dx = wd(x[i] - cx), dy = wd(y[i] - cy);
        if (dx * dx + dy * dy < r2) mask[i] = 1, any = true;
    }
    if (any) removeParticles(mask);
}

void Simulation::randomFill(int total, const float* ratios) {
    clearParticles();
    if (numTypes <= 0 || total <= 0) return;
    float sum = 0.0f;
    for (int t = 0; t < numTypes; ++t) sum += std::max(0.0f, ratios[t]);
    std::vector<int> counts(numTypes, 0);
    int assigned = 0;
    for (int t = 0; t < numTypes; ++t) {
        float r = sum > 0.0f ? std::max(0.0f, ratios[t]) / sum : 1.0f / numTypes;
        counts[t] = (int)std::floor(r * total);
        assigned += counts[t];
    }
    // 端数は比率の大きい種類から順に配る
    for (int t = 0; assigned < total; t = (t + 1) % numTypes) {
        if (sum <= 0.0f || ratios[t] > 0.0f) { ++counts[t]; ++assigned; }
    }
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    x.reserve(total);
    for (int t = 0; t < numTypes; ++t)
        for (int k = 0; k < counts[t]; ++k) addParticle(U(rng_) * world, U(rng_) * world, t, total);
    // 種類ごとに並ばないようシャッフル (結合なしなので単純に入れ替え可)
    const int n = count();
    for (int i = n - 1; i > 0; --i) {
        int j = std::uniform_int_distribution<int>(0, i)(rng_);
        std::swap(x[i], x[j]); std::swap(y[i], y[j]); std::swap(type[i], type[j]);
    }
}

bool Simulation::addType() {
    if (numTypes >= kMaxTypes) return false;
    int t = numTypes++;
    for (int k = 0; k < numTypes; ++k) {
        weak[t][k] = weak[k][t] = 0.0f;
        canBond[t][k] = canBond[k][t] = false;
    }
    valence[t] = 0;
    return true;
}

void Simulation::removeType(int t) {
    if (t < 0 || t >= numTypes || numTypes <= 1) return;
    const int n = count();
    std::vector<uint8_t> mask(n);
    for (int i = 0; i < n; ++i) mask[i] = type[i] == t;
    removeParticles(mask);
    for (auto& ty : type)
        if (ty > t) --ty;
    for (int a = t; a < numTypes - 1; ++a) valence[a] = valence[a + 1];
    // 行と列を詰める
    for (int a = 0; a < numTypes; ++a) {
        int sa = a < t ? a : a + 1;
        if (sa >= numTypes) break;
        for (int b = 0; b < numTypes; ++b) {
            int sb = b < t ? b : b + 1;
            if (sb >= numTypes) break;
            weak[a][b] = weak[sa][sb];
            canBond[a][b] = canBond[sa][sb];
        }
    }
    --numTypes;
}

void Simulation::setNumTypes(int n) {
    n = std::clamp(n, 1, kMaxTypes);
    while (numTypes > n) removeType(numTypes - 1);
    while (numTypes < n) addType();
}

void Simulation::randomizeWeak() {
    std::uniform_real_distribution<float> U(-1.0f, 1.0f);
    for (int a = 0; a < numTypes; ++a)
        for (int b = 0; b < numTypes; ++b) weak[a][b] = std::round(U(rng_) * 100.0f) / 100.0f;
}

void Simulation::buildGrid() {
    const int n = count();
    float cell = std::min(std::max(p.rMax, p.bondDist), world * 0.45f);
    gridN_ = std::max(1, std::min(256, (int)(world / cell)));
    const int G = gridN_, cells = G * G;
    const float invCell = G / world;
    cellStart_.assign(cells + 1, 0);
    cellOf_.resize(n);
    for (int i = 0; i < n; ++i) {
        int cx = std::min(G - 1, (int)(x[i] * invCell));
        int cy = std::min(G - 1, (int)(y[i] * invCell));
        int c = cx + cy * G;
        cellOf_[i] = c;
        ++cellStart_[c + 1];
    }
    for (int c = 0; c < cells; ++c) cellStart_[c + 1] += cellStart_[c];
    sorted_.resize(n);
    sx_.resize(n); sy_.resize(n); st_.resize(n);
    std::vector<int> cursor(cellStart_.begin(), cellStart_.end() - 1);
    for (int i = 0; i < n; ++i) {
        int k = cursor[cellOf_[i]]++;
        sorted_[k] = i;
        sx_[k] = x[i]; sy_[k] = y[i]; st_[k] = type[i];
    }
}

void Simulation::step() {
    ++stepCounter_;
    const int n = count();
    if (n == 0) return;
    buildGrid();
    ax_.resize(n);
    ay_.resize(n);

    const int chunks = pool_.threadCount() * 4;
    if ((int)candidates_.size() < chunks) candidates_.resize(chunks);
    for (auto& c : candidates_) c.clear();

    const int G = gridN_;
    const float rMax = std::min(p.rMax, world * 0.45f), rMax2 = rMax * rMax, invR = 1.0f / rMax;
    const float beta = std::clamp(p.beta, 0.01f, 0.99f), invBeta = 1.0f / beta, inv1mb = 1.0f / (1.0f - beta);
    const float ff = rMax * p.forceFactor;
    const float bondD2 = p.bondDist * p.bondDist;
    const float k = p.springK, L = p.bondLength;
    const uint32_t bondThresh = (uint32_t)(std::clamp(p.bondProb, 0.0f, 1.0f) * 16777216.0f);
    const uint32_t seed = hash32((uint32_t)stepCounter_ * 2654435761u);

    pool_.parallelFor(n, chunks, [&](int chunk, int b, int e) {
        auto& cand = candidates_[chunk];
        int nb[9];
        for (int kk = b; kk < e; ++kk) {
            const int i = sorted_[kk];
            const float xi = sx_[kk], yi = sy_[kk];
            const int ti = st_[kk];
            const float* wrow = weak[ti];
            const bool iHasBond = bondCount[i] > 0;
            const bool iFree = bondCount[i] < valence[ti] && bondThresh > 0;
            const int c = cellOf_[i], cx = c % G, cy = c / G;
            int nn = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int cc = (cx + dx + G) % G + ((cy + dy + G) % G) * G;
                    bool dup = false;
                    for (int q = 0; q < nn; ++q) dup |= nb[q] == cc;
                    if (!dup) nb[nn++] = cc;
                }
            float fx = 0.0f, fy = 0.0f;
            for (int q = 0; q < nn; ++q) {
                const int end = cellStart_[nb[q] + 1];
                for (int m = cellStart_[nb[q]]; m < end; ++m) {
                    if (m == kk) continue;
                    float ddx = wd(sx_[m] - xi), ddy = wd(sy_[m] - yi);
                    float d2 = ddx * ddx + ddy * ddy;
                    if (d2 >= rMax2 || d2 < 1e-12f) continue;
                    const float d = std::sqrt(d2), r = d * invR;
                    const int tj = st_[m];
                    float f;
                    if (r < beta) {
                        f = r * invBeta - 1.0f;  // 共通の近距離斥力
                    } else {
                        f = wrow[tj] * (1.0f - std::fabs(2.0f * r - 1.0f - beta) * inv1mb);
                        // 結合済みペアには引力側を働かせない
                        if (f > 0.0f && iHasBond && isBonded(i, sorted_[m])) f = 0.0f;
                    }
                    const float s = f / d;
                    fx += ddx * s;
                    fy += ddy * s;
                    if (iFree && d2 < bondD2) {
                        const int j = sorted_[m];
                        if (j > i && canBond[ti][tj] && bondCount[j] < valence[tj] && !isBonded(i, j) &&
                            (hash3(seed, (uint32_t)i, (uint32_t)j) >> 8) < bondThresh)
                            cand.emplace_back(i, j);
                    }
                }
            }
            fx *= ff;
            fy *= ff;
            // バネ力
            if (iHasBond) {
                for (int s = 0; s < kMaxBonds; ++s) {
                    const int j = bonds[i][s];
                    if (j < 0) continue;
                    float ddx = wd(x[j] - xi), ddy = wd(y[j] - yi);
                    float d = std::sqrt(ddx * ddx + ddy * ddy);
                    if (d < 1e-7f) continue;
                    float fs = k * (d - L) / d;
                    fx += ddx * fs;
                    fy += ddy * fs;
                }
            }
            ax_[i] = fx;
            ay_[i] = fy;
        }
    });

    const float dt = p.dt;
    const float fric = std::pow(0.5f, dt / std::max(1e-4f, p.frictionHalfLife));
    const float noise = p.thermal * std::sqrt(dt) * 3.4641016f;  // 一様乱数 [-0.5,0.5) * sqrt(12) で分散 1
    const uint32_t seed2 = hash32(seed + 0x1234567u);
    pool_.parallelFor(n, chunks, [&](int, int b, int e) {
        for (int i = b; i < e; ++i) {
            float nx = 0.0f, ny = 0.0f;
            if (noise > 0.0f) {
                uint32_t h = hash3(seed2, (uint32_t)i, 0u);
                nx = (u01(h) - 0.5f) * noise;
                ny = (u01(hash32(h)) - 0.5f) * noise;
            }
            vx[i] = vx[i] * fric + ax_[i] * dt + nx;
            vy[i] = vy[i] * fric + ay_[i] * dt + ny;
            x[i] = wp(x[i] + vx[i] * dt);
            y[i] = wp(y[i] + vy[i] * dt);
        }
    });

    formBonds();
    breakBonds();
}

void Simulation::formBonds() {
    for (auto& list : candidates_) {
        for (auto [i, j] : list) {
            if (bondCount[i] >= valence[type[i]] || bondCount[j] >= valence[type[j]] || isBonded(i, j)) continue;
            for (auto& s : bonds[i]) if (s < 0) { s = j; break; }
            for (auto& s : bonds[j]) if (s < 0) { s = i; break; }
            ++bondCount[i];
            ++bondCount[j];
        }
    }
}

void Simulation::unlink(int i, int slot) {
    int j = bonds[i][slot];
    if (j < 0) return;
    bonds[i][slot] = -1;
    --bondCount[i];
    for (auto& s : bonds[j])
        if (s == i) { s = -1; --bondCount[j]; break; }
}

void Simulation::breakBonds() {
    const int n = count();
    const float bd2 = p.breakDist * p.breakDist;
    const uint32_t thresh = (uint32_t)(std::clamp(p.breakProb, 0.0f, 1.0f) * 16777216.0f);
    const uint32_t seed = hash32((uint32_t)stepCounter_ * 0x85ebca6bu + 77u);
    for (int i = 0; i < n; ++i) {
        if (!bondCount[i]) continue;
        const int ti = type[i];
        for (int s = 0; s < kMaxBonds; ++s) {
            const int j = bonds[i][s];
            if (j < 0) continue;
            // 価数の超過 (価数を下げた場合) は常に切る
            if (bondCount[i] > valence[ti]) { unlink(i, s); continue; }
            if (j < i) continue;  // ペアは小さい方の添字側で 1 回だけ判定
            float dx = wd(x[j] - x[i]), dy = wd(y[j] - y[i]);
            bool cut = dx * dx + dy * dy > bd2 || !canBond[ti][type[j]] ||
                       (thresh > 0 && (hash3(seed, (uint32_t)i, (uint32_t)j) >> 8) < thresh);
            if (cut) unlink(i, s);
        }
    }
}
