/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <math.h>
#include <string.h>

#define VIEW_DATA "external-dimming-view"
#define BACKLIGHT_ROOT "/sys/class/backlight"
#define POLL_MS 120
#define MONITOR_POLL_MS 1500

typedef struct {
    int size;
    unsigned short *red, *green, *blue;
} GammaBase;

typedef struct {
    RROutput output;
    RRCrtc crtc;
    GammaBase base;
    gboolean ready;
} Target;

static Display *display;
static GHashTable *views;
static guint brightness_timer, monitor_timer;
static Target target;
static double last_brightness = -1.0;

static void gamma_clear(GammaBase *gamma) {
    g_free(gamma->red);
    g_free(gamma->green);
    g_free(gamma->blue);
    memset(gamma, 0, sizeof(*gamma));
}

static gboolean output_is_external(const char *name) {
    static const char *internal_prefixes[] = {"eDP", "LVDS", "DSI", "LCD", NULL};
    if (name == NULL || *name == '\0')
        return FALSE;
    for (guint i = 0; internal_prefixes[i] != NULL; ++i) {
        if (g_str_has_prefix(name, internal_prefixes[i]))
            return FALSE;
    }
    return TRUE;
}

static gboolean gamma_capture(RRCrtc crtc, GammaBase *base) {
    int size = XRRGetCrtcGammaSize(display, crtc);
    if (size <= 0)
        return FALSE;
    XRRCrtcGamma *current = XRRGetCrtcGamma(display, crtc);
    if (current == NULL)
        return FALSE;
    gamma_clear(base);
    base->size = size;
    base->red = g_memdup2(current->red, sizeof(unsigned short) * size);
    base->green = g_memdup2(current->green, sizeof(unsigned short) * size);
    base->blue = g_memdup2(current->blue, sizeof(unsigned short) * size);
    XRRFreeGamma(current);
    return TRUE;
}

static Atom gamma_state_atom(void) {
    return XInternAtom(display, "_XFCE_TWEAKS_EXTERNAL_DIMMING_BASE", False);
}

static gboolean gamma_state_load(RROutput output, int expected_size, GammaBase *base) {
    Atom atom = gamma_state_atom(), actual_type = None;
    int actual_format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *property = NULL;
    int status = XRRGetOutputProperty(display, output, atom, 0, 65536, False,
                                      False, AnyPropertyType, &actual_type,
                                      &actual_format, &count, &remaining,
                                      &property);
    gboolean ok = FALSE;
    const gsize header_size = 8;
    if (status == Success && property != NULL && actual_format == 8 && count >= header_size &&
        memcmp(property, "XTD1", 4) == 0) {
        guint32 size = 0;
        memcpy(&size, property + 4, sizeof(size));
        gsize expected = header_size + (gsize)size * sizeof(unsigned short) * 3;
        if (size == (guint32)expected_size && count == expected && remaining == 0) {
            gamma_clear(base);
            base->size = size;
            base->red = g_new(unsigned short, size);
            base->green = g_new(unsigned short, size);
            base->blue = g_new(unsigned short, size);
            const unsigned char *data = property + header_size;
            memcpy(base->red, data, size * sizeof(unsigned short));
            data += size * sizeof(unsigned short);
            memcpy(base->green, data, size * sizeof(unsigned short));
            data += size * sizeof(unsigned short);
            memcpy(base->blue, data, size * sizeof(unsigned short));
            ok = TRUE;
        }
    }
    if (property != NULL)
        XFree(property);
    return ok;
}

static void gamma_state_save(RROutput output, const GammaBase *base) {
    if (output == None || base->size <= 0)
        return;
    const gsize header_size = 8;
    gsize count = header_size + (gsize)base->size * sizeof(unsigned short) * 3;
    unsigned char *data = g_malloc0(count);
    memcpy(data, "XTD1", 4);
    guint32 size = base->size;
    memcpy(data + 4, &size, sizeof(size));
    unsigned char *cursor = data + header_size;
    memcpy(cursor, base->red, size * sizeof(unsigned short));
    cursor += size * sizeof(unsigned short);
    memcpy(cursor, base->green, size * sizeof(unsigned short));
    cursor += size * sizeof(unsigned short);
    memcpy(cursor, base->blue, size * sizeof(unsigned short));
    XRRChangeOutputProperty(display, output, gamma_state_atom(), XA_INTEGER, 8,
                            PropModeReplace, data, count);
    XSync(display, False);
    g_free(data);
}

