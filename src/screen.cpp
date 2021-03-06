/*
    src/screen.cpp -- Top-level widget and interface between NanoGUI and GLFW

    A significant redesign of this code was contributed by Christian Schueller.

    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <nanogui/screen.h>
#include <nanogui/opengl.h>
#include <map>
#include <iostream>

#if defined(_WIN32)
#  define NOMINMAX
#  undef APIENTRY

#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

#  define GLFW_EXPOSE_NATIVE_WGL
#  define GLFW_EXPOSE_NATIVE_WIN32
#  include <GLFW/glfw3native.h>
#endif

NAMESPACE_BEGIN(nanogui)

std::map<GLFWwindow *, Screen *> __nanogui_screens;

#if defined(NANOGUI_GLAD)
static bool gladInitialized = false;
#endif

/* Calculate pixel ratio for hi-dpi devices. */
static float get_pixel_ratio(GLFWwindow *window) {
#if defined(_WIN32)
    HWND hWnd = glfwGetWin32Window(window);
    HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    /* The following function only exists on Windows 8.1+, but we don't want to make that a dependency */
    static HRESULT (WINAPI *GetDpiForMonitor_)(HMONITOR, UINT, UINT*, UINT*) = nullptr;
    static bool GetDpiForMonitor_tried = false;

    if (!GetDpiForMonitor_tried) {
        auto shcore = LoadLibrary(TEXT("shcore"));
        if (shcore)
            GetDpiForMonitor_ = (decltype(GetDpiForMonitor_)) GetProcAddress(shcore, "GetDpiForMonitor");
        GetDpiForMonitor_tried = true;
    }

    if (GetDpiForMonitor_) {
        uint32_t dpiX, dpiY;
        if (GetDpiForMonitor_(monitor, 0 /* effective DPI */, &dpiX, &dpiY) == S_OK)
            return std::round(dpiX / 96.0);
    }
    return 1.f;
#elif defined(__linux__)
    (void) window;

    /* Try to read the pixel ratio from GTK */
    FILE *fp = popen("gsettings get org.gnome.desktop.interface scaling-factor", "r");
    if (!fp)
        return 1;

    int ratio = 1;
    if (fscanf(fp, "uint32 %i", &ratio) != 1)
        return 1;

    if (pclose(fp) != 0)
        return 1;

    return ratio >= 1 ? ratio : 1;
#else
    Vector2i fbSize, size;
    glfwGetFramebufferSize(window, &fbSize[0], &fbSize[1]);
    glfwGetWindowSize(window, &size[0], &size[1]);
    return (float)fbSize[0] / (float)size[0];
#endif
}

Screen::Screen()
    : ScreenCore(), mGLFWWindow(nullptr),  mBackground(0.3f, 0.3f, 0.32f, 1.f), mShutdownGLFWOnDestruct(false), mFullscreen(false) {
    memset(mCursors, 0, sizeof(GLFWcursor *) * (int) Cursor::CursorCount);
}

Screen::Screen(const Vector2i &size, const std::string &caption, bool resizable,
               bool fullscreen, int colorBits, int alphaBits, int depthBits,
               int stencilBits, int nSamples,
               unsigned int glMajor, unsigned int glMinor)
    : ScreenCore(), mGLFWWindow(nullptr), mBackground(0.3f, 0.3f, 0.32f, 1.f), mCaption(caption),
      mShutdownGLFWOnDestruct(false), mFullscreen(fullscreen) {
    memset(mCursors, 0, sizeof(GLFWcursor *) * (int) Cursor::CursorCount);

    /* Request a forward compatible OpenGL glMajor.glMinor core profile context.
       Default value is an OpenGL 3.3 core profile context. */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, glMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, glMinor);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_SAMPLES, nSamples);
    glfwWindowHint(GLFW_RED_BITS, colorBits);
    glfwWindowHint(GLFW_GREEN_BITS, colorBits);
    glfwWindowHint(GLFW_BLUE_BITS, colorBits);
    glfwWindowHint(GLFW_ALPHA_BITS, alphaBits);
    glfwWindowHint(GLFW_STENCIL_BITS, stencilBits);
    glfwWindowHint(GLFW_DEPTH_BITS, depthBits);
    glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, resizable ? GL_TRUE : GL_FALSE);

    if (fullscreen) {
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        mGLFWWindow = glfwCreateWindow(mode->width, mode->height,
                                       caption.c_str(), monitor, nullptr);
    } else {
        mGLFWWindow = glfwCreateWindow(size.x(), size.y(),
                                       caption.c_str(), nullptr, nullptr);
    }

    if (!mGLFWWindow)
        throw std::runtime_error("Could not create an OpenGL " +
                                 std::to_string(glMajor) + "." +
                                 std::to_string(glMinor) + " context!");

    glfwMakeContextCurrent(mGLFWWindow);

