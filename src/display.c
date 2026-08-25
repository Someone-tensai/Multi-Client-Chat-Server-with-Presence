#include<stdio.h>
#include<time.h>
#include<pthread.h>
#include "../include/display.h"

static pthread_mutex_t screen_lock = PTHREAD_MUTEX_INITIALIZER;


#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_RED "\033[1;31m"
#define COLOR_BLUE "\033[1;34m"
#define COLOR_YELLOW "\033[2;33m"
#define STYLE_PM "\033[3;35m"

 
static void print_timestamp(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    printf("[%02d:%02d] ", t->tm_hour, t->tm_min);
}

void display_message(const char *room, const char *user, const char *text)
{
    pthread_mutex_lock(&screen_lock);
    print_timestamp();
    printf( COLOR_BLUE "%s " COLOR_RESET "| "  COLOR_GREEN "%s:" COLOR_RESET " %s"  "\n", room, user, text);
    pthread_mutex_unlock(&screen_lock);

}

void display_pm(const char *from_user, const char *text)
{
    pthread_mutex_lock(&screen_lock);
    print_timestamp();
    printf(STYLE_PM "[DM from %s]" COLOR_RESET ": %s"  "\n", from_user, text);
    pthread_mutex_unlock(&screen_lock);

}

void display_notice(const char *text)
{
    pthread_mutex_lock(&screen_lock);
    print_timestamp();
    printf(COLOR_YELLOW"*** NOTICE : %s ***" COLOR_RESET "\n", text);
    pthread_mutex_unlock(&screen_lock);
}

void display_error(const char *err_code) 
{
    pthread_mutex_lock(&screen_lock);
    print_timestamp();
    printf(COLOR_RED "[error] %s !!!" COLOR_RESET "\n", err_code);
    pthread_mutex_unlock(&screen_lock);
}

void display_system(const char *text)
{
    pthread_mutex_lock(&screen_lock);
    print_timestamp();
    printf(COLOR_YELLOW "[system] %s" COLOR_RESET "\n", text);
    pthread_mutex_unlock(&screen_lock);
}

