/******************************************************************************
* File Name: inference_task.c
*
* Description: This file contains API calls to inference task.
*
*
********************************************************************************
* (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header File
*******************************************************************************/
#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "cybsp.h"
#include "cy_pdl.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
/* FreeRTOS header file */
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "FreeRTOSConfig.h"
#include "stdlib.h"
#include "model.h"
#include "ifx_image_utils.h"
#include "lcd_task.h"
#include "inference_task.h"
#include "ifx_time_utils.h"
#include "mtb_ml_utils.h"
#include "mtb_ml_common.h"
#include "mtb_ml.h"
#include "ipc_shared.h"

/*******************************************************************************
 * Global Variable
 *******************************************************************************/
/* Performance measure: Buffer wait time */ 
float prep_wait_buf; 
/* Performance measure: YUV422 to BGR565 conversion time */              
float prep_YUV422_to_bgr565; 
/* Performance measure: BGR565 to display time */      
float prep_bgr565_to_disp;   
/* Performance measure: RGB565 to RGB888 conversion time */
float prep_RGB565_to_RGB888;

#ifdef USE_DVP_CAM
extern bool frame_ready;
#endif


__attribute__((section(".cy_socmem_data")))
/* Final output variable for object detection */
__attribute__((aligned(16))) prediction_od_t prediction; 
__attribute__((section(".psram_data"))) 
float data_out[IMAI_DATA_OUT_COUNT] = {0};
/* Inference execution time */
volatile float inference_time = 0;  
/* The best class out of all i.e. the class with max value out of all classes */
float max_class_val = 0;

/*******************************************************************************
 * Per-frame inference aggregation configuration
 *******************************************************************************/
#define CONFIDENCE_THRESHOLD        (0.25f)
#define NMS_IOU_THRESHOLD           (0.65f)
/* Discard boxes smaller than 15x15 pixels after mapping to model input space. */
#define MIN_BOX_SIZE_PX             (15.0f)
#define MAX_FRAMES_PER_WINDOW       (50)   /* 20s / ~300ms ≈ 66, 留安全余量 */

typedef struct {
    int         count;
    uint32_t    labels[MAX_PREDICTIONS];
    float       confs[MAX_PREDICTIONS];
    float       x[MAX_PREDICTIONS];
    float       y[MAX_PREDICTIONS];
    float       w[MAX_PREDICTIONS];
    float       h[MAX_PREDICTIONS];
    float       avg_conf;
} frame_result_t;

/*******************************************************************************
 * Function Name: get_image
 *
 * Description:
 *   Retrieves the latest image by calling the draw function.
 *
 * Input Arguments:
 *   None
 *
 * Return Value:
 *   uint8_t* - Pointer to the image buffer
 *
 *******************************************************************************/
static uint8_t * get_image()
{
    return draw();
}

/*******************************************************************************
* Function Name: get_best_class
*******************************************************************************
*
* Summary:
*  The function calculates the best class out of all available classes.
*
* Parameters:
*  cls  - Pointer to the class array.
*  size - number of classes for the model i.e. size of class array.
*  max_cls_val - pointer to store the the best class out of all i.e. 
*                the max of all classes
*
* Return:
*  max_index - Returns the idex of the best class out of all i.e. 
*              the max of all classes
*
*******************************************************************************/
int8_t get_best_class(const float *cls, size_t size, float *max_cls_val) {
    if (cls == NULL || size == 0) {
    /* Array is empty */
        return -1;
    }

    int8_t max_index = 0;
    float max_value = cls[0];

    for (int8_t i = 1; i < size; i++) {
        if (cls[i] > max_value) {
            max_value = cls[i];
            max_index = i;
        }
    }

    *max_cls_val = max_value;
    return max_index;
}

/*******************************************************************************
 * NMS helper structures and functions
 * Adapted from SafeDrive reference; simplified for refrigerator item detection.
 *******************************************************************************/
typedef struct {
    float cx;
    float cy;
    float w;
    float h;
    float conf;
    int   class_id;
} raw_detection_t;