static void gamma_state_clear(RROutput output) {
    if (output != None) {
        XRRDeleteOutputProperty(display, output, gamma_state_atom());
        XSync(display, False);
    }
}

static gboolean gamma_set(RRCrtc crtc, double factor) {
    if (crtc == None || target.base.size <= 0)
        return FALSE;
    XRRCrtcGamma *adjusted = XRRAllocGamma(target.base.size);
    if (adjusted == NULL)
        return FALSE;
    for (int i = 0; i < target.base.size; ++i) {
        adjusted->red[i] = (unsigned short)lrint(target.base.red[i] * factor);
        adjusted->green[i] = (unsigned short)lrint(target.base.green[i] * factor);
        adjusted->blue[i] = (unsigned short)lrint(target.base.blue[i] * factor);
    }
    XRRSetCrtcGamma(display, crtc, adjusted);
    XRRFreeGamma(adjusted);
    XFlush(display);
    return TRUE;
}

static gboolean gamma_apply(double factor) {
    return target.ready && gamma_set(target.crtc, factor);
}

static gboolean target_is_attached_to(RRCrtc crtc) {
    if (display == NULL || target.output == None || crtc == None)
        return FALSE;
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(
        display, DefaultRootWindow(display));
    if (resources == NULL)
        return FALSE;
    gboolean attached = FALSE;
    for (int i = 0; i < resources->noutput; ++i) {
        if (resources->outputs[i] != target.output)
            continue;
        XRROutputInfo *info = XRRGetOutputInfo(display, resources, target.output);
        attached = info != NULL && info->connection == RR_Connected && info->crtc == crtc;
        if (info != NULL)
            XRRFreeOutputInfo(info);
        break;
    }
    XRRFreeScreenResources(resources);
    return attached;
}

static gboolean read_laptop_brightness(double *value) {
    GError *error = NULL;
    GDir *directory = g_dir_open(BACKLIGHT_ROOT, 0, &error);
    if (directory == NULL) {
        g_clear_error(&error);
        return FALSE;
    }
    const char *entry, *chosen = NULL;
    while ((entry = g_dir_read_name(directory)) != NULL) {
        if (g_str_has_prefix(entry, "intel_backlight")) {
            chosen = entry;
            break;
        }
        if (chosen == NULL)
            chosen = entry;
    }
    char *current_path = chosen ? g_build_filename(BACKLIGHT_ROOT, chosen, "brightness", NULL) : NULL;
    char *maximum_path = chosen ? g_build_filename(BACKLIGHT_ROOT, chosen, "max_brightness", NULL) : NULL;
    g_dir_close(directory);
    if (current_path == NULL)
        return FALSE;
    char *current_text = NULL, *maximum_text = NULL;
    gboolean ok = g_file_get_contents(current_path, &current_text, NULL, NULL) &&
                  g_file_get_contents(maximum_path, &maximum_text, NULL, NULL);
    g_free(current_path);
    g_free(maximum_path);
    if (!ok) {
        g_free(current_text);
        g_free(maximum_text);
        return FALSE;
    }
    char *end_current = NULL, *end_maximum = NULL;
    double current = g_ascii_strtod(current_text, &end_current);
    double maximum = g_ascii_strtod(maximum_text, &end_maximum);
    ok = end_current != current_text && end_maximum != maximum_text && maximum > 0;
    if (ok)
        *value = CLAMP(current / maximum, 0.0, 1.0);
    g_free(current_text);
    g_free(maximum_text);
    return ok;
}