#if defined(NANOGUI_GLAD)
    if (!gladInitialized) {
        gladInitialized = true;
        if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress))
            throw std::runtime_error("Could not initialize GLAD!");
        glGetError(); // pull and ignore unhandled errors like GL_INVALID_ENUM
    }
#endif

    glfwGetFramebufferSize(mGLFWWindow, &mFBSize[0], &mFBSize[1]);
    mSize = size;
    glViewport(0, 0, mFBSize[0], mFBSize[1]);
    glClearColor(mBackground[0], mBackground[1], mBackground[2], mBackground[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glfwSwapInterval(0);
    glfwSwapBuffers(mGLFWWindow);

#if defined(__APPLE__)
    /* Poll for events once before starting a potentially
       lengthy loading process. This is needed to be
       classified as "interactive" by other software such
       as iTerm2 */

    glfwPollEvents();
#endif

    /* Propagate GLFW events to the appropriate Screen instance */
    glfwSetCursorPosCallback(mGLFWWindow,
        [](GLFWwindow *w, double x, double y) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen *s = it->second;
            if (!s->mProcessEvents)
                return;
            s->cursorPosCallbackEvent(x, y);
        }
    );

    glfwSetMouseButtonCallback(mGLFWWindow,
        [](GLFWwindow *w, int button, int action, int modifiers) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen *s = it->second;
            if (!s->mProcessEvents)
                return;
            s->mouseButtonCallbackEvent(button, action, modifiers);
        }
    );

    glfwSetKeyCallback(mGLFWWindow,
        [](GLFWwindow *w, int key, int scancode, int action, int mods) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen *s = it->second;
            if (!s->mProcessEvents)
                return;
            s->keyCallbackEvent(key, scancode, action, mods);
        }
    );

    glfwSetCharCallback(mGLFWWindow,
        [](GLFWwindow *w, unsigned int codepoint) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen *s = it->second;
            if (!s->mProcessEvents)
                return;
            s->charCallbackEvent(codepoint);
        }
    );

    glfwSetDropCallback(mGLFWWindow,
        [](GLFWwindow *w, int count, const char **filenames) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen *s = it->second;
            if (!s->mProcessEvents)
                return;
            s->dropCallbackEvent(count, filenames);
        }
    );

    glfwSetScrollCallback(mGLFWWindow,
        [](GLFWwindow *w, double x, double y) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen *s = it->second;
            if (!s->mProcessEvents)
                return;
            s->scrollCallbackEvent(x, y);
        }
    );

    /* React to framebuffer size events -- includes window
       size events and also catches things like dragging
       a window from a Retina-capable screen to a normal
       screen on Mac OS X */
    glfwSetFramebufferSizeCallback(mGLFWWindow,
        [](GLFWwindow* w, int width, int height) {
            auto it = __nanogui_screens.find(w);
            if (it == __nanogui_screens.end())
                return;
            Screen* s = it->second;

            if (!s->mProcessEvents)
                return;

            s->resizeCallbackEvent(width, height);
        }
    );

    initialize(mGLFWWindow, true);
}

