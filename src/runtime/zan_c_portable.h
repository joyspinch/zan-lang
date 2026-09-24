#ifndef ZAN_C_PORTABLE_H
#define ZAN_C_PORTABLE_H

#if defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
  typedef __SIZE_TYPE__ size_t;
  typedef __INT8_TYPE__ int8_t;
  typedef __UINT8_TYPE__ uint8_t;
  typedef __INT16_TYPE__ int16_t;
  typedef __UINT16_TYPE__ uint16_t;
  typedef __INT32_TYPE__ int32_t;
  typedef __UINT32_TYPE__ uint32_t;
  typedef __INT64_TYPE__ int64_t;
  typedef __UINT64_TYPE__ uint64_t;
  typedef _Bool bool;
  #define true 1
  #define false 0
  #define NULL ((void*)0)

  void *malloc(size_t size);
  void *calloc(size_t nmemb, size_t size);
  void *realloc(void *ptr, size_t size);
  void free(void *ptr);
  void *memcpy(void *dest, const void *src, size_t n);
  void *memset(void *s, int c, size_t n);
  size_t strlen(const char *s);
  char *strcpy(char *dest, const char *src);
  int strcmp(const char *s1, const char *s2);
  int strncmp(const char *s1, const char *s2, size_t n);
  char *strstr(const char *haystack, const char *needle);
  int snprintf(char *str, size_t size, const char *format, ...);
#else
  #include <stddef.h>
  #include <stdint.h>
  #include <stdbool.h>
  #include <stdlib.h>
  #include <string.h>
  #include <stdio.h>
#endif

#endif /* ZAN_C_PORTABLE_H */