static float box_iou(float x1, float y1, float w1, float h1,
                     float x2, float y2, float w2, float h2)
{
    float xmin1 = x1 - w1 * 0.5f;
    float ymin1 = y1 - h1 * 0.5f;
    float xmax1 = x1 + w1 * 0.5f;
    float ymax1 = y1 + h1 * 0.5f;

    float xmin2 = x2 - w2 * 0.5f;
    float ymin2 = y2 - h2 * 0.5f;
    float xmax2 = x2 + w2 * 0.5f;
    float ymax2 = y2 + h2 * 0.5f;

    float inter_xmin = (xmin1 > xmin2) ? xmin1 : xmin2;
    float inter_ymin = (ymin1 > ymin2) ? ymin1 : ymin2;
    float inter_xmax = (xmax1 < xmax2) ? xmax1 : xmax2;
    float inter_ymax = (ymax1 < ymax2) ? ymax1 : ymax2;

    float inter_w = (inter_xmax > inter_xmin) ? (inter_xmax - inter_xmin) : 0.0f;
    float inter_h = (inter_ymax > inter_ymin) ? (inter_ymax - inter_ymin) : 0.0f;
    float inter_area = inter_w * inter_h;

    float area1 = w1 * h1;
    float area2 = w2 * h2;
    float union_area = area1 + area2 - inter_area;

    return (union_area > 0.0f) ? (inter_area / union_area) : 0.0f;
}

/* Simple descending sort by confidence. */
static void sort_detections_by_conf(raw_detection_t *dets, int *order, int n)
{
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (dets[order[j]].conf > dets[order[i]].conf) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }
}

/*******************************************************************************
 * Cross-frame de-duplication helper
 *******************************************************************************/
static float ml_box_iou(float x1, float y1, float w1, float h1,
                        float x2, float y2, float w2, float h2)
{
    return box_iou(x1, y1, w1, h1, x2, y2, w2, h2);
}

/*******************************************************************************
 * Merge best frame + cross-frame de-duplication
 * 1. Pick the frame with highest average confidence as base
 * 2. Walk all other frames; for each detection, compute IoU with existing
 *    g_ml_result entries.
 *    - IoU < 0.5  -> new object, append
 *    - IoU >= 0.5 -> same object, keep higher confidence
 *******************************************************************************/
static int merge_best_frame_with_deduplication(frame_result_t *frames,
                                                int frame_count,
                                                int best_idx)
{
    int det_count = 0;

    /* 1. Base: copy best frame entirely */
    if (best_idx >= 0 && best_idx < frame_count) {
        for (int i = 0;
             i < frames[best_idx].count && det_count < MAX_DETECTIONS;
             i++) {
            g_ml_result.labels[det_count] = frames[best_idx].labels[i];
            g_ml_result.confs[det_count]  = frames[best_idx].confs[i];
            g_ml_result.x[det_count]      = frames[best_idx].x[i];
            g_ml_result.y[det_count]      = frames[best_idx].y[i];
            g_ml_result.w[det_count]      = frames[best_idx].w[i];
            g_ml_result.h[det_count]      = frames[best_idx].h[i];
            det_count++;
        }
    }

    /* 2. Merge other frames with IoU-based de-duplication */
    for (int f = 0; f < frame_count && det_count < MAX_DETECTIONS; f++) {
        if (f == best_idx) continue;

        for (int i = 0;
             i < frames[f].count && det_count < MAX_DETECTIONS;
             i++) {
            float cx = frames[f].x[i];
            float cy = frames[f].y[i];
            float cw = frames[f].w[i];
            float ch = frames[f].h[i];

            float max_iou = 0.0f;
            int   dup_idx   = -1;

            for (int j = 0; j < det_count; j++) {
                float iou = ml_box_iou(cx, cy, cw, ch,
                                       g_ml_result.x[j], g_ml_result.y[j],
                                       g_ml_result.w[j], g_ml_result.h[j]);
                if (iou > max_iou) {
                    max_iou = iou;
                    dup_idx = j;
                }
            }

            if (max_iou < 0.5f) {
                /* New object -> append (confidence must >= 0.6 to avoid false positives) */
                if (frames[f].confs[i] >= 0.6f) {
                    g_ml_result.labels[det_count] = frames[f].labels[i];
                    g_ml_result.confs[det_count]  = frames[f].confs[i];
                    g_ml_result.x[det_count]      = cx;
                    g_ml_result.y[det_count]      = cy;
                    g_ml_result.w[det_count]      = cw;
                    g_ml_result.h[det_count]      = ch;
                    det_count++;
                }
            } else if (dup_idx >= 0 &&
                       frames[f].confs[i] >= 0.6f &&
                       frames[f].confs[i] > g_ml_result.confs[dup_idx]) {
                /* Same location, higher confidence (>=0.6), replace */
                g_ml_result.labels[dup_idx] = frames[f].labels[i];
                g_ml_result.confs[dup_idx]  = frames[f].confs[i];
                g_ml_result.x[dup_idx]      = cx;
                g_ml_result.y[dup_idx]      = cy;
                g_ml_result.w[dup_idx]      = cw;
                g_ml_result.h[dup_idx]      = ch;
            }
        }
    }

    return det_count;
}

