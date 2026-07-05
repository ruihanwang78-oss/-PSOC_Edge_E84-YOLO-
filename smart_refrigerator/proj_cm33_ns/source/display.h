#ifndef DISPLAY_H
#define DISPLAY_H

void display_init(void);
void display_clear(void);

void display_show(const char **alerts, int alert_count);
void display_test(void);

#endif