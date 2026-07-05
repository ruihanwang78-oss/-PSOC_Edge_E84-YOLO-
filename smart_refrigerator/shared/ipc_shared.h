#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include <stdint.h>

#define MAX_DETECTIONS  30

typedef struct {
    volatile uint32_t ready;        /* 0=无数据, 1=有新结果 */
    volatile uint32_t count;        /* 实际检测数量 */
    volatile uint32_t labels[MAX_DETECTIONS];
    volatile float    confs[MAX_DETECTIONS];
    volatile float    x[MAX_DETECTIONS];   /* 中心点 x (0.0~1.0) */
    volatile float    y[MAX_DETECTIONS];   /* 中心点 y (0.0~1.0) */
    volatile float    w[MAX_DETECTIONS];   /* 宽度 (0.0~1.0) */
    volatile float    h[MAX_DETECTIONS];   /* 高度 (0.0~1.0) */
    volatile uint32_t timestamp;
} ml_result_t;

extern ml_result_t g_ml_result;

extern const char *g_label_names[];

#endif