#ifndef ZAN_MOBILE_POSTLINK_H
#define ZAN_MOBILE_POSTLINK_H

#include <stdbool.h>

typedef struct {
    const char *input_path;
    const char *linked_path;
    const char *staged_path;
    const char *apk_path;
    const char *apk_package;
    const char *apk_label;
    bool apk_arm64;
    bool quiet;
    int driver_count;
    const char (*driver_dirs)[1024];
    const char *const *driver_names;
    const int *driver_name_lengths;
    int android_permission_count;
    const char (*android_permissions)[128];
    const char *ipa_path;
    const char *ipa_name;
    const char *ipa_bundle_id;
    const char *icon_path;
} zan_mobile_postlink_options_t;

/* Borrow the inputs for this call. Select an APK or IPA by its final path and
 * write the package to staged_path. Remove linked_path after the attempt.
 * Return 0 on success and 1 on failure. */
int zan_mobile_postlink(const zan_mobile_postlink_options_t *options);

#endif
