#include "sim.h"

#include <algorithm>
#include <cmath>
#include <immintrin.h>
#include <intrin.h>

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

// ---- AVX2 カーネル (実行時に CPU 対応を確認し、非対応ならスカラー版を使う) ----
bool CpuHasAVX2() {
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuid(r, 1);
    const bool osxsave = (r[2] & (1 << 27)) != 0, avx = (r[2] & (1 << 28)) != 0;
    if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) != 0;
}
const bool kHasAVX2 = CpuHasAVX2();

struct KernelArgs {
    float rMax2, invR, beta, invBeta, inv1mb, bondD2;
    const float* wrow;
    int b0, b1, b2, b3;
    bool iHasBond, iFree;
};

// 区間 [b, e) の粒子から受ける弱い力を 8 個ずつ計算する (fx, fy に加算。倍率は呼び出し側)
template <class F>
inline void ForceSegAVX2(const float* sx, const float* sy, const uint8_t* st, const int* sorted, int b, int e,
                         float ox, float oy, const KernelArgs& k, float& fx, float& fy, F&& tryBond) {
    const __m256 vox = _mm256_set1_ps(ox), voy = _mm256_set1_ps(oy);
    const __m256 vR2 = _mm256_set1_ps(k.rMax2), vEps = _mm256_set1_ps(1e-12f);
    const __m256 vInvR = _mm256_set1_ps(k.invR), vBeta = _mm256_set1_ps(k.beta);
    const __m256 vInvB = _mm256_set1_ps(k.invBeta), vInv1mb = _mm256_set1_ps(k.inv1mb);
    const __m256 one = _mm256_set1_ps(1.0f), two = _mm256_set1_ps(2.0f), onePb = _mm256_set1_ps(1.0f + k.beta);
    const __m256 absMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
    const __m256 zero = _mm256_setzero_ps(), vBD2 = _mm256_set1_ps(k.bondD2);
    const __m256i lane = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const __m256i vb0 = _mm256_set1_epi32(k.b0), vb1 = _mm256_set1_epi32(k.b1);
    const __m256i vb2 = _mm256_set1_epi32(k.b2), vb3 = _mm256_set1_epi32(k.b3);
    __m256 afx = zero, afy = zero;
    for (int m = b; m < e; m += 8) {
        const __m256 dx = _mm256_add_ps(_mm256_loadu_ps(sx + m), vox);
        const __m256 dy = _mm256_add_ps(_mm256_loadu_ps(sy + m), voy);
        const __m256 d2 = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
        const __m256 tail = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(e - m), lane));
        const __m256 in = _mm256_and_ps(
            _mm256_and_ps(_mm256_cmp_ps(d2, vR2, _CMP_LT_OQ), _mm256_cmp_ps(d2, vEps, _CMP_GT_OQ)), tail);
        if (_mm256_movemask_ps(in) == 0) continue;
        const __m256 d = _mm256_sqrt_ps(_mm256_max_ps(d2, vEps));
        const __m256 r = _mm256_mul_ps(d, vInvR);
        const __m256 rep = _mm256_sub_ps(_mm256_mul_ps(r, vInvB), one);
        const __m256i idx = _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i*)(st + m)));
        const __m256 w = _mm256_i32gather_ps(k.wrow, idx, 4);
        const __m256 t = _mm256_and_ps(_mm256_sub_ps(_mm256_mul_ps(two, r), onePb), absMask);
        const __m256 tri = _mm256_mul_ps(w, _mm256_sub_ps(one, _mm256_mul_ps(t, vInv1mb)));
        __m256 f = _mm256_blendv_ps(tri, rep, _mm256_cmp_ps(r, vBeta, _CMP_LT_OQ));
        if (k.iHasBond) {  // 結合済みペアには引力側を働かせない
            const __m256i j = _mm256_loadu_si256((const __m256i*)(sorted + m));
            const __m256i bm = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi32(j, vb0), _mm256_cmpeq_epi32(j, vb1)),
                                               _mm256_or_si256(_mm256_cmpeq_epi32(j, vb2), _mm256_cmpeq_epi32(j, vb3)));
            f = _mm256_andnot_ps(_mm256_and_ps(_mm256_castsi256_ps(bm), _mm256_cmp_ps(f, zero, _CMP_GT_OQ)), f);
        }
        const __m256 s = _mm256_and_ps(_mm256_div_ps(f, d), in);
        afx = _mm256_add_ps(afx, _mm256_mul_ps(dx, s));
        afy = _mm256_add_ps(afy, _mm256_mul_ps(dy, s));
        if (k.iFree) {
            int bits = _mm256_movemask_ps(_mm256_and_ps(_mm256_cmp_ps(d2, vBD2, _CMP_LT_OQ), in));
            while (bits) {
                unsigned long l;
                _BitScanForward(&l, (unsigned long)bits);
                bits &= bits - 1;
                tryBond(m + (int)l);
            }
        }
    }
    alignas(32) float hx[8], hy[8];
    _mm256_store_ps(hx, afx);
    _mm256_store_ps(hy, afy);
    for (int q = 0; q < 8; ++q) { fx += hx[q]; fy += hy[q]; }
}


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

