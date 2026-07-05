#include "json_parser.h"
#include "jsmn.h"
#include <string.h>

int parse_cloud_response(const char *json, int len, parsed_response_t *out)
{
    const char *body = json;
    int body_len = len;

    const char *header_end = strstr(json, "\r\n\r\n");
    if (header_end != NULL) {
        body = header_end + 4;
        body_len = len - (int)(body - json);
    }

    jsmn_parser p;
    jsmntok_t t[80];
    int r;

    memset(out, 0, sizeof(parsed_response_t));

    jsmn_init(&p);
    r = jsmn_parse(&p, body, (size_t)body_len, t, 80);
    if (r < 0) return r;

    for (int i = 1; i < r; i++) {
        if (t[i].type != JSMN_STRING) continue;

        int klen = t[i].end - t[i].start;
        const char *key = body + t[i].start;

        /* ========== 解析 alerts 数组（用 size 控制次数）========== */
        if (klen == 6 && strncmp(key, "alerts", 6) == 0) {
            if (i + 1 < r && t[i+1].type == JSMN_ARRAY && t[i+1].size > 0) {
                int obj_idx = i + 2;
                for (int a = 0; a < t[i+1].size && out->alert_count < MAX_ALERTS; a++) {
                    if (obj_idx < r && t[obj_idx].type == JSMN_OBJECT) {
                        int field_end = obj_idx + 1 + t[obj_idx].size * 2;
                        if (field_end > r) field_end = r;

                        for (int f = obj_idx + 1; f < field_end; f += 2) {
                            if (t[f].type == JSMN_STRING && f + 1 < r) {
                                int fk = t[f].end - t[f].start;
                                const char *fkey = body + t[f].start;
                                if (fk == 4 && strncmp(fkey, "item", 4) == 0) {
                                    out->alert_items[out->alert_count] = body + t[f+1].start;
                                    out->alert_item_lens[out->alert_count] = t[f+1].end - t[f+1].start;
                                } else if (fk == 7 && strncmp(fkey, "message", 7) == 0) {
                                    out->alert_msgs[out->alert_count] = body + t[f+1].start;
                                    out->alert_msg_lens[out->alert_count] = t[f+1].end - t[f+1].start;
                                }
                            }
                        }
                        out->alert_count++;
                        obj_idx = field_end;
                    } else {
                        obj_idx++;
                        a--; /* 不是 object，不算一个元素 */
                    }
                }
            }
        }
        /* ========== 解析 recipes 数组（用 size 控制次数）========== */
        else if (klen == 7 && strncmp(key, "recipes", 7) == 0) {
            if (i + 1 < r && t[i+1].type == JSMN_ARRAY && t[i+1].size > 0) {
                int obj_idx = i + 2;
                for (int a = 0; a < t[i+1].size && out->recipe_count < MAX_RECIPES; a++) {
                    if (obj_idx < r && t[obj_idx].type == JSMN_OBJECT) {
                        int field_end = obj_idx + 1 + t[obj_idx].size * 2;
                        if (field_end > r) field_end = r;

                        for (int f = obj_idx + 1; f < field_end; f += 2) {
                            if (t[f].type == JSMN_STRING && f + 1 < r) {
                                int fk = t[f].end - t[f].start;
                                if (fk == 4 && strncmp(body + t[f].start, "name", 4) == 0) {
                                    out->recipe_names[out->recipe_count] = body + t[f+1].start;
                                    out->recipe_name_lens[out->recipe_count] = t[f+1].end - t[f+1].start;
                                }
                            }
                        }
                        out->recipe_count++;
                        obj_idx = field_end;
                    } else {
                        obj_idx++;
                        a--; /* 不是 object，不算一个元素 */
                    }
                }
            }
        }
    }

    out->valid = (out->recipe_count > 0 || out->alert_count > 0) ? 1 : 0;
    return 0;
}