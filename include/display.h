#ifndef DISPLAY_H
#define DISPLAY_H

void display_message(const char *room, const char *user, const char *text);
void display_pm(const char *from_user, const char *text);
void display_notice(const char *text);
void display_error(const char * err_code);
void display_system(const char *text);

#endif