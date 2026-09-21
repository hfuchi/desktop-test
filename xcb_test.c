#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xcb/xcb.h>

int main(void) {
    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) {
        fprintf(stderr, "XCB 接続エラー\n");
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;

    xcb_window_t win = xcb_generate_id(conn);

    // 単純に赤色（または目立つ色）のグラフィックコンテキストを作成
    // override_redirect = 1 で純粋に画面の上部に配置
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    uint32_t values[3] = {
        screen->white_pixel, // 白背景でテスト
        1,                   // override_redirect = 1
        XCB_EVENT_MASK_EXPOSURE
    };

    xcb_create_window(
        conn,
        XCB_COPY_FROM_PARENT,
        win,
        screen->root,
        0, 0, screen->width_in_pixels, 40, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        mask, values
    );

    // タイトル付与 (マップ前)
    const char *title = "mybar_test";
    xcb_change_property(
        conn, XCB_PROP_MODE_REPLACE, win,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(title), title
    );

    xcb_map_window(conn, win);
    
    // 配置を最前面へ
    uint32_t config_values[] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, config_values);
    
    xcb_flush(conn);

    printf("[TEST] マップ完了。ウィンドウID: 0x%08x\n", win);
    printf("[TEST] 10秒間表示を維持します...\n");

    // 簡易イベントループ
    xcb_generic_event_t *ev;
    int count = 0;
    while (count < 100) { // 約10秒
        while ((ev = xcb_poll_for_event(conn))) {
            free(ev);
        }
        usleep(100000); // 100ms
        count++;
    }

    xcb_destroy_window(conn, win);
    xcb_disconnect(conn);
    return 0;
}
