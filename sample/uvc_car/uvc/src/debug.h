#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASCII_COLOR_BLACK "\033[1;30m"
#define ASCII_COLOR_RED "\033[1;31m"
#define ASCII_COLOR_GREEN "\033[1;32m"
#define ASCII_COLOR_YELLOW "\033[1;33m"
#define ASCII_COLOR_BLUE "\033[1;34m"
#define ASCII_COLOR_PURPLE "\033[1;35m"
#define ASCII_COLOR_DARK_GREEN "\033[1;36m"
#define ASCII_COLOR_WHITE "\033[1;37m"

#define ASCII_COLOR_END "\033[0m"

#define DBG_INFO(fmt, args...)                                                                     \
    printf(ASCII_COLOR_GREEN "%s[%d]: " fmt ASCII_COLOR_END, __FUNCTION__, __LINE__, ##args);
#define DBG_ERR(fmt, args...)                                                                      \
    printf(ASCII_COLOR_RED "%s[%d]: " fmt ASCII_COLOR_END, __FUNCTION__, __LINE__, ##args);

#define AUTOSOME_ERROR(format, ...)                                                                \
    fprintf(stderr, ASCII_COLOR_RED "Autosome" ASCII_COLOR_END " %s[%d] ====> " format "\n",       \
            __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define AUTOSOME_WARM(format, ...)                                                                 \
    fprintf(stderr, ASCII_COLOR_YELLOW "Autosome" ASCII_COLOR_END " %s[%d] ====> " format "\n",    \
            __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define AUTOSOME_DEBUG(format, ...)                                                                \
    fprintf(stderr, ASCII_COLOR_BLUE "Autosome" ASCII_COLOR_END " %s[%d] ====> " format "\n",      \
            __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define AUTOSOME_INFO(format, ...)                                                                 \
    fprintf(stderr, ASCII_COLOR_GREEN "Autosome" ASCII_COLOR_END " %s[%d] ====> " format "\n",     \
            __FUNCTION__, __LINE__, ##__VA_ARGS__)

#endif   //__DEBUG_H__