void Simulation::randomizeCanBond() {
    std::bernoulli_distribution B(0.5);
    for (int a = 0; a < numTypes; ++a)
        for (int b = a; b < numTypes; ++b) setCanBond(a, b, B(rng_));
}

void Simulation::randomizeValence() {
    std::uniform_int_distribution<int> U(0, kMaxBonds);
    for (int a = 0; a < numTypes; ++a) valence[a] = U(rng_);
}

void Simulation::randomizeParams() {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    auto lin = [&](float lo, float hi) { return lo + (hi - lo) * U(rng_); };
    auto logu = [&](float lo, float hi) { return lo * std::pow(hi / lo, U(rng_)); };
    const float maxR = world * 0.45f;
    p.rMax = std::min(logu(0.03f, 0.15f), maxR);
    p.beta = lin(0.1f, 0.5f);
    p.frictionHalfLife = logu(0.01f, 0.2f);
    p.forceFactor = lin(2.0f, 30.0f);
    p.bondLength = logu(0.005f, 0.04f);
    p.springK = lin(50.0f, 1000.0f);
    p.bondDist = std::min(p.bondLength * lin(1.0f, 1.8f), maxR);
    p.breakDist = p.bondDist * lin(1.5f, 3.0f);
    p.bondProb = logu(0.005f, 0.5f);
    p.breakProb = logu(1e-5f, 1e-2f);
    p.thermal = lin(0.0f, 0.5f);
}

void Simulation::buildGrid() {
    const int n = count();
    float cell = std::min(std::max(p.rMax, p.bondDist), world * 0.45f);
    gridN_ = std::max(1, std::min(1024, (int)(world / cell)));
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
    // SIMD で末尾を 8 要素単位で読めるよう余白を付ける (余白は遠方の値で範囲外になる)
    sorted_.assign(n + 8, -1);
    sx_.assign(n + 8, 1e15f); sy_.assign(n + 8, 1e15f); st_.assign(n + 8, 0);
    std::vector<int> cursor(cellStart_.begin(), cellStart_.end() - 1);
    for (int i = 0; i < n; ++i) {
        int k = cursor[cellOf_[i]]++;
        sorted_[k] = i;
        sx_[k] = x[i]; sy_[k] = y[i]; st_[k] = type[i];
    }
}

#ifdef SIM_PROFILE
#include <chrono>
double g_prof[5];
#include <atomic>
std::atomic<long long> g_pairs{0};
#define PROF_MARK(k) { auto _t = std::chrono::steady_clock::now(); g_prof[k] += std::chrono::duration<double, std::milli>(_t - _pt).count(); _pt = _t; }
#else
#define PROF_MARK(k)
#endif

