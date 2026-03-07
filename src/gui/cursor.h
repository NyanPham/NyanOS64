#ifndef CURSOR_H_INC
#define CURSOR_H_INC

#define CURSOR_W 12
#define CURSOR_H 18

typedef enum
{
    CURSOR_ARROW,
    CURSOR_RESIZE_H,
    CURSOR_RESIZE_V,
    CURSOR_MOVE,
} CursorType;

void cursor_init(void);
void cursor_set_shape(CursorType type);
void cursor_erase(void);
void cursor_paint(void);

#endif