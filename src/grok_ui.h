#ifndef GROK_UI_H
#define GROK_UI_H

typedef struct grok_ui_rect {
    int x;
    int y;
    int w;
    int h;
} grok_ui_rect_t;

int grok_ui_slot_rect(int slot, grok_ui_rect_t *rect);

int grok_ui_capture_slot(int slot, const char *png_path);
int grok_ui_ocr_text(const char *png_path, char **text_out);

int grok_ui_click_slot(int slot, int x, int y);
int grok_ui_type_text(const char *text);
int grok_ui_key(const char *key);

void grok_ui_free_text(char *text);

#endif