void Simulation::step() {
    ++stepCounter_;
    const int n = count();
    if (n == 0) return;
#ifdef SIM_PROFILE
    auto _pt = std::chrono::steady_clock::now();
#endif
    buildGrid();
    PROF_MARK(0);
    ax_.resize(n);
    ay_.resize(n);

    const int chunks = pool_.threadCount() * 32;
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

    // 近傍区間: セル (cx,cy) の周囲 3x3 を、行ごとの連続区間 + 折り返し用の座標シフトで表す
    struct Seg { int b, e; float shx, shy; };
    const bool generic = G < 3;  // セルが 3 未満なら重複を避けて最短像で計算
    const float W = world;
    auto buildSegs = [&](int cx, int cy, Seg* seg) -> int {
        int ns = 0;
        if (generic) {
            int nb[9], nn = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int cc = (cx + dx + G) % G + ((cy + dy + G) % G) * G;
                    bool dup = false;
                    for (int q = 0; q < nn; ++q) dup |= nb[q] == cc;
                    if (!dup) nb[nn++] = cc;
                }
            for (int q = 0; q < nn; ++q) seg[ns++] = {cellStart_[nb[q]], cellStart_[nb[q] + 1], 0.0f, 0.0f};
            return ns;
        }
        for (int dy = -1; dy <= 1; ++dy) {
            int ry = cy + dy;
            float shy = 0.0f;
            if (ry < 0) { ry += G; shy = -W; } else if (ry >= G) { ry -= G; shy = W; }
            const int row = ry * G;
            if (cx >= 1 && cx + 1 < G) {
                seg[ns++] = {cellStart_[row + cx - 1], cellStart_[row + cx + 2], 0.0f, shy};
            } else if (cx == 0) {
                seg[ns++] = {cellStart_[row], cellStart_[row + 2], 0.0f, shy};
                seg[ns++] = {cellStart_[row + G - 1], cellStart_[row + G], -W, shy};
            } else {  // cx == G-1
                seg[ns++] = {cellStart_[row + G - 2], cellStart_[row + G], 0.0f, shy};
                seg[ns++] = {cellStart_[row], cellStart_[row + 1], W, shy};
            }
        }
        return ns;
    };

    const bool useSimd = kHasAVX2 && simdEnabled && !generic;
    // 粒子 (セル順に整列済み) 単位で分割して負荷を均す。区間はセルが変わったときだけ作り直す
    pool_.parallelFor(n, chunks, [&](int chunk, int kb, int ke) {
      auto& cand = candidates_[chunk];
      Seg seg[9];
      int ns = 0, curCell = -1;
      {
        for (int kk = kb; kk < ke; ++kk) {
            const int i = sorted_[kk];
            if (cellOf_[i] != curCell) {
                curCell = cellOf_[i];
                ns = buildSegs(curCell % G, curCell / G, seg);
            }
            const float xi = sx_[kk], yi = sy_[kk];
            const int ti = st_[kk];
            const float* wrow = weak[ti];
            const bool iHasBond = bondCount[i] > 0;
            const bool iFree = bondCount[i] < valence[ti] && bondThresh > 0;
            const int b0 = bonds[i][0], b1 = bonds[i][1], b2 = bonds[i][2], b3 = bonds[i][3];
            float fx = 0.0f, fy = 0.0f;
#ifdef SIM_PROFILE
            { long long c = 0; for (int q = 0; q < ns; ++q) c += seg[q].e - seg[q].b; g_pairs += c; }
#endif
            auto tryBond = [&](int m) {
                const int j = sorted_[m], tj = st_[m];
                if (j > i && canBond[ti][tj] && bondCount[j] < valence[tj] && !isBonded(i, j) &&
                    (hash3(seed, (uint32_t)i, (uint32_t)j) >> 8) < bondThresh)
                    cand.emplace_back(i, j);
            };
            if (useSimd) {
                KernelArgs ka{rMax2, invR, beta, invBeta, inv1mb, bondD2, wrow, b0, b1, b2, b3, iHasBond, iFree};
                for (int q = 0; q < ns; ++q)
                    ForceSegAVX2(sx_.data(), sy_.data(), st_.data(), sorted_.data(), seg[q].b, seg[q].e,
                                 seg[q].shx - xi, seg[q].shy - yi, ka, fx, fy, tryBond);
                _mm256_zeroupper();
            }
            for (int q = 0; q < (useSimd ? 0 : ns); ++q) {
                const int end = seg[q].e;
                const float ox = seg[q].shx - xi, oy = seg[q].shy - yi;
                for (int m = seg[q].b; m < end; ++m) {
                    float ddx = sx_[m] + ox, ddy = sy_[m] + oy;
                    if (generic) { ddx = wd(ddx); ddy = wd(ddy); }
                    const float d2 = ddx * ddx + ddy * ddy;
                    // 範囲判定・関数形の選択は分岐なし (select) で行い、分岐予測ミスを避ける
                    const bool inR = (d2 < rMax2) & (d2 > 1e-12f);
                    const float d = std::sqrt(std::max(d2, 1e-12f)), r = d * invR;
                    const int tj = st_[m];
                    const float rep = r * invBeta - 1.0f;  // 共通の近距離斥力
                    const float tri = wrow[tj] * (1.0f - std::fabs(2.0f * r - 1.0f - beta) * inv1mb);
                    float f = r < beta ? rep : tri;
                    // 結合済みペアには引力側を働かせない
                    if (iHasBond) {
                        const int j = sorted_[m];
                        const bool bonded = (j == b0) | (j == b1) | (j == b2) | (j == b3);
                        f = (bonded & (f > 0.0f)) ? 0.0f : f;
                    }
                    const float s = inR ? f / d : 0.0f;
                    fx += ddx * s;
                    fy += ddy * s;
                    if (iFree && d2 < bondD2) tryBond(m);
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
      }
    });
    PROF_MARK(1);

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

    PROF_MARK(2);
    formBonds();
    PROF_MARK(3);
    breakBonds();
    PROF_MARK(4);
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
    // 判定は並列 (読み取りのみ)。ペアは小さい方の添字側で 1 回だけ判定し、スロットごとのビットに記録
    cutMask_.assign(n, 0);
    pool_.parallelFor(n, pool_.threadCount() * 8, [&](int, int b, int e) {
        for (int i = b; i < e; ++i) {
            if (!bondCount[i]) continue;
            const int ti = type[i];
            uint8_t mask = 0;
            for (int s = 0; s < kMaxBonds; ++s) {
                const int j = bonds[i][s];
                if (j < i) continue;
                float dx = wd(x[j] - x[i]), dy = wd(y[j] - y[i]);
                bool cut = dx * dx + dy * dy > bd2 || !canBond[ti][type[j]] ||
                           (thresh > 0 && (hash3(seed, (uint32_t)i, (uint32_t)j) >> 8) < thresh);
                if (cut) mask |= (uint8_t)(1u << s);
            }
            cutMask_[i] = mask;
        }
    });
    for (int i = 0; i < n; ++i) {
        if (cutMask_[i])
            for (int s = 0; s < kMaxBonds; ++s)
                if (cutMask_[i] & (1u << s)) unlink(i, s);
        // 価数の超過 (価数を下げた場合) は常に切る
        for (int s = kMaxBonds - 1; s >= 0 && bondCount[i] > valence[type[i]]; --s) unlink(i, s);
    }
}
