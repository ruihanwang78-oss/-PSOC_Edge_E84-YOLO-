# 智能冰箱项目修改记录

> 修改时间：2025-06-09
> 修改范围：P0（共享内存修复）+ P1（CM55 读取 CM33 显示共享内存）

---

## 一、修改文件清单

| 序号 | 文件路径 | 修改类型 | 所属任务 |
|------|----------|----------|----------|
| 1 | `bsps/.../COMPONENT_CM33/TOOLCHAIN_GCC_ARM/pse84_ns_cm33.ld` | 新增段 | P0 |
| 2 | `bsps/.../COMPONENT_CM55/TOOLCHAIN_GCC_ARM/pse84_ns_cm55.ld` | 新增段 | P0 |
| 3 | `shared/ipc_shared.h` | 修复重复定义 | P0 |
| 4 | `shared/ipc_shared.c` | 添加定义 | P0 |
| 5 | `shared/shared_display.h` | 新增文件（从 cm33_ns/source 复制并修改） | P0/P1 |
| 6 | `proj_cm55/source/lcd_task.c` | 添加共享内存读取 + 显示逻辑 | P1 |

---

## 二、P0：修复共享内存物理映射

### 问题根因
`g_ml_result` 被声明在 `.cy_ipc_shared` 段，但两边链接器脚本都没有这个段名，导致变量被放到各自本地 RAM，CM33 和 CM55 看到的是两份独立副本，数据不通。

### 修改 1：CM33 链接器脚本
**文件：** `bsps/TARGET_APP_KIT_PSE84_EVAL_EPC2/COMPONENT_CM33/TOOLCHAIN_GCC_ARM/pse84_ns_cm33.ld`

在 `.cy_shared_socmem` 段之后，新增：
```ld
/* A section for IPC shared memory between CM33 and CM55 */
.cy_ipc_shared (NOLOAD) : ALIGN(4)
{
    KEEP(*(.cy_ipc_shared))
    KEEP(*(.cy_ipc_shared*))

    . = ALIGN(4);
} > m33_m55_shared
```
- 映射到 `m33_m55_shared` 区域（地址 `0x26480000`，大小 256KB）
- 与 CM55 的 `.cy_ipc_shared` 指向同一块物理内存

### 修改 2：CM55 链接器脚本
**文件：** `bsps/TARGET_APP_KIT_PSE84_EVAL_EPC2/COMPONENT_CM55/TOOLCHAIN_GCC_ARM/pse84_ns_cm55.ld`

1. 新增 `.cy_ipc_shared` 段（同 CM33）：
```ld
/* A section for IPC shared memory between CM33 and CM55 */
.cy_ipc_shared (NOLOAD) : ALIGN(4)
{
    KEEP(*(.cy_ipc_shared))
    KEEP(*(.cy_ipc_shared*))

    . = ALIGN(4);
} > m33_m55_shared
```

2. 新增 `.cy_shared_socmem` 段（用于显示共享内存）：
```ld
/* A section for shared SOC memory (display data from CM33) */
.cy_shared_socmem (NOLOAD) : ALIGN(4)
{
    KEEP(*(.cy_shared_socmem))
    KEEP(*(.cy_shared_socmem*))

    . = ALIGN(4);
} > m33_m55_shared
```

> 注意：`.reserved_socmem` 仍保留在 `m33_m55_shared` 末尾，但由于链接器按顺序分配，`.cy_ipc_shared` 和 `.cy_shared_socmem` 会先被放置，不会冲突。

### 修改 3/4：修复 `g_label_names` 重复定义
**文件：** `shared/ipc_shared.h` + `shared/ipc_shared.c`

- `ipc_shared.h`：将 `static const char *g_label_names[] = {...}` 改为 `extern const char *g_label_names[];`
- `ipc_shared.c`：添加 `const char *g_label_names[] = {...};` 定义

