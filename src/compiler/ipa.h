/* ipa.h -- one-shot iOS IPA packaging for zanc (--emit-ipa).
 *
 * An IPA is a zip archive containing:
 *   Payload/<AppName>.app/
 *     <AppName>                (Mach-O arm64 executable with ad-hoc code signature)
 *     Info.plist               (XML plist with bundle id, executable name, orientation, etc.)
 *     PkgInfo                  (8-byte ASCII: "APPL????")
 *
 * This generator produces standard .ipa files installable via TrollStore,
 * jailbreak tools, or sideloading utilities (AltStore, Sideloadly)
 * without requiring macOS or official Apple developer certificates.
 */

#ifndef ZAN_IPA_H
#define ZAN_IPA_H

#include <stddef.h>

/* Assemble the .ipa archive.
 *
 *   ipa_path       output .ipa (overwritten)
 *   binary_path    path of the linked Mach-O arm64 binary
 *   app_name       application name (e.g. "MyApp")
 *   bundle_id      bundle identifier (e.g. "dev.zan.myapp", or NULL for default)
 *   display_name   human-readable name (or NULL to default to app_name)
 *   version        short version string, e.g. "1.0.0" (or NULL)
 *
 * Returns 0 on success, nonzero on failure. */
int zan_ipa_build(const char *ipa_path, const char *binary_path,
                  const char *app_name, const char *bundle_id,
                  const char *display_name, const char *version);

#endif /* ZAN_IPA_H */
