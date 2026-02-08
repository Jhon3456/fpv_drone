#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;
uint32_t active_theme_index = 0;

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 320, 240);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_EDITED);
    {
        lv_obj_t *parent_obj = obj;
        {
            // panel1
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.panel1 = obj;
            lv_obj_set_pos(obj, 0, 4);
            lv_obj_set_size(obj, 330, 29);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff9ae9bd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // titulo
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.titulo = obj;
            lv_obj_set_pos(obj, 119, 9);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text(obj, "Mando RC  ");
        }
        {
            // panel2
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.panel2 = obj;
            lv_obj_set_pos(obj, 3, 33);
            lv_obj_set_size(obj, 314, 193);
            lv_obj_set_style_border_color(obj, lv_color_hex(0xfffdfdfd), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // arc1
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc1 = obj;
            lv_obj_set_pos(obj, 22, 41);
            lv_obj_set_size(obj, 63, 66);
            lv_arc_set_value(obj, 0);
            lv_obj_set_style_arc_width(obj, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff48f38d), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_width(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff84c0f5), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xffe52424), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 32767, LV_PART_KNOB | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff3e55c6), LV_PART_KNOB | LV_STATE_DEFAULT);
        }
        {
            // arc2
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc2 = obj;
            lv_obj_set_pos(obj, 97, 41);
            lv_obj_set_size(obj, 63, 66);
            lv_arc_set_range(obj, -30, 30);
            lv_arc_set_value(obj, 0);
            lv_obj_set_style_arc_width(obj, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xffb762fd), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_recolor(obj, lv_color_hex(0xff000000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_width(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xffe0b7dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff9a6ebe), LV_PART_KNOB | LV_STATE_DEFAULT);
        }
        {
            // arc3
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc3 = obj;
            lv_obj_set_pos(obj, 170, 41);
            lv_obj_set_size(obj, 63, 66);
            lv_arc_set_range(obj, -30, 30);
            lv_arc_set_value(obj, 0);
            lv_obj_set_style_arc_width(obj, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff6ab9f7), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_width(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xff212121), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xffb0f7f6), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff2aa9ca), LV_PART_KNOB | LV_STATE_DEFAULT);
        }
        {
            // arc4
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc4 = obj;
            lv_obj_set_pos(obj, 248, 41);
            lv_obj_set_size(obj, 63, 66);
            lv_arc_set_range(obj, -30, 30);
            lv_arc_set_value(obj, 0);
            lv_obj_set_style_arc_width(obj, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_opa(obj, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff21f3a4), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_width(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff90dab7), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff40e56c), LV_PART_KNOB | LV_STATE_DEFAULT);
        }
        {
            // ind1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ind1 = obj;
            lv_obj_set_pos(obj, 39, 66);
            lv_obj_set_size(obj, 30, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0");
        }
        {
            // ind2
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ind2 = obj;
            lv_obj_set_pos(obj, 114, 66);
            lv_obj_set_size(obj, 30, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0");
        }
        {
            // ind4
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ind4 = obj;
            lv_obj_set_pos(obj, 265, 66);
            lv_obj_set_size(obj, 30, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0");
        }
        {
            // ind3
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ind3 = obj;
            lv_obj_set_pos(obj, 186, 66);
            lv_obj_set_size(obj, 30, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0");
        }
        {
            // AUX1
            lv_obj_t *obj = lv_checkbox_create(parent_obj);
            objects.aux1 = obj;
            lv_obj_set_pos(obj, 9, 120);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_checkbox_set_text(obj, "GPS  RESCUE");
        }
        {
            // AUX2
            lv_obj_t *obj = lv_checkbox_create(parent_obj);
            objects.aux2 = obj;
            lv_obj_set_pos(obj, 9, 146);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_checkbox_set_text(obj, "OPTICAL FLOW");
        }
        {
            // AUX3
            lv_obj_t *obj = lv_checkbox_create(parent_obj);
            objects.aux3 = obj;
            lv_obj_set_pos(obj, 9, 175);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_checkbox_set_text(obj, "ARMED");
        }
        {
            // AUX4
            lv_obj_t *obj = lv_checkbox_create(parent_obj);
            objects.aux4 = obj;
            lv_obj_set_pos(obj, 9, 203);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_checkbox_set_text(obj, "GPS HOLD");
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 223, 151);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(obj, &img_drone);
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
}



typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_main();
}
