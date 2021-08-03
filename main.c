/********************************************************************
**  xsmon, https://github.com/xsmon/xsmon
**  Copyright (C) 2021 Sergey Vlasov <sergey@vlasov.me>
**
**  Licensed under MIT
**  https://github.com/xsmon/xsmon/blob/master/LICENSE.MIT
**
*********************************************************************/

#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define VERSION "0.1"

char *g_progname = NULL;

struct options_
{
    uint32_t bg_color;
    uint32_t cpu_color;
    uint32_t mem_color;
    uint32_t alert_color;
    size_t cpu_alert_threshold;
    size_t mem_alert_threshold;
} g_options;

typedef struct buffer_
{
    double *data;
    size_t capacity;
    size_t head;
} buffer_t;

typedef struct icon_
{
    buffer_t buffer;
    xcb_window_t window;
    xcb_gcontext_t gc;
    uint16_t width;
    uint16_t height;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t alert_fg_color;
    uint32_t alert_bg_color;
    size_t alert_theshold;
} icon_t;

int round_(double d)
{
    return (int)(d + 0.5);
}

uint32_t get_color(const char *hex)
{
    return (uint32_t)strtol(hex + 1, NULL, 16);
}

uint32_t blend(uint32_t bg_rgb, uint32_t fg_rgba)
{
    unsigned int bg_r = ((bg_rgb >> 16) & 0xFF);
    unsigned int bg_g = ((bg_rgb >> 8) & 0xFF);
    unsigned int bg_b = (bg_rgb & 0xFF);

    unsigned int fg_r = ((fg_rgba >> 24) & 0xFF);
    unsigned int fg_g = ((fg_rgba >> 16) & 0xFF);
    unsigned int fg_b = ((fg_rgba >> 8) & 0xFF);
    unsigned int fg_a = (fg_rgba & 0xFF);

    unsigned int res_r = (fg_r * (fg_a + 1) + bg_r * (0xFF - fg_a)) >> 8;
    unsigned int res_g = (fg_g * (fg_a + 1) + bg_g * (0xFF - fg_a)) >> 8;
    unsigned int res_b = (fg_b * (fg_a + 1) + bg_b * (0xFF - fg_a)) >> 8;

    return (res_r << 16) + (res_g << 8) + res_b;
}

void buffer_init(buffer_t *buffer, size_t capacity)
{
    if (buffer->capacity > 0) {
        free(buffer->data);
    }

    buffer->data = NULL;
    if (capacity > 0) {
        buffer->data = (double *)calloc(capacity, sizeof(double));
    }

    buffer->head = 0;
    buffer->capacity = capacity;
}

void buffer_push(buffer_t *buffer, double value)
{
    if (buffer->capacity == 0) {
        return;
    }

    buffer->data[buffer->head] = value;
    buffer->head = (buffer->head + 1) % buffer->capacity;
}

void print_usage()
{
    printf("Usage: %s [options]\n", g_progname);
    printf("\n");
    printf(
        "  --bg_color RGB"
        "       background color (default: '#%x')\n",
        g_options.bg_color);
    printf(
        "  --cpu_color RGB"
        "      cpu color (default: '#%x')\n",
        g_options.cpu_color);
    printf(
        "  --mem_color RGB"
        "      memory color (default: '#%x')\n",
        g_options.mem_color);
    printf(
        "  --alert_color RGBA"
        "   alert color (default: '#%x')\n",
        g_options.alert_color);
    printf(
        "  --cpu_alert NUM"
        "      cpu alert threshold percentage (default: %zu)\n",
        g_options.cpu_alert_threshold);
    printf(
        "  --mem_alert NUM"
        "      memory alert threshold percentage (default: %zu)\n",
        g_options.mem_alert_threshold);
    printf("  -v, --version        version number\n");
    printf("  -h, --help           print this message\n");
}

void print_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    fprintf(stderr, "%s: ", g_progname);
    vfprintf(stderr, format, args);

    va_end(args);
}

