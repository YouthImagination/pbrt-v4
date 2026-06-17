// pbrt_dll.cpp - Safe DLL bridge for embedding PBRT rendering in Blender
//
// This DLL provides a C API for in-memory rendering that is safe to call
// from within a host application (e.g. Blender).  Key safety features:
//
//   1. Uses InitPBRT_Embedded() which does NOT install
//      SetUnhandledExceptionFilter (preserving Blender's crash handler).
//   2. All PBRT errors are caught as PBRTError exceptions instead of
//      calling quick_exit(), so the host process survives.
//   3. Proper cleanup in all code paths via RAII-style try/catch.

#include <pbrt/pbrt.h>
#include <pbrt/cpu/render.h>
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/scene.h>
#include <pbrt/cameras.h>
#include <pbrt/film.h>
#include <pbrt/util/image.h>
#include <pbrt/util/error.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>

#ifdef PBRT_BUILD_GPU_RENDERER
#include <pbrt/wavefront/wavefront.h>
#endif

// Thread-local error message buffer for the last error.
static thread_local char g_lastError[4096] = {0};
static thread_local bool g_initialized = false;

extern "C" {

// Returns a pointer to the last error message string.
// The caller must NOT free this pointer.
__declspec(dllexport) const char* pbrt_get_last_error() {
    return g_lastError;
}

// Render a PBRT scene from an in-memory string and write pixel data
// into the provided RGBA float buffer.
//
// Parameters:
//   scene_text  - null-terminated UTF-8 scene description string
//   width       - expected image width  (informational; actual resolution
//                 comes from the scene's Film directive)
//   height      - expected image height
//   out_rgba    - caller-allocated float buffer of size width*height*4
//   use_gpu     - whether to attempt GPU rendering
//
// Returns true on success, false on error (call pbrt_get_last_error()).
__declspec(dllexport) bool render_pbrt_in_memory(
    const char* scene_text,
    int width,
    int height,
    float* out_rgba,
    bool use_gpu
) {
    using namespace pbrt;

    // Clear the error buffer
    g_lastError[0] = '\0';

    if (!scene_text || !out_rgba || width <= 0 || height <= 0) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "Invalid arguments: scene_text=%p, out_rgba=%p, width=%d, height=%d",
                 scene_text, out_rgba, width, height);
        return false;
    }

    // Zero the output buffer so partial failures still produce a black image
    // rather than garbage.
    std::memset(out_rgba, 0, sizeof(float) * width * height * 4);

    // Initialize PBRT in embedded (DLL-safe) mode.
    // This enables PBRTError exceptions instead of quick_exit().
    PBRTOptions options;
    options.useGPU = use_gpu;
    options.quiet = true;

    try {
        InitPBRT_Embedded(options);
        g_initialized = true;
    } catch (const PBRTError& e) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "PBRT initialization failed: %s", e.what());
        g_initialized = false;
        return false;
    } catch (const std::exception& e) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "PBRT initialization failed (std::exception): %s", e.what());
        g_initialized = false;
        return false;
    } catch (...) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "PBRT initialization failed with unknown exception");
        g_initialized = false;
        return false;
    }

    try {
        // Parse scene in-memory from string
        BasicScene scene;
        BasicSceneBuilder builder(&scene);
        ParseString(&builder, std::string(scene_text));

        // Execute render
#ifdef PBRT_BUILD_GPU_RENDERER
        if (Options->useGPU || Options->wavefront) {
            RenderWavefront(scene);
        } else {
            RenderCPU(scene);
        }
#else
        RenderCPU(scene);
#endif

        // Retrieve image from Film
        Camera camera = scene.GetCamera();
        Film film = camera.GetFilm();
        ImageMetadata metadata;
        Image img = film.GetImage(&metadata);
        Point2i res = img.Resolution();
        int nChannels = img.NChannels();

        // Verify resolution matches expectations
        int out_w = std::min(res.x, width);
        int out_h = std::min(res.y, height);

        // Copy pixels into the caller's float buffer.
        // PBRT images are top-to-bottom (y=0 at top).
        // Blender expects bottom-to-top (y=0 at bottom).
        for (int y = 0; y < out_h; ++y) {
            int dest_y = y;                // Blender: y=0 is bottom
            int src_y = out_h - 1 - y;     // PBRT: y=0 is top

            for (int x = 0; x < out_w; ++x) {
                int dest_idx = 4 * (dest_y * width + x);

                if (nChannels >= 3) {
                    out_rgba[dest_idx + 0] = img.GetChannel({x, src_y}, 0);
                    out_rgba[dest_idx + 1] = img.GetChannel({x, src_y}, 1);
                    out_rgba[dest_idx + 2] = img.GetChannel({x, src_y}, 2);
                } else if (nChannels == 1) {
                    float val = img.GetChannel({x, src_y}, 0);
                    out_rgba[dest_idx + 0] = val;
                    out_rgba[dest_idx + 1] = val;
                    out_rgba[dest_idx + 2] = val;
                } else {
                    out_rgba[dest_idx + 0] = 0.0f;
                    out_rgba[dest_idx + 1] = 0.0f;
                    out_rgba[dest_idx + 2] = 0.0f;
                }

                if (nChannels >= 4) {
                    out_rgba[dest_idx + 3] = img.GetChannel({x, src_y}, 3);
                } else {
                    out_rgba[dest_idx + 3] = 1.0f; // Default Alpha
                }
            }
        }

        // Cleanup PBRT state
        CleanupPBRT();
        g_initialized = false;
        SetDLLSafeMode(false);
        return true;

    } catch (const PBRTError& e) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "PBRT render error: %s", e.what());
        std::cerr << "[PBRT DLL] " << g_lastError << std::endl;
    } catch (const std::exception& e) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "PBRT render error (std::exception): %s", e.what());
        std::cerr << "[PBRT DLL] " << g_lastError << std::endl;
    } catch (...) {
        snprintf(g_lastError, sizeof(g_lastError),
                 "PBRT render error: unknown exception");
        std::cerr << "[PBRT DLL] " << g_lastError << std::endl;
    }

    // Cleanup on error path
    if (g_initialized) {
        try {
            CleanupPBRT();
        } catch (...) {
            // Ignore cleanup errors
        }
        g_initialized = false;
    }
    SetDLLSafeMode(false);
    return false;
}

} // extern "C"
