/* Shadow header for <net/if_dl.h> (macOS link-layer addresses), used only
 * when compiling the C runtime remainder for aarch64-linux (put this
 * directory first on the include path). musl has no AF_LINK; getifaddrs
 * hands out AF_PACKET sockaddr_ll for link-layer entries instead.
 *
 * The only consumers are zanhost.c's MAC-address extraction
 * (sa_family check + LLADDR + sdl_alen), so the macOS struct is defined
 * as a byte-exact VIEW of sockaddr_ll:
 *
 *   offset  sockaddr_ll field        view field
 *   0       sll_family (u16)         sdl_family_pad
 *   2       sll_protocol (u16)       sdl_protocol_pad
 *   4       sll_ifindex (s32)        sdl_index
 *   8       sll_hatype (u16)         sdl_type_pad
 *   10      sll_pkttype (u8)         sdl_slen_pad
 *   11      sll_halen  (u8)          sdl_alen
 *   12..19  sll_addr[8]              sdl_data[8]
 */

#ifndef ZAN_COMPAT_IF_DL_H
#define ZAN_COMPAT_IF_DL_H

#include <netpacket/packet.h>

#if !defined(AF_LINK)
#define AF_LINK AF_PACKET
#endif

struct sockaddr_dl {
    unsigned short sdl_family_pad;
    unsigned short sdl_protocol_pad;
    int sdl_index;
    unsigned short sdl_type_pad;
    unsigned char sdl_slen_pad;
    unsigned char sdl_alen;
    unsigned char sdl_data[8];
};

#define LLADDR(s) ((void *)(s)->sdl_data)

#endif /* ZAN_COMPAT_IF_DL_H */