void parse_args(int argc, char *argv[])
{
    g_options.bg_color = get_color("#101114");
    g_options.cpu_color = get_color("#8AE234");
    g_options.mem_color = get_color("#AD7FA8");
    g_options.alert_color = get_color("#FF0000CC");
    g_options.cpu_alert_threshold = 95;
    g_options.mem_alert_threshold = 80;

    bool error = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--bg_color") == 0) {
            ++i;
            g_options.bg_color = get_color(argv[i]);
        } else if (strcmp(argv[i], "--cpu_color") == 0) {
            ++i;
            g_options.cpu_color = get_color(argv[i]);
        } else if (strcmp(argv[i], "--mem_color") == 0) {
            ++i;
            g_options.mem_color = get_color(argv[i]);
        } else if (strcmp(argv[i], "--alert_color") == 0) {
            ++i;
            g_options.alert_color = get_color(argv[i]);
        } else if (strcmp(argv[i], "--cpu_alert") == 0) {
            ++i;
            g_options.cpu_alert_threshold = (size_t)strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "--mem_alert") == 0) {
            ++i;
            g_options.mem_alert_threshold = (size_t)strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_usage();
            exit(EXIT_SUCCESS);
        } else if (strcmp(argv[i], "-v") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            printf("%s\n", VERSION);
            exit(EXIT_SUCCESS);
        } else if (argv[i][0] == '-') {  // check if it's an option
            print_error("Wrong option: '%s'\n", argv[i]);
            error = true;
            break;
        } else {
            print_error("Bad argument: '%s'\n", argv[i]);
            error = true;
            break;
        }
    }

    if (!error) {
        return;
    }

    fprintf(stderr, "Try '-h' for help\n");
    exit(EXIT_FAILURE);
}

xcb_atom_t get_atom(xcb_connection_t *connection, const char *name)
{
    xcb_atom_t atom;
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(connection, cookie, NULL);
    if (!reply) {
        print_error("xcb_intern_atom failed\n");
        exit(EXIT_FAILURE);
    }

    atom = reply->atom;
    free(reply);
    return atom;
}

void dock_window(xcb_connection_t *connection, int preferred_screen,
                 xcb_window_t window)
{
    xcb_window_t selection_owner_window;
    {
        char atom_name[21];
        snprintf(atom_name, sizeof atom_name, "_NET_SYSTEM_TRAY_S%d",
                 preferred_screen);
        xcb_atom_t net_system_tray_sdefault = get_atom(connection, atom_name);
        xcb_get_selection_owner_cookie_t cookie =
            xcb_get_selection_owner(connection, net_system_tray_sdefault);
        xcb_get_selection_owner_reply_t *reply =
            xcb_get_selection_owner_reply(connection, cookie, NULL);
        if (!reply) {
            print_error("xcb_get_selection_owner_reply failed\n");
            exit(EXIT_FAILURE);
        }
        selection_owner_window = reply->owner;
        free(reply);
    }

    {
        xcb_client_message_event_t event;
        memset(&event, 0, sizeof event);
        event.response_type = XCB_CLIENT_MESSAGE;
        event.window = selection_owner_window;
        event.type = get_atom(connection, "_NET_SYSTEM_TRAY_OPCODE");
        event.format = 32;
        event.data.data32[0] = XCB_CURRENT_TIME;
        event.data.data32[1] = 0;  // request to dock
        event.data.data32[2] = window;
        event.data.data32[3] = 0;
        event.data.data32[4] = 0;
        xcb_send_event(connection, 0, selection_owner_window,
                       XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
    }

    xcb_flush(connection);
}

void create_icon(xcb_connection_t *connection, const char *name,
                 uint32_t fg_color, size_t alert_theshold, icon_t *icon)
{
    xcb_screen_t *screen =
        xcb_setup_roots_iterator(xcb_get_setup(connection)).data;

    icon->buffer.data = NULL;
    icon->buffer.capacity = 0;
    icon->buffer.head = 0;

    icon->width = 48;
    icon->height = 48;

    icon->fg_color = fg_color;
    icon->bg_color = g_options.bg_color;
    icon->alert_fg_color = blend(icon->fg_color, g_options.alert_color);
    icon->alert_bg_color = blend(icon->bg_color, g_options.alert_color);
    icon->alert_theshold = alert_theshold;

    icon->window = xcb_generate_id(connection);
    xcb_create_window(
        connection, XCB_COPY_FROM_PARENT, icon->window, screen->root, 0, 0,
        icon->width, icon->height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
        (const uint32_t[]){screen->black_pixel, XCB_EVENT_MASK_EXPOSURE});

    icon->gc = xcb_generate_id(connection);
    xcb_create_gc(connection, icon->gc, icon->window, XCB_GC_FOREGROUND,
                  (uint32_t[]){screen->black_pixel});

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, icon->window,
                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        (uint32_t)strlen(name), name);

    xcb_flush(connection);
}

