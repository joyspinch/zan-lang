
#ifndef ZAN_IPA_H
#define ZAN_IPA_H

#include <stddef.h>

int zan_ipa_build(const char *ipa_path, const char *binary_path,
                  const char *app_name, const char *bundle_id,
                  const char *display_name, const char *version,
                  const char *icon_path, const char *assets_dir);

#endif /* ZAN_IPA_H */