**原因：** 原写法在头文件中直接定义数组，被多个 `.c` 文件包含时会导致链接器报 "multiple definition" 错误。

### 修改 5：统一 `shared_display.h` 位置
**文件：** `shared/shared_display.h`（新增）

- 从 `proj_cm33_ns/source/shared_display.h` 复制到 `shared/shared_display.h`
- 添加 `extern display_shared_mem_t shared_display;` 声明
- 两边 Makefile 都已包含 `INCLUDES+= ../shared`，无需额外修改 include path

---

## 三、P1：CM55 读取 CM33 显示共享内存

### 修改 6：CM55 显示任务
**文件：** `proj_cm55/source/lcd_task.c`

#### (a) 添加头文件包含
```c
#include "shared_display.h"
```

#### (b) 声明共享内存变量
```c
/* CM33 写入的显示共享内存（与 CM33 的 display.c 中变量同地址） */
CY_SECTION(".cy_shared_socmem")
display_shared_mem_t shared_display;
```
- 使用 `CY_SECTION(".cy_shared_socmem")` 确保变量被链接器放到物理共享内存

#### (c) 新增 `update_cloud_info()` 函数
```c
static void update_cloud_info(vg_lite_buffer_t *render_target)
{
    /* Check if CM33 has posted new display data */
    if (shared_display.ready != 1 || shared_display.magic != DISPLAY_MAGIC)
    {
        return;
    }

    /* Use semi-transparent dark background for readability */
    ifx_set_bg_color(0x00202020);

    int y_pos = 360;
    const int line_height = 22;
    const int max_y = 430;  /* Keep above inference-time text at y=440 */

    for (uint32_t i = 0; i < shared_display.line_count && i < MAX_DISPLAY_LINES; i++)
    {
        if (shared_display.lines[i][0] == '\0')
            continue;

        ifx_print_to_buffer(10, y_pos, "%s", shared_display.lines[i]);
        y_pos += line_height;
        if (y_pos > max_y)
            break;
    }

    /* Clear ready flag so CM33 can write next batch */
    __DSB();
    shared_display.ready = 0;
    __DSB();
}
```

#### (d) 主循环中调用
在 `cm55_ns_gfx_task()` 的主循环中，推理结果显示之后：
```c
update_box_data(render_target, &prediction);
update_box_data1(render_target, inference_time);
update_cloud_info(render_target);  /* Overlay cloud alerts/recipes from CM33 */

VG_switch_frame();
```

---

## 四、编译验证建议

1. **编译整个项目**：`make -j8`（或 ModusToolbox 中 Build）
2. **检查 map 文件**：确认以下符号地址
   - CM33 的 `g_ml_result` 应在 `0x26480000` 附近（`.cy_ipc_shared` 段）
   - CM55 的 `g_ml_result` 地址应与 CM33 相同
   - CM33 的 `shared_display` 应在 `0x26480000 + offset` 附近（`.cy_shared_socmem` 段）
   - CM55 的 `shared_display` 地址应与 CM33 相同
3. **运行时验证**：
   - CM55 端 UART 应输出 `[CM55] Wrote X detections to shared memory`
   - CM33 端 UART 应输出 `[CM33] ML detection data sent OK`
   - CM55 LCD 上应在画面底部（y=360~430）显示云端返回的缺货警告和推荐菜谱

---

## 五、数据流总结

```
USB Camera → CM55 推理 → g_ml_result (共享内存) → CM33 读取 → HTTP POST 到云端
                                              ↑
                                              ↓
Cloud API 返回 JSON → CM33 解析 → shared_display (共享内存) → CM55 LCD 显示
```

两条共享内存通道：
| 通道 | 方向 | 段名 | 内容 |
|------|------|------|------|
| ML 结果 | CM55 → CM33 | `.cy_ipc_shared` | 检测框、类别、置信度 |
| 显示数据 | CM33 → CM55 | `.cy_shared_socmem` | 缺货警告、推荐菜谱 |
