#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

#include "gl_loader.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "parallel.h"
#include "render.h"
#include "sim.h"
#include "ui.h"

static std::string WideToUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Windows 標準の日本語フォントを読み込む
static void LoadJapaneseFont(float sizePx) {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    // 日本語 + ギリシャ文字 (β) + 一般記号
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        static const ImWchar extra[] = {0x0370, 0x03FF, 0x2000, 0x22FF, 0};
        b.AddRanges(extra);
        b.BuildRanges(&ranges);
    }
    const wchar_t* candidates[] = {L"\\Fonts\\meiryo.ttc", L"\\Fonts\\YuGothM.ttc", L"\\Fonts\\msgothic.ttc"};
    for (auto c : candidates) {
        std::wstring path = std::wstring(winDir) + c;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        if (io.Fonts->AddFontFromFileTTF(WideToUtf8(path).c_str(), sizePx, nullptr, ranges.Data))
            return;
    }
    io.Fonts->AddFontDefault();
}

static void SetupInitialScene(Simulation& sim, AppState& st, int types) {
    sim.setNumTypes(types);
    sim.randomizeWeak();
    const int val[3] = {2, 1, 3};
    for (int a = 0; a < sim.numTypes; ++a) {
        sim.valence[a] = val[a % 3];
        for (int b = 0; b < sim.numTypes; ++b) sim.canBond[a][b] = true;
    }
    sim.randomFill(st.cap, st.ratios);
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(1600, 950, "拡張 Particle Life", nullptr, nullptr);
    if (!win) {
        MessageBoxW(nullptr, L"OpenGL 3.3 のウィンドウを作成できませんでした", L"エラー", MB_OK);
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (!LoadGL()) {
        MessageBoxW(nullptr, L"OpenGL 関数の読み込みに失敗しました", L"エラー", MB_OK);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(win, &xs, &ys);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(xs);
    LoadJapaneseFont(17.0f * xs);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    Renderer renderer;
    if (!renderer.init()) {
        MessageBoxW(nullptr, L"シェーダの初期化に失敗しました", L"エラー", MB_OK);
        return 1;
    }

    ThreadPool pool;
    Simulation sim(pool);
    AppState st;
    if (argc > 1) st.cap = std::clamp(std::atoi(argv[1]), 0, AppState::kMaxParticles);  // 任意: 初期粒子数
    if (argc > 2) sim.world = st.worldEdit = std::clamp((float)std::atof(argv[2]), 0.05f, 1000.0f);  // 任意: 世界の広さ
    const int types = argc > 3 ? std::clamp(std::atoi(argv[3]), 1, kMaxTypes) : 3;  // 任意: 初期の種類数
    SetupInitialScene(sim, st, types);
    st.numTypesEdit = sim.numTypes;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        if (!st.paused) {
            auto t0 = std::chrono::steady_clock::now();
            st.stepAcc += st.speed;
            const int steps = std::min(AppState::kMaxStepsPerFrame, (int)st.stepAcc);
            st.stepAcc -= (float)(int)st.stepAcc;
            for (int i = 0; i < steps; ++i) {
                sim.step();
                // 1 フレームの計算は 50ms までに抑えて UI の応答性を保つ (超過分は捨てる)
                if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(50)) break;
            }
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            st.simMs = st.simMs * 0.9 + ms * 0.1;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawUI(sim, st);
        HandlePreviewInput(sim, st);
        ImGui::Render();

        int fbW, fbH, winW, winH;
        glfwGetFramebufferSize(win, &fbW, &fbH);
        glfwGetWindowSize(win, &winW, &winH);
        const float sx = winW > 0 ? (float)fbW / winW : 1.0f, sy = winH > 0 ? (float)fbH / winH : 1.0f;
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        const int px = (int)(st.pvX * sx), pw = (int)(st.pvW * sx), ph = (int)(st.pvH * sy);
        const int py = fbH - (int)(st.pvY * sy) - ph;  // 左下原点へ変換
        renderer.draw(sim, px, py, pw, ph, std::max(1.5f, st.pointPx * xs / sim.world), st.showBonds, st.sat, st.val);
        glViewport(0, 0, fbW, fbH);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