static gboolean find_target(gboolean capture_if_new) {
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(
        display, DefaultRootWindow(display));
    if (resources == NULL)
        return FALSE;
    RROutput found_output = None;
    RRCrtc found_crtc = None;
    for (int i = 0; i < resources->noutput; ++i) {
        XRROutputInfo *info = XRRGetOutputInfo(display, resources, resources->outputs[i]);
        if (info != NULL && info->connection == RR_Connected && info->crtc != None &&
            output_is_external(info->name)) {
            found_output = resources->outputs[i];
            found_crtc = info->crtc;
            if (info->name != NULL)
                g_debug("External dimming found external display on %.*s", (int)info->nameLen, info->name);
            XRRFreeOutputInfo(info);
            break;
        }
        if (info != NULL)
            XRRFreeOutputInfo(info);
    }
    XRRFreeScreenResources(resources);
    if (found_output == None || found_crtc == None) {
        target.ready = FALSE;
        return FALSE;
    }
    if (!target.ready || target.output != found_output || target.crtc != found_crtc) {
        target.output = found_output;
        target.crtc = found_crtc;
        int gamma_size = XRRGetCrtcGammaSize(display, found_crtc);
        if (gamma_size <= 0) {
            target.ready = FALSE;
            return FALSE;
        }
        if (target.base.size != gamma_size) {
            gamma_clear(&target.base);
            if (!gamma_state_load(found_output, gamma_size, &target.base)) {
                if (!gamma_capture(found_crtc, &target.base)) {
                    target.ready = FALSE;
                    return FALSE;
                }
                gamma_state_save(found_output, &target.base);
            }
        } else if (capture_if_new && target.base.size == 0) {
            if (!gamma_capture(found_crtc, &target.base)) {
                target.ready = FALSE;
                return FALSE;
            }
            gamma_state_save(found_output, &target.base);
        }
        target.ready = TRUE;
        last_brightness = -1.0;
        g_message("External Dimming: controlling external display with %d gamma entries",
                  target.base.size);
    }
    return TRUE;
}

static gboolean update_brightness(gboolean force) {
    double brightness;
    if (!read_laptop_brightness(&brightness))
        return FALSE;
    if (!target.ready && !find_target(TRUE))
        return FALSE;
    if (!force && fabs(brightness - last_brightness) < 0.00001)
        return TRUE;
    if (gamma_apply(brightness)) {
        last_brightness = brightness;
        return TRUE;
    }
    return FALSE;
}

static gboolean brightness_tick(gpointer unused) {
    (void)unused;
    update_brightness(FALSE);
    return G_SOURCE_CONTINUE;
}

static gboolean monitor_tick(gpointer unused) {
    (void)unused;
    if (find_target(FALSE))
        update_brightness(TRUE);
    else
        last_brightness = -1.0;
    return G_SOURCE_CONTINUE;
}

static void stop_dimming(void) {
    if (target.base.size > 0 && target_is_attached_to(target.crtc))
        gamma_set(target.crtc, 1.0);
    if (display != NULL)
        gamma_state_clear(target.output);
    target.ready = FALSE;
    target.output = None;
    target.crtc = None;
    gamma_clear(&target.base);
    last_brightness = -1.0;
    if (display != NULL) {
        XCloseDisplay(display);
        display = NULL;
    }
    if (brightness_timer) {
        g_source_remove(brightness_timer);
        brightness_timer = 0;
    }
    if (monitor_timer) {
        g_source_remove(monitor_timer);
        monitor_timer = 0;
    }
}

static void view_gone(gpointer unused, GObject *object) {
    (void)unused;
    if (views != NULL && g_hash_table_remove(views, object) &&
        g_hash_table_size(views) == 0)
        stop_dimming();
}

static void attach_view(GtkWidget *view, const char *monitor_id) {
    (void)monitor_id;
    if (g_object_get_data(G_OBJECT(view), VIEW_DATA) != NULL)
        return;
    if (views == NULL)
        views = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_hash_table_add(views, view);
    g_object_set_data(G_OBJECT(view), VIEW_DATA, GINT_TO_POINTER(1));
    g_object_weak_ref(G_OBJECT(view), view_gone, NULL);
    if (display == NULL) {
        display = XOpenDisplay(NULL);
        if (display == NULL) {
            g_warning("External Dimming: cannot connect to the X11 display");
            return;
        }
        update_brightness(TRUE);
        brightness_timer = g_timeout_add(POLL_MS, brightness_tick, NULL);
        monitor_timer = g_timeout_add(MONITOR_POLL_MS, monitor_tick, NULL);
    }
}

static void detach_view(GtkWidget *view) {
    if (views == NULL || !g_hash_table_remove(views, view))
        return;
    g_object_weak_unref(G_OBJECT(view), view_gone, NULL);
    g_object_set_data(G_OBJECT(view), VIEW_DATA, NULL);
    if (g_hash_table_size(views) == 0)
        stop_dimming();
}

static const XtTweak tweak = {
    .abi_version = XT_ABI_VERSION,
    .struct_size = sizeof(XtTweak),
    .id = "external-dimming",
    .name = "External Display Dimming",
    .attach_view = attach_view,
    .detach_view = detach_view,
};

G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost *api) {
    if (api == NULL || api->abi_version != XT_ABI_VERSION ||
        api->struct_size < sizeof(XtHost))
        return NULL;
    return &tweak;
}
