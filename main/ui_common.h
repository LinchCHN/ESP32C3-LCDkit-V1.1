#pragma once

#include "lvgl.h"

/* 中文字体(16px 黑体子集,见 cn_font.c) */
extern lv_font_t cn_font;

/* —— 配色 —— */
#define COL_GOLD    0xFFD700   /* 外圈:工作进度(圆满) */
#define COL_CYAN    0x00E0C0   /* 中圈:番茄钟 */
#define COL_BLUE    0x7FB2FF   /* 内圈:本月已赚几天 */
#define COL_SCR_BG  0x101820   /* 界面背景 */
#define COL_ARC_BG  0x202838   /* 圈底色 */

/* —— 圆环几何(肥圈 + 三圈无缝贴合) ——
 * LVGL arc 的 size 是外径,弧占最外侧 W 像素;故相邻圈"外径=内径"即贴合:
 *   外圈外径 220 -> 内径 220-2W = 184 = 中圈外径
 *   中圈外径 184 -> 内径 184-2W = 148 = 内圈外径
 * 中央留白直径 = 148-2W = 112px,够放 4 行 16px 中文。*/
#define ARC_W         18
#define ARC_SIZE_OUT  220
#define ARC_SIZE_MID  (ARC_SIZE_OUT - 2 * ARC_W)   /* 184 */
#define ARC_SIZE_IN   (ARC_SIZE_OUT - 4 * ARC_W)   /* 148 */

/* 创建一个圆环:size=外径,width=弧宽,color=进度弧颜色。末端圆润。 */
lv_obj_t *ui_arc_create(lv_obj_t *parent, lv_coord_t size,
                        lv_coord_t width, lv_color_t color);
