/* 底层系统交互与数据协议契约 */

#ifndef ZAN_IPA_H
#define ZAN_IPA_H

#include <stddef.h>

/* 底层系统交互与数据协议契约 */
int zan_ipa_build(const char *ipa_path, const char *binary_path,
                  const char *app_name, const char *bundle_id,
                  const char *display_name, const char *version);

#endif /* ZAN_IPA_H */
