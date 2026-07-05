#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdint.h>

#define MAX_ALERTS   6
#define MAX_RECIPES  5

typedef struct {
    int valid;
    
    /* alerts 数组 */
    const char *alert_items[MAX_ALERTS];
    int alert_item_lens[MAX_ALERTS];
    const char *alert_msgs[MAX_ALERTS];
    int alert_msg_lens[MAX_ALERTS];
    int alert_count;

    /* recipes 数组 */
    const char *recipe_names[MAX_RECIPES];
    int recipe_name_lens[MAX_RECIPES];
    int recipe_count;
} parsed_response_t;

int parse_cloud_response(const char *json, int len, parsed_response_t *out);

#endif