/*******************************************************************************
 * Function Name: cm55_inference_task
 *
 * Description:
 *   Main task for running object detection inference on the CM55 core. Initializes
 *   the object detection model, preprocesses input images, performs inference, and
 *   postprocesses results. Updates performance metrics and signals the graphics
 *   display semaphore.
 *
 * Input Arguments:
 *   void *arg - Task argument (not used)
 *
 * Return Value:
 *   None
 *
 *******************************************************************************/
void cm55_inference_task( void *arg )
{
    int result = IMAI_init();
    IMAI_api_def *api_def = IMAI_api();
    volatile float _time_start_prev = 0;
    (void)_time_start_prev;

    /* model.h 输出: data_out [10, 1029]（shape[0]=anchors, shape[1]=attributes）
     * 实际内存布局由模型决定：
     *   - attr-major: data_out[attr * num_anchors + anchor]（新模型属此类）
     *   - box-major:  data_out[anchor * num_attributes + attr]
     * 为兼容不同模型，这里动态判断。 */
    int dim0 = api_def->func_list[0].param_list[1].shape[0].size;
    int dim1 = api_def->func_list[0].param_list[1].shape[1].size;
    int num_anchors     = (dim0 > dim1) ? dim0 : dim1;
    int num_attributes  = (dim0 < dim1) ? dim0 : dim1;
    bool attr_major     = (dim0 > dim1);   /* true: 数据按属性优先排列 */
    int class_offset    = attr_major ? 4 : 5;
    int num_classes_from_model = attr_major ? (num_attributes - 4)
                                            : (num_attributes - 5);

    printf("[CM55] model output: %s, anchors=%d, attributes=%d, classes=%d\r\n",
           attr_major ? "attr-major" : "box-major",
           num_anchors, num_attributes, num_classes_from_model);

    if (num_classes_from_model <= 0 || num_classes_from_model > NUM_CLASSES)
    {
        printf("Invalid model output attributes: %d, classes: %d\r\n",
               num_attributes, num_classes_from_model);
        CY_ASSERT(0);
    }

    if(result != 0)
        printf("Failed to initialize the model\r\n");

    /* 20秒识别窗口控制变量 */
    static TickType_t window_start_tick = 0;
    static int        phase = 0;  /* 1=识别中, 2=等待传输/显示 */
    static TickType_t last_lcd_notify = 0;  /* 限制传输阶段 LCD 刷新频率 */

    /* 每帧推理聚合：20s 内每一帧作为独立节点，取综合置信度最高的一帧 */
    static frame_result_t g_frames[MAX_FRAMES_PER_WINDOW];
    static int            g_frame_count = 0;

    /* 显示阶段结束标志（由 lcd_task.c 在 60 秒后置位） */
    extern volatile uint32_t g_display_phase_done;

    window_start_tick = xTaskGetTickCount();
    phase = 1;
    memset(g_frames, 0, sizeof(g_frames));
    g_frame_count = 0;

    while (true)
    {
        /* 严格互斥：传输/显示期间 CM55 不推理 */
        if (phase == 2)
        {
            if (g_ml_result.ready == 0)
            {
                if (g_display_phase_done == 1)
                {
                    /* 显示阶段结束，开启下一轮推理窗口 */
                    g_display_phase_done = 0;
                    __DSB();
                    phase = 1;
                    window_start_tick = xTaskGetTickCount();
                    memset(g_frames, 0, sizeof(g_frames));
                    g_frame_count = 0;
                    memset((void *)&g_ml_result, 0, sizeof(g_ml_result));
                    printf("[CM55] 显示阶段结束，开始下一轮视觉推理 (g_ml_result cleared)\r\n");
                }
                else
                {
                    /* CM33 已读取，正在云端通信/显示，保持 LCD 刷新 */
                    TickType_t now = xTaskGetTickCount();
                    if ((now - last_lcd_notify) >= pdMS_TO_TICKS(500))
                    {
                        result = cy_rtos_semaphore_set(&model_semaphore);
                        if (CY_RSLT_SUCCESS != result) {
                            printf("Model Semaphore set failed\r\n");
                        }
                        last_lcd_notify = now;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
            }
            else
            {
                /* CM33 尚未读取，保持 LCD 刷新 */
                TickType_t now = xTaskGetTickCount();
                if ((now - last_lcd_notify) >= pdMS_TO_TICKS(500))
                {
                    result = cy_rtos_semaphore_set(&model_semaphore);
                    if (CY_RSLT_SUCCESS != result) {
                        printf("Model Semaphore set failed\r\n");
                    }
                    last_lcd_notify = now;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

#ifdef USE_DVP_CAM
        if (frame_ready == true)
#endif
        {
            volatile float _time_start = ifx_time_get_ms_f();
#ifdef USE_DVP_CAM
            frame_ready = false;
#endif

            /* Get the latest input image to the input image buffer. */
            uint8_t *image_buf_uint8 = get_image();
            cy_rtos_delay_milliseconds(1);

            /* Object detection */
            volatile float _time_object_det = ifx_time_get_ms_f();

            __attribute__((section(".psram_data")))
            static float image_buf_float[IMAI_DATA_IN_COUNT];

            for (int i = 0; i < IMAI_DATA_IN_COUNT; i++) {
                image_buf_float[i] = (float)image_buf_uint8[i] / 255.0f;
            }

			memset(&prediction, 0, sizeof(prediction));
			prediction.count = 0;

            IMAI_compute(image_buf_float, data_out);

            int16_t *bbox_int16 = prediction.bbox_int16;
            float   *conf       = prediction.conf;
            uint8_t *class_id   = prediction.class_id;

            int32_t actual_nr_predictions = 0;

            /* ========== YOLO 后处理：解析 + NMS ========== */
            raw_detection_t raw_dets[MAX_RAW_DETECTIONS];
            int raw_count = 0;

            for (int r = 0; r < num_anchors && raw_count < MAX_RAW_DETECTIONS; r++)
            {
                float x, y, w, h;

                if (attr_major) {
                    x = data_out[0 * num_anchors + r];
                    y = data_out[1 * num_anchors + r];
                    w = data_out[2 * num_anchors + r];
                    h = data_out[3 * num_anchors + r];
                } else {
                    x = data_out[r * num_attributes + 0];
                    y = data_out[r * num_attributes + 1];
                    w = data_out[r * num_attributes + 2];
                    h = data_out[r * num_attributes + 3];
                }

                if (w <= 0.0f || h <= 0.0f ||
                    w * IMAGE_WIDTH  < MIN_BOX_SIZE_PX ||
                    h * IMAGE_HEIGHT < MIN_BOX_SIZE_PX) {
                    continue;
                }

                /* 找最佳类别 */
                int   best_cls   = 0;
                float best_score = attr_major
                                   ? data_out[(class_offset + 0) * num_anchors + r]
                                   : data_out[r * num_attributes + class_offset + 0];
                for (int c = 1; c < num_classes_from_model; c++) {
                    float score = attr_major
                                  ? data_out[(class_offset + c) * num_anchors + r]
                                  : data_out[r * num_attributes + class_offset + c];
                    if (score > best_score) {
                        best_score = score;
                        best_cls   = c;
                    }
                }

                if (best_cls < 0 || best_score < CONFIDENCE_THRESHOLD) continue;

                raw_dets[raw_count].cx       = x;
                raw_dets[raw_count].cy       = y;
                raw_dets[raw_count].w        = w;
                raw_dets[raw_count].h        = h;
                raw_dets[raw_count].conf     = best_score;
                raw_dets[raw_count].class_id = best_cls;
                raw_count++;
            }

            /* NMS：按类别分别抑制重复框 */
            int order[MAX_RAW_DETECTIONS];
            sort_detections_by_conf(raw_dets, order, raw_count);

            int keep[MAX_RAW_DETECTIONS] = {0};
            for (int i = 0; i < raw_count && actual_nr_predictions < MAX_PREDICTIONS; i++) {
                int idx_i = order[i];
                if (keep[idx_i]) continue;

                /* 保留此框 */
                int cls_i = raw_dets[idx_i].class_id;

                for (int j = i + 1; j < raw_count; j++) {
                    int idx_j = order[j];
                    if (keep[idx_j]) continue;
                    if (raw_dets[idx_j].class_id != cls_i) continue;

                    float iou_val = box_iou(raw_dets[idx_i].cx, raw_dets[idx_i].cy,
                                             raw_dets[idx_i].w,  raw_dets[idx_i].h,
                                             raw_dets[idx_j].cx, raw_dets[idx_j].cy,
                                             raw_dets[idx_j].w,  raw_dets[idx_j].h);
                    if (iou_val > NMS_IOU_THRESHOLD) {
                        keep[idx_j] = 1;
                    }
                }

                /* 写入 prediction 结构体 */
                float x = raw_dets[idx_i].cx;
                float y = raw_dets[idx_i].cy;
                float w = raw_dets[idx_i].w;
                float h = raw_dets[idx_i].h;
                int   idx = actual_nr_predictions;

                class_id[idx] = (uint8_t)cls_i;
                conf[idx]     = raw_dets[idx_i].conf;

                /* 类别名：强制使用 g_label_names[cls_i]，model_labels 顺序与模型输出不一致 */
                char *class_string = prediction.class_string[idx];
                if (cls_i < NUM_CLASSES && g_label_names[cls_i] != NULL) {
                    strncpy(class_string, g_label_names[cls_i], MAX_CLASS_LEN - 1);
                    class_string[MAX_CLASS_LEN - 1] = '\0';
                } else {
                    snprintf(class_string, MAX_CLASS_LEN, "cls%d", cls_i);
                }

                /* 归一化坐标 → 像素坐标 (224x224) */
                bbox_int16[idx * 4 + 0] = (int16_t)((x - HALF(w)) * IMAGE_WIDTH  + RND_F2I_FACTOR);
                bbox_int16[idx * 4 + 1] = (int16_t)((y - HALF(h)) * IMAGE_HEIGHT + RND_F2I_FACTOR);
                bbox_int16[idx * 4 + 2] = (int16_t)((x + HALF(w)) * IMAGE_WIDTH  + RND_F2I_FACTOR);
                bbox_int16[idx * 4 + 3] = (int16_t)((y + HALF(h)) * IMAGE_HEIGHT + RND_F2I_FACTOR);

                actual_nr_predictions++;
            }

            prediction.count = actual_nr_predictions;

            /* ============================================
             * 每帧独立保存：20s 内每一帧作为节点，
             * 取综合置信度（该帧所有框置信度平均）最高的一帧作为最终结果。
             * ============================================ */
            if (phase == 1)  /* 识别中 */
            {
                uint32_t elapsed = (xTaskGetTickCount() - window_start_tick) * portTICK_PERIOD_MS;

                /* 保存本帧 NMS 后的结果 */
                if (g_frame_count < MAX_FRAMES_PER_WINDOW && prediction.count > 0)
                {
                    float sum_conf = 0.0f;
                    g_frames[g_frame_count].count = prediction.count;
                    for (int i = 0; i < prediction.count; i++)
                    {
                        float x1_pix = (float)prediction.bbox_int16[i * 4 + 0];
                        float y1_pix = (float)prediction.bbox_int16[i * 4 + 1];
                        float x2_pix = (float)prediction.bbox_int16[i * 4 + 2];
                        float y2_pix = (float)prediction.bbox_int16[i * 4 + 3];

                        g_frames[g_frame_count].labels[i] = prediction.class_id[i];
                        g_frames[g_frame_count].confs[i]  = prediction.conf[i];
                        g_frames[g_frame_count].x[i]      = (x1_pix + x2_pix) / (2.0f * IMAGE_WIDTH);
                        g_frames[g_frame_count].y[i]      = (y1_pix + y2_pix) / (2.0f * IMAGE_HEIGHT);
                        g_frames[g_frame_count].w[i]      = (x2_pix - x1_pix) / IMAGE_WIDTH;
                        g_frames[g_frame_count].h[i]      = (y2_pix - y1_pix) / IMAGE_HEIGHT;
                        sum_conf += prediction.conf[i];
                    }
                    g_frames[g_frame_count].avg_conf =
                        (prediction.count > 0) ? (sum_conf / prediction.count) : 0.0f;
                    g_frame_count++;
                }

                /* 检查20秒是否结束 */
                if (elapsed >= 20000U)
                {
                    phase = 2;  /* 进入等待传输阶段 */

                    /* 1. 找平均置信度最高的一帧 */
                    int best_frame_idx = -1;
                    float best_avg_conf = 0.0f;
                    for (int i = 0; i < g_frame_count; i++)
                    {
                        if (g_frames[i].avg_conf > best_avg_conf)
                        {
                            best_avg_conf = g_frames[i].avg_conf;
                            best_frame_idx = i;
                        }
                    }

                    /* 2. 最佳帧 + 跨帧去重合并 */
                    int det_count = merge_best_frame_with_deduplication(
                                        g_frames, g_frame_count, best_frame_idx);

                    g_ml_result.count = det_count;
                    g_ml_result.timestamp = xTaskGetTickCount();

                    __DSB();
                    g_ml_result.ready = 1;  /* 通知 CM33 */

                    printf("\r\n[CM55] >>> WINDOW END, best_frame=%d/%d, count=%d, avg_conf=%.3f <<<\r\n",
                           best_frame_idx, g_frame_count, det_count, best_avg_conf);
                    for (int i = 0; i < det_count; i++) {
                        printf("[CM55] DETECTED: label=%lu %s, conf=%.2f\r\n",
                               g_ml_result.labels[i],
                               g_label_names[g_ml_result.labels[i]],
                               g_ml_result.confs[i]);
                    }
                    if (det_count == 0) {
                        printf("[CM55] NO VALID TARGET DETECTED\r\n");
                    }

                    /* 调试：打印每帧的检测数量和平均置信度 */
                    printf("[CM55] frame stats:");
                    for (int i = 0; i < g_frame_count; i++) {
                        printf(" [%d]cnt=%d,avg_conf=%.3f", i,
                               g_frames[i].count, g_frames[i].avg_conf);
                    }
                    printf("\r\n");
                }
            }
            else if (phase == 2)  /* 等待传输/显示 */
            {
                if (g_ml_result.ready == 0)
                {
                    if (g_display_phase_done == 1)
                    {
                        /* 显示阶段结束，开启下一轮推理窗口 */
                        g_display_phase_done = 0;
                        __DSB();
                        phase = 1;
                        window_start_tick = xTaskGetTickCount();
                        memset(g_frames, 0, sizeof(g_frames));
                        g_frame_count = 0;
                        memset((void *)&g_ml_result, 0, sizeof(g_ml_result));
                        printf("[CM55] 显示阶段结束，开始下一轮视觉推理 (g_ml_result cleared)\r\n");
                    }
                    else
                    {
                        /* CM33 已读取，正在云端通信/显示，触发 LCD 刷新 */
                        TickType_t now = xTaskGetTickCount();
                        if ((now - last_lcd_notify) >= pdMS_TO_TICKS(500))
                        {
                            result = cy_rtos_semaphore_set(&model_semaphore);
                            if (CY_RSLT_SUCCESS != result) {
                                printf("Model Semaphore set failed\r\n");
                            }
                            last_lcd_notify = now;
                        }
                        vTaskDelay(pdMS_TO_TICKS(100));
                        continue;
                    }
                }
                else
                {
                    /* CM33 尚未读取，触发 LCD 刷新显示共享内存内容 */
                    TickType_t now = xTaskGetTickCount();
                    if ((now - last_lcd_notify) >= pdMS_TO_TICKS(500))
                    {
                        result = cy_rtos_semaphore_set(&model_semaphore);
                        if (CY_RSLT_SUCCESS != result) {
                            printf("Model Semaphore set failed\r\n");
                        }
                        last_lcd_notify = now;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
            }
            /* ============================================ */

            volatile float _time_end = ifx_time_get_ms_f();
            inference_time = _time_end - _time_object_det;

            result = cy_rtos_semaphore_set(&model_semaphore);
            if (CY_RSLT_SUCCESS != result) {
                printf("\r\nModel Semphore set failed\r\n");
            }
            _time_start_prev = _time_start;
        }
    }
}