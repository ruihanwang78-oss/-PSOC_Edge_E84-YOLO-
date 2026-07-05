#include "jsmn.h"

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens,
                                   const size_t num_tokens) {
  jsmntok_t *tok;
  if (parser->toknext >= num_tokens) return NULL;
  tok = &tokens[parser->toknext++];
  tok->start = tok->end = -1;
  tok->size = 0;
  tok->type = JSMN_UNDEFINED;
  tok->parent = -1;
  return tok;
}

static void jsmn_fill_token(jsmntok_t *token, const jsmntype_t type,
                            const int start, const int end) {
  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
                                const size_t len, jsmntok_t *tokens,
                                const size_t num_tokens) {
  jsmntok_t *token;
  int start;
  start = parser->pos;
  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    switch (js[parser->pos]) {
      case ':': case ',': case ']': case '}': case ' ':
      case '\t': case '\r': case '\n': case '\0':
        goto found;
    }
    if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
      parser->pos = start;
      return -1; /* JSMN_ERROR_INVAL */
    }
  }
found:
  if (tokens == NULL) { parser->pos--; return 0; }
  token = jsmn_alloc_token(parser, tokens, num_tokens);
  if (token == NULL) { parser->pos = start; return -2; /* NOMEM */ }
  jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
  token->parent = parser->toksuper;
  parser->pos--;
  return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js,
                             const size_t len, jsmntok_t *tokens,
                             const size_t num_tokens) {
  jsmntok_t *token;
  int start = parser->pos;
  parser->pos++;
  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    if (js[parser->pos] == '\"') {
      if (tokens == NULL) return 0;
      token = jsmn_alloc_token(parser, tokens, num_tokens);
      if (token == NULL) { parser->pos = start; return -2; }
      jsmn_fill_token(token, JSMN_STRING, start + 1, parser->pos);
      token->parent = parser->toksuper;
      return 0;
    }
    if (js[parser->pos] == '\\' && parser->pos + 1 < len) parser->pos++;
  }
  parser->pos = start;
  return -1;
}

void jsmn_init(jsmn_parser *parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser, const char *js, const size_t len,
               jsmntok_t *tokens, const unsigned int num_tokens) {
  int r;
  int count = parser->toknext;
  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    char c = js[parser->pos];
    switch (c) {
      case '{': case '[':
        count++;
        if (tokens == NULL) break;
        if (parser->toknext >= num_tokens) return -2;
        if (parser->toksuper != -1) tokens[parser->toksuper].size++;
        tokens[parser->toknext].type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
        tokens[parser->toknext].start = parser->pos;
        tokens[parser->toknext].size = 0;
        tokens[parser->toknext].parent = parser->toksuper;
        parser->toksuper = parser->toknext++;
        break;
      case '}': case ']':
        if (tokens == NULL) break;
        if (parser->toknext < 1) return -1;
        jsmntype_t type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
        if (tokens[parser->toknext - 1].type == JSMN_STRING &&
            tokens[parser->toknext - 1].size != 0)
          parser->toksuper = tokens[parser->toknext - 1].parent;
        for (;;) {
          if (parser->toksuper == -1) { if (tokens[parser->toknext - 1].type != type) return -1; break; }
          jsmntok_t *t = &tokens[parser->toksuper];
          if (t->type == type) { parser->toksuper = t->parent; break; }
          parser->toksuper = t->parent;
        }
        break;
      case '\"':
        r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
        if (r < 0) return r;
        count++;
        if (parser->toksuper != -1 && tokens != NULL)
          tokens[parser->toksuper].size++;
        break;
      case '\t': case '\r': case '\n': case ' ': break;
      case ':':
        parser->toksuper = parser->toknext - 1;
        break;
      case ',':
        if (tokens != NULL && parser->toksuper != -1 &&
            tokens[parser->toksuper].type != JSMN_ARRAY &&
            tokens[parser->toksuper].type != JSMN_OBJECT) {
          for (;;) {
            if (parser->toksuper == -1) break;
            jsmntok_t *t = &tokens[parser->toksuper];
            if (t->type == JSMN_ARRAY || t->type == JSMN_OBJECT) break;
            parser->toksuper = t->parent;
          }
        }
        break;
      default:
        r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
        if (r < 0) return r;
        count++;
        if (parser->toksuper != -1 && tokens != NULL)
          tokens[parser->toksuper].size++;
        break;
    }
  }
  if (tokens != NULL) {
    for (int i = parser->toknext - 1; i >= 0; i--) {
      if (tokens[i].start != -1 && tokens[i].end == -1) return -1;
    }
  }
  return count;
}