#ifndef JSMN_H
#define JSMN_H
#include <stddef.h>

typedef enum {
  JSMN_UNDEFINED = 0,
  JSMN_OBJECT = 1,
  JSMN_ARRAY = 2,
  JSMN_STRING = 3,
  JSMN_PRIMITIVE = 4
} jsmntype_t;

typedef struct {
  jsmntype_t type;
  int start;
  int end;
  int size;
  int parent;   /* ← 加上这一行 */
} jsmntok_t;

typedef struct {
  unsigned int pos;
  unsigned int toknext;
  int toksuper;
} jsmn_parser;

void jsmn_init(jsmn_parser *parser);
int jsmn_parse(jsmn_parser *parser, const char *js, const size_t len,
               jsmntok_t *tokens, const unsigned int num_tokens);

#endif