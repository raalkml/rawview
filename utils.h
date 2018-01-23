#ifndef _UTILS_H_
#define _UTILS_H_ 1

/* offsetof and container_of taken from Linux kernel */

#if GCC_VERSION >= 40000
#define __compiler_offsetof(a, b) __builtin_offsetof(a, b)
#endif

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(type, member)	__compiler_offsetof(type, member)
#else
#define offsetof(type, member)	((size_t)&((type *)0)->member)
#endif

#define container_of(ptr, type, member) \
	(((type *)((void *)(ptr) - offsetof(type, member))))

#endif /* _UTILS_H_ */
