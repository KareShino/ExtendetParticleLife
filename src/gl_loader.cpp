#include <windows.h>

#include "gl_loader.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GL_DEF(ret, name, ...) PFN_##name p_##name = nullptr;
GL_FUNCS(GL_DEF)
#undef GL_DEF

bool LoadGL() {
    bool ok = true;
#define GL_LOAD(ret, name, ...)                                      \
    p_##name = (PFN_##name)glfwGetProcAddress(#name);                \
    if (!p_##name) ok = false;
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    return ok;
}
