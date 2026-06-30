#include "screen_pc.h"
#include "ui_common.h"
#include "app_state.h"
#include <stdio.h>

#define PC_RING_SIZE 64
#define PC_RING_W   10
static const int PC_COLORS[3] = { COL_CYAN, COL_BLUE, COL_GOLD };
static const lv_coord_t PC_GAP = 80;
static const char *PC_NAMES[3] = { "CPU", "GPU", "RAM" };

static lv_obj_t *s_rings[3];
static lv_obj_t *s_labels[3];

static lv_obj_t *screen_pc_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_SCR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_recolor(title, true);
    lv_obj_set_style_text_font(title, &cn_font, 0);
    lv_label_set_text(title, "#FFD700 电脑占用#");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    for (int i = 0; i < 3; i++) {
        lv_coord_t x = (i - 1) * PC_GAP;
        s_rings[i] = ui_arc_create(scr, PC_RING_SIZE, PC_RING_W, lv_color_hex(PC_COLORS[i]));
        lv_obj_align(s_rings[i], LV_ALIGN_TOP_MID, x, 78);
        s_labels[i] = lv_label_create(scr);
        lv_label_set_recolor(s_labels[i], true);
        lv_obj_set_style_text_font(s_labels[i], &cn_font, 0);
        lv_obj_align(s_labels[i], LV_ALIGN_TOP_MID, x, 78 + PC_RING_SIZE + 6);
    }
    return scr;
}

static void screen_pc_update(void)
{
    const app_view_t *v = app_state_get();
    int vals[3] = { v->pc_cpu, v->pc_gpu, v->pc_mem };
    char lbl[24];
    for (int i = 0; i < 3; i++) {
        if (v->pc_ok && vals[i] >= 0) {
            int vv = vals[i];
            if (vv > 100) vv = 100;
            if (vv < 0)   vv = 0;
            lv_arc_set_value(s_rings[i], vv * 10);        /* 0-100 → 0-1000 */
            snprintf(lbl, sizeof(lbl), "#%06X %s %d%%#", PC_COLORS[i], PC_NAMES[i], vv);
        } else {
            lv_arc_set_value(s_rings[i], 0);
            snprintf(lbl, sizeof(lbl), "#666666 %s --#", PC_NAMES[i]);
        }
        lv_label_set_text(s_labels[i], lbl);
    }
}

const screen_t screen_pc = {
    .create     = screen_pc_create,
    .update     = screen_pc_update,
    .on_press   = NULL,
    .on_long    = NULL,
    .on_release = NULL,
    .on_rotate  = NULL,
};
