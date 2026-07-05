#ifndef SHARED_DISPLAY_H
#define SHARED_DISPLAY_H

#include <stdint.h>

#define DISPLAY_MAGIC       0xDEADBEEF
#define MAX_LEFT_ITEMS      6
#define MAX_RIGHT_ITEMS     6
#define MAX_LINE_LENGTH     64

typedef struct {
    uint32_t magic;
    volatile uint32_t ready;        /* CM33 写 1，CM55 读后置 0 */
    char left_lines[MAX_LEFT_ITEMS][MAX_LINE_LENGTH];   /* 6 种物品状态 */
    char right_lines[MAX_RIGHT_ITEMS][MAX_LINE_LENGTH]; /* 推荐菜谱 */
    uint32_t right_count;
} display_shared_mem_t;

extern display_shared_mem_t shared_display;

#endif
