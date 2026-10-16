/* Force-included (-include) when compiling the C runtime remainder for
 * the aarch64-linux target: declares the shimmed macOS spellings so
 * zanstubs.c compiles clean against musl (implicit-declaration is an
 * error there), before its own includes run. Definitions live in
 * zanlinuxshims.c. */

#ifndef ZAN_LINUX_PRELUDE_H
#define ZAN_LINUX_PRELUDE_H

#include <stdint.h>
#include <pthread.h>

int *__error(void);
void arc4random_buf(void *buf, unsigned long n);
int pthread_threadid_np(pthread_t t, uint64_t *tid);
int _NSGetExecutablePath(char *buf, unsigned int *bufsize);
long long OSAtomicAdd64Barrier(long long v, long long *p);
int OSAtomicCompareAndSwap64Barrier(long long o, long long n, long long *p);
void os_unfair_lock_lock(void *p);
void os_unfair_lock_unlock(void *p);
int mach_vm_read_overwrite(unsigned long target, unsigned long long addr,
    unsigned long long size, unsigned long long outbuf,
    unsigned long long *outsize);
extern unsigned long mach_task_self_;
unsigned char *CC_MD5(const void *data, unsigned long len, unsigned char *md);
unsigned char *CC_SHA1(const void *data, unsigned long len, unsigned char *md);
unsigned char *CC_SHA256(const void *data, unsigned long len,
    unsigned char *md);
void CCHmac(int alg, const void *key, unsigned long keyLen,
    const void *data, unsigned long dataLen, void *macOut);

#endif /* ZAN_LINUX_PRELUDE_H */
