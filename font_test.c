#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>      // UTF-8 変換に必須
#include <wchar.h>     // ワイド文字操作に必須
#include <xcb/xcb.h>
#include <xcb/render.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define BAR_HEIGHT 30

int main(void) {
    // 1. ロケールを UTF-8 に設定 (mbstowcs を正しく機能させるため)
    setlocale(LC_CTYPE, "");

    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) return 1;

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    uint32_t width = screen->width_in_pixels;

    FT_Library ft_lib;
    FT_Init_FreeType(&ft_lib);

    FT_Face face;
    const char *font_path = "/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf";
    if (FT_New_Face(ft_lib, font_path, 0, &face) != 0) return 1;

    FT_Set_Pixel_Sizes(face, 0, 16);

    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    uint32_t values[3] = { screen->black_pixel, 1, XCB_EVENT_MASK_EXPOSURE };

    xcb_window_t win = xcb_generate_id(conn);
    xcb_create_window(
        conn, XCB_COPY_FROM_PARENT, win, screen->root,
        0, 0, width, BAR_HEIGHT, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual, mask, values
    );

    xcb_map_window(conn, win);

    xcb_render_query_pict_formats_cookie_t cookie = xcb_render_query_pict_formats(conn);
    xcb_render_query_pict_formats_reply_t *reply = xcb_render_query_pict_formats_reply(conn, cookie, NULL);

    xcb_render_pictformat_t win_format = XCB_NONE;
    xcb_render_pictformat_t format_32 = XCB_NONE;

    xcb_render_pictforminfo_iterator_t iter = xcb_render_query_pict_formats_formats_iterator(reply);
    for (; iter.rem; xcb_render_pictforminfo_next(&iter)) {
        if (iter.data->depth == 24 && win_format == XCB_NONE) win_format = iter.data->id;
        if (iter.data->depth == 32 && format_32 == XCB_NONE) format_32 = iter.data->id;
    }
    free(reply);

    xcb_render_picture_t target = xcb_generate_id(conn);
    xcb_render_create_picture(conn, target, win, win_format, 0, NULL);

    xcb_flush(conn);

    xcb_generic_event_t *ev;
    while ((ev = xcb_wait_for_event(conn))) {
        uint8_t type = ev->response_type & ~0x80;

        if (type == XCB_EXPOSE) {
            // 背景クリア
            xcb_rectangle_t bg_rect = { 0, 0, width, BAR_HEIGHT };
            xcb_render_color_t bg_color = { 0x2222, 0x2222, 0x2222, 0xffff };
            xcb_render_fill_rectangles(conn, XCB_RENDER_PICT_OP_SRC, target, bg_color, 1, &bg_rect);

            // 赤色テスト領域
            xcb_rectangle_t test_rect = { 10, 5, 20, 20 };
            xcb_render_color_t red_color = { 0xffff, 0x0000, 0x0000, 0xffff };
            xcb_render_fill_rectangles(conn, XCB_RENDER_PICT_OP_SRC, target, red_color, 1, &test_rect);

            // 【ポイント1】 日本語 UTF-8 文字列を用意
            const char *utf8_text = "日本語テスト 123";
            
            // 【ポイント2】 wchar_t（ワイド文字列）へ変換
            size_t len = mbstowcs(NULL, utf8_text, 0);
            wchar_t *wtext = malloc((len + 1) * sizeof(wchar_t));
            mbstowcs(wtext, utf8_text, len + 1);

            int cursor_x = 40;
            int baseline_y = 20;

            // 【ポイント3】 wchar_t 配列を 1 文字（Code Point）ずつ走査
            for (size_t i = 0; i < len; i++) {
                wchar_t wc = wtext[i];

                // ワイド文字（Unicode Code Point）を直接渡す
                FT_UInt idx = FT_Get_Char_Index(face, wc);
                if (idx == 0 || FT_Load_Glyph(face, idx, FT_LOAD_RENDER) != 0) continue;

                FT_GlyphSlot slot = face->glyph;
                int w = slot->bitmap.width;
                int h = slot->bitmap.rows;

                if (w > 0 && h > 0) {
                    uint32_t *buf = malloc(w * h * sizeof(uint32_t));
                    for (int r = 0; r < h; r++) {
                        for (int c = 0; c < w; c++) {
                            uint8_t a = slot->bitmap.buffer[r * slot->bitmap.pitch + c];
                            // Premultiplied ARGB (白文字)
                            buf[r * w + c] = ((uint32_t)a << 24) |
                                             ((uint32_t)a << 16) |
                                             ((uint32_t)a << 8)  |
                                              (uint32_t)a;
                        }
                    }

                    xcb_pixmap_t pm = xcb_generate_id(conn);
                    xcb_create_pixmap(conn, 32, pm, screen->root, w, h);

                    xcb_gcontext_t gc = xcb_generate_id(conn);
                    xcb_create_gc(conn, gc, pm, 0, NULL);
                    xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pm, gc, w, h, 0, 0, 0, 32, w * h * 4, (const uint8_t *)buf);
                    xcb_free_gc(conn, gc);
                    free(buf);

                    xcb_render_picture_t glyph_pic = xcb_generate_id(conn);
                    xcb_render_create_picture(conn, glyph_pic, pm, format_32, 0, NULL);

                    int render_x = cursor_x + slot->bitmap_left;
                    int render_y = baseline_y - slot->bitmap_top;

                    xcb_render_composite(conn, XCB_RENDER_PICT_OP_OVER, glyph_pic, XCB_NONE, target,
                                         0, 0, 0, 0, render_x, render_y, w, h);

                    xcb_render_free_picture(conn, glyph_pic);
                    xcb_free_pixmap(conn, pm);
                }
                cursor_x += (slot->advance.x >> 6);
            }

            free(wtext);
            xcb_flush(conn);
        }
        free(ev);
    }

    return 0;
}
