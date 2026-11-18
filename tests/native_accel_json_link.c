/* Production-archive consumer: deliberately reference only JSON entry points.
 * The link test disables section GC, so any extracted crypto family remains
 * visible to nm even if none of its functions is called. */
#include "../src/accel/zan_hw_accel.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t text[97];
    memset(text, ' ', sizeof(text));
    text[96] = 'x';
    if (zan_hw_json_skip_whitespace(text, 0, sizeof(text)) != 96) return 1;
    memset(text, 'x', sizeof(text));
    text[80] = '"';
    if (zan_hw_json_scan_string(text, 0, sizeof(text)) != 80) return 2;
    text[50] = '\\';
    if (zan_hw_json_scan_string(text, 0, sizeof(text)) != -51) return 3;
    if (zan_hw_json_skip_whitespace(NULL, 2, 2) != 2) return 4;
    if (zan_hw_json_scan_string(NULL, 2, 2) != 2) return 5;
    puts("JSON archive isolation OK");
    return 0;
}
