#include "window.h"

#include <stdio.h>

#ifdef COLORS_HAVE_SDL2
#include <SDL2/SDL.h>

static SDL_Window* g_window;
static SDL_Renderer* g_renderer;
static SDL_Texture* g_texture;
static u32 g_tex_w;
static u32 g_tex_h;
static bool g_quit;
static u32 g_buttons;   // live WPAD-shaped button mask

bool window_init(u32 width, u32 height, const char* title, int scale) {
    if (g_window != NULL)
        return true;
    if (width == 0u || height == 0u)
        return false;
    if (scale < 1)
        scale = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[window] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    g_window = SDL_CreateWindow(title != NULL ? title : "ColorsRecomp",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                (int)width * scale, (int)height * scale,
                                SDL_WINDOW_RESIZABLE);
    if (g_window == NULL) {
        fprintf(stderr, "[window] SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_renderer = SDL_CreateRenderer(
        g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (g_renderer == NULL)
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (g_renderer == NULL) {
        fprintf(stderr, "[window] SDL_CreateRenderer failed: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888,
                                  SDL_TEXTUREACCESS_STREAMING, (int)width,
                                  (int)height);
    if (g_texture == NULL) {
        fprintf(stderr, "[window] SDL_CreateTexture failed: %s\n",
                SDL_GetError());
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }
    SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_NONE);
    g_tex_w = width;
    g_tex_h = height;
    g_quit = false;

    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);
    fprintf(stderr, "[window] %ux%u at %dx scale\n", width, height, scale);
    return true;
}

void window_present(const u8* rgba, u32 width, u32 height) {
    if (g_window == NULL || g_renderer == NULL || g_texture == NULL || g_quit)
        return;
    if (width != g_tex_w || height != g_tex_h)
        return;
    SDL_UpdateTexture(g_texture, NULL, rgba, (int)width * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);

    // Title doubles as a liveness indicator (frame count and host frame rate).
    static u32 frames;
    static u32 last_tick;
    ++frames;
    const u32 now = SDL_GetTicks();
    if ((frames % 30u) == 0u) {
        char title[96];
        const u32 dt = now - last_tick;
        const double fps = dt != 0u ? 30000.0 / (double)dt : 0.0;
        snprintf(title, sizeof(title), "ColorsRecomp - frame %u (%.1f fps)",
                 frames, fps);
        SDL_SetWindowTitle(g_window, title);
        last_tick = now;
    }
}

// Physical key -> Wii Remote button bit. Arrows drive the D-pad, Z/X are 1/2,
// Space is A, Left Ctrl is B, Return is +, Backspace is -, H is Home.
static u32 key_bit(SDL_Keycode sym) {
    switch (sym) {
    case SDLK_LEFT: return 0x0001u;
    case SDLK_RIGHT: return 0x0002u;
    case SDLK_DOWN: return 0x0004u;
    case SDLK_UP: return 0x0008u;
    case SDLK_RETURN: return 0x0010u;
    case SDLK_x: return 0x0100u;
    case SDLK_z: return 0x0200u;
    case SDLK_LCTRL: return 0x0400u;
    case SDLK_SPACE: return 0x0800u;
    case SDLK_BACKSPACE: return 0x1000u;
    case SDLK_h: return 0x8000u;
    default: return 0u;
    }
}

bool window_poll(void) {
    if (g_window == NULL)
        return !g_quit;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            g_quit = true;
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                (ev.key.keysym.sym == SDLK_q &&
                 (ev.key.keysym.mod & KMOD_CTRL) != 0))
                g_quit = true;
            g_buttons |= key_bit(ev.key.keysym.sym);
            break;
        case SDL_KEYUP:
            g_buttons &= ~key_bit(ev.key.keysym.sym);
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                g_buttons = 0u;
            break;
        default:
            break;
        }
    }
    return !g_quit;
}

u32 window_buttons(void) {
    return g_window != NULL ? g_buttons : 0u;
}

bool window_active(void) {
    return g_window != NULL;
}

bool window_should_quit(void) {
    return g_quit;
}

void window_shutdown(void) {
    if (g_texture != NULL) {
        SDL_DestroyTexture(g_texture);
        g_texture = NULL;
    }
    if (g_renderer != NULL) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window != NULL) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();
}

#else /* !COLORS_HAVE_SDL2 */

bool window_init(u32 width, u32 height, const char* title, int scale) {
    (void)width;
    (void)height;
    (void)title;
    (void)scale;
    fprintf(stderr, "[window] built without SDL2; running headless\n");
    return false;
}

void window_present(const u8* rgba, u32 width, u32 height) {
    (void)rgba;
    (void)width;
    (void)height;
}

bool window_poll(void) {
    return true;
}

u32 window_buttons(void) {
    return 0u;
}

bool window_active(void) {
    return false;
}

bool window_should_quit(void) {
    return false;
}

void window_shutdown(void) {}

#endif
