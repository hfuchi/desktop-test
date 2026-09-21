/* gcc -Wall test-cairo.c  -o mybar $(pkg-config --cflags --libs xcb cairo cairo-xcb) */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

#define BAR_HEIGHT 30
#define FONT_SIZE 13.0

// 32bit (ARGB) Visual を検索
static xcb_visualtype_t *get_argb32_visual_type(xcb_screen_t *screen) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        if (depth_iter.data->depth == 32) {
            xcb_visualtype_iterator_t vis_iter = xcb_depth_visuals_iterator(depth_iter.data);
            for (; vis_iter.rem; xcb_visualtype_next(&vis_iter)) {
                return vis_iter.data;
            }
        }
    }
    return NULL;
}

// 描画処理（透過テスト）
static void draw_bar(cairo_t *cr, uint32_t width, uint32_t height) {
    // 1. サーフェス全体を完全にクリア（透明化）する
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    // 2. 半透明背景を描画 (R, G, B, Alpha) -> Alpha: 0.5 (50% 透過)
    cairo_set_source_rgba(cr, 0.10, 0.11, 0.15, 0.50);
    cairo_paint(cr);

    // 3. テキストの描画 (不透明)
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, FONT_SIZE);
    cairo_set_source_rgb(cr, 0.90, 0.93, 0.98);

    cairo_move_to(cr, 20, 20);
    cairo_show_text(cr, "TRANSPARENCY TEST - MYBAR");
}

int main(void) {
    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) {
        fprintf(stderr, "[ERROR] XCB 接続エラー\n");
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    uint32_t width = screen->width_in_pixels;
    uint32_t height = BAR_HEIGHT;

    // ARGB32 Visual の取得
    xcb_visualtype_t *visual = get_argb32_visual_type(screen);
    if (!visual) {
        fprintf(stderr, "[ERROR] 32bit ARGB Visual が取得できませんでした。\n");
        xcb_disconnect(conn);
        return 1;
    }

    // 専用カラーマップの作成（アルファチャンネル使用時に必須）
    xcb_colormap_t colormap = xcb_generate_id(conn);
    xcb_create_colormap(conn, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visual->visual_id);

    // ウィンドウ作成
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
    uint32_t values[5] = {
        0,        // BACK_PIXEL
        0,        // BORDER_PIXEL
        1,        // OVERRIDE_REDIRECT (WMの管理外にする)
        XCB_EVENT_MASK_EXPOSURE,
        colormap  // COLORMAP
    };

    xcb_window_t win = xcb_generate_id(conn);
    xcb_create_window(
        conn,
        32,       // 深度: 32bit
        win,
        screen->root,
        0, 0, width, height, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        visual->visual_id,
        mask, values
    );

    xcb_map_window(conn, win);
    xcb_flush(conn);

    // Cairo サーフェス作成
    cairo_surface_t *surface = cairo_xcb_surface_create(conn, win, visual, width, height);
    cairo_t *cr = cairo_create(surface);

    // 初期描画
    draw_bar(cr, width, height);
    cairo_surface_flush(surface);
    xcb_flush(conn);

    // イベントループ
    xcb_generic_event_t *event;
    while ((event = xcb_wait_for_event(conn))) {
        uint8_t response_type = event->response_type & ~0x80;
        if (response_type == XCB_EXPOSE) {
            draw_bar(cr, width, height);
            cairo_surface_flush(surface);
            xcb_flush(conn);
        }
        free(event);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    xcb_destroy_window(conn, win);
    xcb_disconnect(conn);

    return 0;
}
