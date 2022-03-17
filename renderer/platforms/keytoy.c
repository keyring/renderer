#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#endif
#include "devices.h"
#include "../core/graphics.h"
#include "../core/image.h"
#include "../core/macro.h"
#include "../core/platform.h"
#include "../core/private.h"

struct buffer_object {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t handle;
    uint32_t size;
    uint8_t *vaddr;
    uint32_t fb_id;
};

const int BUFFER_COUNT = 2;
struct buffer_object present_buf[BUFFER_COUNT];
unsigned int buffer_index = 0;

static int create_dumb_fb(int fd, struct buffer_object *bo)
{
    struct drm_mode_create_dumb create = {};
    struct drm_mode_map_dumb map = {};

    create.width = bo->width;
    create.height = bo->height;
    create.bpp = 32;

    drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

    bo->pitch = create.pitch;
    bo->size = create.size;
    bo->handle = create.handle;
    drmModeAddFB(fd, bo->width, bo->height, 24, 32, bo->pitch, bo->handle, &bo->fb_id);

    map.handle = create.handle;
    drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

    bo->vaddr = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);

    memset(bo->vaddr, 0xff, bo->size);

    return 0;
}

void destroy_dumb_fb(int fd, struct buffer_object *bo)
{
    struct drm_mode_destroy_dumb destroy = {};
    drmModeRmFB(fd, bo->fb_id);
    munmap(bo->vaddr, bo->size);
    destroy.handle = bo->handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}


struct window {
    device_t *render_device;
    image_t *surface;
    /* common data */
    int should_close;
    char keys[KEY_NUM];
    char buttons[BUTTON_NUM];
    callbacks_t callbacks;
    void *userdata;
};

/* platform initialization */

static void initialize_path(void) {
    char path[PATH_SIZE];
    int error;

#if defined(__FreeBSD__)
    size_t bytes;
    int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    assert(sysctl(mib, 4, path, &bytes, 0x0, 0) == 0);
#else
    ssize_t bytes;
    bytes = readlink("/proc/self/exe", path, PATH_SIZE - 1);
    assert(bytes != -1);
#endif
    path[bytes] = '\0';
    *strrchr(path, '/') = '\0';

    error = chdir(path);
    assert(error == 0);
    error = chdir("assets");
    assert(error == 0);
    UNUSED_VAR(error);
}

void platform_initialize(void) {
    initialize_path();
}

void platform_terminate(void) {

}

/* window related functions */

window_t *window_create(const char *title, int width, int height) {
    window_t *window;
    device_t *render_device;
    image_t *surface;

    render_device = (device_t *)malloc(sizeof(device_t));
    CreateRenderDevice(render_device);
    surface = image_create(width, height, 4, FORMAT_LDR);

    width = render_device->default_fb_width;
    height = render_device->default_fb_height;

    window = (window_t*)malloc(sizeof(window_t));
    memset(window, 0, sizeof(window_t));
    window->render_device = render_device;
    window->surface = surface;

    present_buf[0].width = width;
    present_buf[0].height = height;
    present_buf[1].width = width;
    present_buf[1].height = height;

    create_dumb_fb(render_device->drm_fd, &present_buf[0]);
    create_dumb_fb(render_device->drm_fd, &present_buf[1]);

    return window;
}

void window_destroy(window_t *window) {
    /* todo release device and context */
    image_release(window->surface);
    free(window);
}

int window_should_close(window_t *window) {
    return window->should_close;
}

void window_set_userdata(window_t *window, void *userdata) {
    window->userdata = userdata;
}

void *window_get_userdata(window_t *window) {
    return window->userdata;
}

static void present_surface(window_t *window) {
    image_t *surface = window->surface;
    struct buffer_object *buf = &present_buf[buffer_index];
    unsigned int off;
    int width = surface->width < buf->width ? surface->width : buf->width;
    int height = surface->height < buf->height ? surface->height : buf->height;

    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            off = buf->pitch * j + i * 4;
            int src_index = (j * width + i) * 4;
            unsigned char * src_pixel = &surface->ldr_buffer[src_index];
            *(uint32_t*)&buf->vaddr[off] = (src_pixel[2] << 16) | (src_pixel[1] << 8) | src_pixel[0];
        }
    }

    device_t *device = window->render_device;
    assert(!drmModeSetCrtc(device->drm_fd,device->crtc_p->crtc_id, buf->fb_id, 0, 0,
                           &device->connector_p->connector_id, 1, &device->crtc_p->mode));

    buffer_index ^= 1;
}

void window_draw_buffer(window_t *window, framebuffer_t *buffer) {
    private_blit_bgr(buffer, window->surface);
    present_surface(window);
}

/* input related functions */

static void handle_key_event(window_t *window, int virtual_key, char pressed) {
    return;
    /*
    KeySym *keysyms;
    KeySym keysym;
    keycode_t key;
    int dummy;

    keysyms = XGetKeyboardMapping(g_display, virtual_key, 1, &dummy);
    keysym = keysyms[0];
    XFree(keysyms);

    switch (keysym) {
        case XK_a:     key = KEY_A;     break;
        case XK_d:     key = KEY_D;     break;
        case XK_s:     key = KEY_S;     break;
        case XK_w:     key = KEY_W;     break;
        case XK_space: key = KEY_SPACE; break;
        default:       key = KEY_NUM;   break;
    }
    if (key < KEY_NUM) {
        window->keys[key] = pressed;
        if (window->callbacks.key_callback) {
            window->callbacks.key_callback(window, key, pressed);
        }
    }
    */
}

void input_poll_events(void) {

}

int input_key_pressed(window_t *window, keycode_t key) {
    assert(key >= 0 && key < KEY_NUM);
    return window->keys[key];
}

int input_button_pressed(window_t *window, button_t button) {
    assert(button >= 0 && button < BUTTON_NUM);
    return window->buttons[button];
}

void input_query_cursor(window_t *window, float *xpos, float *ypos) {

}

void input_set_callbacks(window_t *window, callbacks_t callbacks) {
    window->callbacks = callbacks;
}

/* misc platform functions */

static double get_native_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

float platform_get_time(void) {
    static double initial = -1;
    if (initial < 0) {
        initial = get_native_time();
    }
    return (float)(get_native_time() - initial);
}
