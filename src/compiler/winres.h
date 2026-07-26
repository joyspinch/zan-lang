#ifndef ZAN_WINRES_H
#define ZAN_WINRES_H

#include <stddef.h>

/* Writes a COFF object embedding `ico_path` as the program's icon resources
 * (RT_ICON + RT_GROUP_ICON, id 1), ready to be handed to the linker. `arm64`
 * selects the target machine the object is stamped with (0 = x86-64). Returns
 * 0 on success, non-zero when the icon cannot be read or parsed. */
int zan_winres_icon_object(const char *ico_path, const char *out_obj_path,
                          int arm64);

/* Same, from an in-memory .ico image (used for the icon compiled into zanc,
 * which every Windows executable gets when no --icon is given). */
int zan_winres_icon_object_mem(const unsigned char *ico, size_t ico_len,
                              const char *out_obj_path, int arm64);

#endif
