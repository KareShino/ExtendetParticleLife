#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gl_loader.h"

Color TypeColor(int t, float sat, float val) {
    float h = std::fmod(t * 30.0f, 360.0f) / 60.0f;
    float c = val * sat;
    float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float m = val - c;
    float r = 0, g = 0, b = 0;
    switch ((int)h) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    return {r + m, g + m, b + m};
}

static unsigned Compile(unsigned kind, const char* src) {
    unsigned s = glCreateShader(kind);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader error: %s\n", log);
    }
    return s;
}

bool Renderer::init() {
    const char* vs = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
uniform float uPointSize;
uniform float uInvWorld;
out vec4 vCol;
void main(){
    vec2 p = aPos * uInvWorld;
    gl_Position = vec4(p.x*2.0-1.0, 1.0-p.y*2.0, 0.0, 1.0);
    gl_PointSize = uPointSize;
    vCol = aCol;
})";
    const char* fs = R"(#version 330 core
in vec4 vCol;
uniform float uRound;
out vec4 oCol;
void main(){
    if (uRound > 0.5) {
        vec2 d = gl_PointCoord*2.0-1.0;
        float r2 = dot(d,d);
        if (r2 > 1.0) discard;
        oCol = vec4(vCol.rgb*(1.0-0.25*r2), vCol.a);
    } else oCol = vCol;
})";
    unsigned v = Compile(GL_VERTEX_SHADER, vs), f = Compile(GL_FRAGMENT_SHADER, fs);
    prog_ = glCreateProgram();
    glAttachShader(prog_, v);
    glAttachShader(prog_, f);
    glLinkProgram(prog_);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    uPointSize_ = glGetUniformLocation(prog_, "uPointSize");
    uRound_ = glGetUniformLocation(prog_, "uRound");
    uInvWorld_ = glGetUniformLocation(prog_, "uInvWorld");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return true;
}

void Renderer::draw(const Simulation& sim, int fbX, int fbY, int fbW, int fbH, float pointPx, bool showBonds,
                    float sat, float val) {
    if (fbW <= 0 || fbH <= 0) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(fbX, fbY, fbW, fbH);
    glClearColor(0.04f, 0.04f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const int side = std::min(fbW, fbH);
    const int ox = fbX + (fbW - side) / 2, oy = fbY + (fbH - side) / 2;
    glScissor(ox, oy, side, side);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(ox, oy, side, side);

    uint8_t pal[kMaxTypes][3];
    for (int t = 0; t < kMaxTypes; ++t) {
        Color c = TypeColor(t, sat, val);
        pal[t][0] = (uint8_t)(c.r * 255.0f);
        pal[t][1] = (uint8_t)(c.g * 255.0f);
        pal[t][2] = (uint8_t)(c.b * 255.0f);
    }

    const int n = sim.count();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glUniform1f(uInvWorld_, 1.0f / sim.world);
    const float W = sim.world, H = 0.5f * W;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    if (showBonds) {
        lines_.clear();
        auto push = [&](float ax, float ay, float bx, float by, const uint8_t* c) {
            lines_.push_back({ax, ay, c[0], c[1], c[2], 150});
            lines_.push_back({bx, by, c[0], c[1], c[2], 150});
        };
        for (int i = 0; i < n; ++i) {
            if (!sim.bondCount[i]) continue;
            for (int j : sim.bonds[i]) {
                if (j <= i) continue;
                float dx = sim.x[j] - sim.x[i], dy = sim.y[j] - sim.y[i];
                const uint8_t* c = pal[sim.type[i]];
                if (std::fabs(dx) <= H && std::fabs(dy) <= H) {
                    push(sim.x[i], sim.y[i], sim.x[j], sim.y[j], c);
                } else {  // 境界をまたぐ結合は両側に描く (はみ出しは scissor で切る)
                    if (dx > H) dx -= W; else if (dx < -H) dx += W;
                    if (dy > H) dy -= W; else if (dy < -H) dy += W;
                    push(sim.x[i], sim.y[i], sim.x[i] + dx, sim.y[i] + dy, c);
                    push(sim.x[j], sim.y[j], sim.x[j] - dx, sim.y[j] - dy, c);
                }
            }
        }
        if (!lines_.empty()) {
            glBufferData(GL_ARRAY_BUFFER, lines_.size() * sizeof(Vtx), lines_.data(), GL_STREAM_DRAW);
            glUniform1f(uRound_, 0.0f);
            glUniform1f(uPointSize_, 1.0f);
            glDrawArrays(GL_LINES, 0, (GLsizei)lines_.size());
        }
    }

    pts_.resize(n);
    for (int i = 0; i < n; ++i) {
        const uint8_t* c = pal[sim.type[i]];
        pts_[i] = {sim.x[i], sim.y[i], c[0], c[1], c[2], 255};
    }
    if (n > 0) {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glBufferData(GL_ARRAY_BUFFER, pts_.size() * sizeof(Vtx), pts_.data(), GL_STREAM_DRAW);
        glUniform1f(uRound_, 1.0f);
        glUniform1f(uPointSize_, std::max(1.0f, pointPx));
        glDrawArrays(GL_POINTS, 0, n);
    }
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
}
