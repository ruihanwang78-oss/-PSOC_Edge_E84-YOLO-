#include "ipc_shared.h"

ml_result_t g_ml_result __attribute__((section(".cy_ipc_shared"))) = {0};

const char *g_label_names[] = {
    "banana", "egg", "fanta_orange", "green_vegetable", "ice_cream", "tomato"
};