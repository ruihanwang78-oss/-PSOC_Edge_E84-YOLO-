#include "display.h"
#include <stdio.h>
#include <string.h>
#include "shared_display.h"
#include "cmsis_gcc.h"

/* 共享内存变量定义在 CM33 的 .cy_shared_socmem 段 */
display_shared_mem_t __attribute__((section(".cy_shared_socmem"))) shared_display;

/* 6 种食材英文名称（与云端 alert key 对应，顺序与模型输出索引一致） */
static const char *g_item_names[6] = {
    "Banana", "Egg", "Fanta", "Green Veg", "Ice Cream", "Tomato"
};
static const char *g_item_keys[6] = {
    "banana", "egg", "fanta_orange", "green_vegetable", "ice_cream", "tomato"
};

static void lcd_clear(void) {
    printf("\n========== DISPLAY ==========\n");
}

static void lcd_draw_string(int x, int y, const char *str) {
    printf("  [%d,%d] %s\n", y, x, str);
}

void display_init(void) {
    printf("[display_init] Ready.\n");
}

void display_clear(void) {
    lcd_clear();
}

void display_show(const char **alerts, int alert_count)
{
    lcd_clear();

    /* 先清标志，防止 CM55 读到半成品 */
    shared_display.ready = 0;
    __DSB();

    /* ---------- 左侧：食材名称 ---------- */
    /* ---------- 右侧：库存状态 ---------- */
    for (int i = 0; i < 6; i++) {
        int is_alert = 0;
        for (int a = 0; a < alert_count; a++) {
            if (strcmp(alerts[a * 2], g_item_keys[i]) == 0) {
                is_alert = 1;
                break;
            }
        }

        /* 左侧写食材名 */
        strncpy(shared_display.left_lines[i], g_item_names[i], MAX_LINE_LENGTH - 1);
        shared_display.left_lines[i][MAX_LINE_LENGTH - 1] = '\0';
        lcd_draw_string(0, i, shared_display.left_lines[i]);

        /* 右侧写库存状态 */
        const char *status = is_alert ? "OUT OF STOCK!" : "IN STOCK";
        strncpy(shared_display.right_lines[i], status, MAX_LINE_LENGTH - 1);
        shared_display.right_lines[i][MAX_LINE_LENGTH - 1] = '\0';
        lcd_draw_string(40, i, shared_display.right_lines[i]);
    }

    shared_display.right_count = 6;

    /* 提交：写屏障 + 置标志通知 CM55 */
    shared_display.magic = DISPLAY_MAGIC;
    __DSB();
    shared_display.ready = 1;
    __DSB();
}

void display_test(void)
{
    printf("[display_test] Running...\n");
    display_init();

    const char *test_alerts[2] = {"banana", "Out of stock"};
    display_show(test_alerts, 1);
    printf("[display_test] Done.\n");
}
