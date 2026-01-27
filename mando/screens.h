#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *panel1;
    lv_obj_t *titulo;
    lv_obj_t *panel2;
    lv_obj_t *arc1;
    lv_obj_t *arc2;
    lv_obj_t *arc3;
    lv_obj_t *arc4;
    lv_obj_t *ind1;
    lv_obj_t *ind2;
    lv_obj_t *ind4;
    lv_obj_t *ind3;
    lv_obj_t *aux1;
    lv_obj_t *aux2;
    lv_obj_t *aux3;
    lv_obj_t *aux4;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/