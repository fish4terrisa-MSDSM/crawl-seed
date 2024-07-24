#ifndef __included_crawl_compile_flags_h
#define __included_crawl_compile_flags_h

#define CRAWL_CFLAGS "-O2 -pipe -Wall -Wformat-security -Wundef -Wextra -Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-type-limits -Wno-uninitialized -Wno-array-bounds -Wmissing-declarations -Wredundant-decls -Wno-parentheses -Wwrite-strings -Wshadow -pedantic -Wuninitialized -Iutil -I. -isystem /usr/include/lua5.1 -DFULLDEBUG -DWIZARD -DASSERTS -DCLUA_BINDINGS -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600"
#define CRAWL_LDFLAGS "-rdynamic -fuse-ld=gold -O2"
#define CRAWL_HOST "x86_64-pc-linux-gnu"
#define CRAWL_ARCH "x86_64-pc-linux-gnu"

#endif

