#ifndef ZAN_WINRES_H
#define ZAN_WINRES_H

#include <stddef.h>

/* 底层系统交互与数据协议契约 */
int zan_winres_icon_object(const char *ico_path, const char *out_obj_path,
                          int arm64);

/* 底层系统交互与数据协议契约 */
int zan_winres_icon_object_mem(const unsigned char *ico, size_t ico_len,
                              const char *out_obj_path, int arm64);

#endif