void Screen::initialize(GLFWwindow *window, bool shutdownGLFWOnDestruct) {
    mGLFWWindow = window;
    mShutdownGLFWOnDestruct = shutdownGLFWOnDestruct;
    glfwGetWindowSize(mGLFWWindow, &mSize[0], &mSize[1]);
    glfwGetFramebufferSize(mGLFWWindow, &mFBSize[0], &mFBSize[1]);

    mPixelRatio = get_pixel_ratio(window);

#if defined(_WIN32) || defined(__linux__)
    if (mPixelRatio != 1 && !mFullscreen)
        glfwSetWindowSize(window, mSize.x() * mPixelRatio, mSize.y() * mPixelRatio);
#endif

#if defined(NANOGUI_GLAD)
    if (!gladInitialized) {
        gladInitialized = true;
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            throw std::runtime_error("Could not initialize GLAD!");
        glGetError(); // pull and ignore unhandled errors like GL_INVALID_ENUM
    }
#endif

    init(mSize, get_pixel_ratio(window));

    mVisible = glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
    __nanogui_screens[mGLFWWindow] = this;

    for (int i=0; i < (int) Cursor::CursorCount; ++i)
        mCursors[i] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR + i);
}

Screen::~Screen() {
    __nanogui_screens.erase(mGLFWWindow);
    for (int i=0; i < (int) Cursor::CursorCount; ++i) {
        if (mCursors[i])
            glfwDestroyCursor(mCursors[i]);
    }
    if (mGLFWWindow && mShutdownGLFWOnDestruct)
        glfwDestroyWindow(mGLFWWindow);
}

void Screen::setVisible(bool visible) {
    if (mVisible != visible) {
        mVisible = visible;

        if (visible)
            glfwShowWindow(mGLFWWindow);
        else
            glfwHideWindow(mGLFWWindow);
    }
}

void Screen::setCaption(const std::string &caption) {
    if (caption != mCaption) {
        glfwSetWindowTitle(mGLFWWindow, caption.c_str());
        mCaption = caption;
    }
}

void Screen::setSize(const Vector2i &size) {
    Widget::setSize(size);

#if defined(_WIN32) || defined(__linux__)
    glfwSetWindowSize(mGLFWWindow, size.x() * mPixelRatio, size.y() * mPixelRatio);
#else
    glfwSetWindowSize(mGLFWWindow, size.x(), size.y());
#endif
}

void Screen::drawAll() {
    glClearColor(mBackground[0], mBackground[1], mBackground[2], mBackground[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glfwMakeContextCurrent(mGLFWWindow);

    glfwGetFramebufferSize(mGLFWWindow, &mFBSize[0], &mFBSize[1]);
    glfwGetWindowSize(mGLFWWindow, &mSize[0], &mSize[1]);

#if defined(_WIN32) || defined(__linux__)
    mSize = (mSize / mPixelRatio).cast<int>();
    mFBSize = (mSize * mPixelRatio).cast<int>();
#else
    /* Recompute pixel ratio on OSX */
    if (mSize[0])
        mPixelRatio = (float) mFBSize[0] / (float) mSize[0];
#endif

    glViewport(0, 0, mFBSize[0], mFBSize[1]);

    drawContents();

    if (mVisible)
        drawWidgets();

    glfwSwapBuffers(mGLFWWindow);
}

bool Screen::dropCallbackEvent(int count, const char **filenames) {
    std::vector<std::string> arg(count);
    for (int i = 0; i < count; ++i)
        arg[i] = filenames[i];
    return dropEvent(arg);
}

bool Screen::resizeCallbackEvent(int, int) {
    Vector2i fbSize, size;
    glfwGetFramebufferSize(mGLFWWindow, &fbSize[0], &fbSize[1]);
    glfwGetWindowSize(mGLFWWindow, &size[0], &size[1]);

#if defined(_WIN32) || defined(__linux__)
    size /= mPixelRatio;
#endif

    if (mFBSize == Vector2i(0, 0) || size == Vector2i(0, 0))
        return false;

    mFBSize = fbSize; mSize = size;

    try {
        return resizeEvent(mSize);
    } catch (const std::exception &e) {
        std::cerr << "Caught exception in event handler: " << e.what()
                  << std::endl;
        abort();
    }
}

void Screen::setCursorAppearance(int c) {
    glfwSetCursor(mGLFWWindow, mCursors[c]);
}

/// Reimplement this anc call glfwSetClipboardString() with the string given by the parameter
void Screen::setClipboardString(const std::string &str) {
    glfwSetClipboardString(mGLFWWindow, str.c_str());
}

/// Reimplement this anc call glfwGetClipboardString()
std::string Screen::getClipboardString() {
    return std::string(glfwGetClipboardString(mGLFWWindow));
}

NAMESPACE_END(nanogui)
