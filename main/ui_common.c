#include "ui_common.h"

/* 创建一个圆环(arc),三个圈共用。
 * size=外径, width=弧宽, color=进度弧颜色;进度末端圆润,底弧平头。 */
lv_obj_t *ui_arc_create(lv_obj_t *parent, lv_coord_t size,
                        lv_coord_t width, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);              /* 起点转到正上方(12 点) */
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);             /* 高分辨率,毫秒级平滑 */
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);       /* 去掉旋钮圆点 */
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_ARC_BG), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true,  LV_PART_INDICATOR);  /* 进度末端圆润 */
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}