void draw_icon(xcb_connection_t *connection, bool tick, icon_t icon)
{
    if (icon.buffer.capacity == 0) {
        return;
    }

    uint32_t fg_color = icon.fg_color;
    uint32_t bg_color = icon.bg_color;
    if (tick &&
        icon.buffer.data[icon.buffer.head - 1] > (double)icon.alert_theshold) {
        fg_color = icon.alert_fg_color;
        bg_color = icon.alert_bg_color;
    }

    xcb_change_gc(connection, icon.gc, XCB_GC_FOREGROUND,
                  (uint32_t[]){bg_color});
    xcb_rectangle_t rect = {0, 0, icon.width, icon.height};
    xcb_poly_fill_rectangle(connection, icon.window, icon.gc, 1, &rect);

    xcb_change_gc(connection, icon.gc, XCB_GC_FOREGROUND,
                  (uint32_t[]){fg_color});
    for (size_t i = 0; i < icon.buffer.capacity; ++i) {
        double value =
            icon.buffer.data[(icon.buffer.head + i) % icon.buffer.capacity];
        int16_t y = (int16_t)round_((value / 100) * icon.height);
        xcb_point_t points[] = {{(int16_t)i, (int16_t)icon.height},
                                {(int16_t)i, (int16_t)(icon.height - y)}};
        xcb_poly_line(connection, XCB_COORD_MODE_ORIGIN, icon.window, icon.gc,
                      2, points);
    }

    xcb_flush(connection);
}

void read_cpu(buffer_t *buffer)
{
    size_t user;
    size_t nice;
    size_t system;
    size_t idle;

    FILE *fp = fopen("/proc/stat", "r");
    int res = fscanf(fp, "%*s %zu %zu %zu %zu", &user, &nice, &system, &idle);
    if (res == EOF) {
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    static size_t prev_work = 0;
    static size_t prev_total = 0;

    size_t work = user + nice + system;
    size_t total = work + idle;

    if (prev_total > 0) {
        buffer_push(buffer, 100 * (double)(work - prev_work) /
                                (double)(total - prev_total));
    }

    prev_work = work;
    prev_total = total;
}

void read_mem(buffer_t *buffer)
{
    size_t total = 0;
    size_t available = 0;

    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        char label[32];
        size_t value;

        while (true) {
            int res = fscanf(fp, "%s %zu%*[^\n]", label, &value);
            if (res == EOF) {
                break;
            }

            if (strcmp(label, "MemTotal:") == 0) {
                total = value;
            } else if (strcmp(label, "MemAvailable:") == 0) {
                available = value;
            }

            if (total != 0 && available != 0) {
                break;
            }
        }
    }
    fclose(fp);

    if (total == 0 || available == 0) {
        exit(EXIT_FAILURE);
    }

    buffer_push(buffer, 100 * (double)(total - available) / (double)(total));
}

int main(int argc, char *argv[])
{
    g_progname = basename(argv[0]);

    parse_args(argc, argv);

    icon_t cpu_icon;
    memset(&cpu_icon, 0, sizeof(icon_t));

    icon_t mem_icon;
    memset(&mem_icon, 0, sizeof(icon_t));

    int preferred_screen;
    xcb_connection_t *connection = xcb_connect(NULL, &preferred_screen);

    create_icon(connection, "CPU", g_options.cpu_color,
                g_options.cpu_alert_threshold, &cpu_icon);
    create_icon(connection, "Memory", g_options.mem_color,
                g_options.mem_alert_threshold, &mem_icon);

    dock_window(connection, preferred_screen, cpu_icon.window);
    dock_window(connection, preferred_screen, mem_icon.window);

    bool tick = false;
    xcb_generic_event_t *event;
    while (true) {
        while ((event = xcb_poll_for_event(connection))) {
            switch (event->response_type & ~0x80) {
                case XCB_EXPOSE: {
                    xcb_expose_event_t *ev = (xcb_expose_event_t *)event;
                    icon_t *icon = NULL;
                    if (ev->window == cpu_icon.window) {
                        icon = &cpu_icon;
                    } else if (ev->window == mem_icon.window) {
                        icon = &mem_icon;
                    } else {
                        print_error("unknown window\n");
                        exit(EXIT_FAILURE);
                    }

                    dock_window(connection, preferred_screen, icon->window);

                    xcb_get_geometry_cookie_t cookie =
                        xcb_get_geometry(connection, ev->window);
                    xcb_get_geometry_reply_t *reply =
                        xcb_get_geometry_reply(connection, cookie, NULL);
                    if (icon->buffer.capacity != reply->width) {
                        buffer_init(&icon->buffer, reply->width);
                    }
                    icon->width = reply->width;
                    icon->height = reply->height;
                    free(reply);

                    break;
                }
                default:
                    break;
            }
            free(event);
            xcb_flush(connection);
        }

        read_cpu(&cpu_icon.buffer);
        draw_icon(connection, tick, cpu_icon);

        read_mem(&mem_icon.buffer);
        draw_icon(connection, tick, mem_icon);

        sleep(1);
        tick = !tick;
    }

    // never reached:
    return EXIT_FAILURE;
}
