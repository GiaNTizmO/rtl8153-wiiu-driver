#include <assert.h>
#include <stdio.h>
#define LWIP_PLATFORM_DIAG(x) do {printf x;} while(0)
#define LWIP_PLATFORM_ASSERT(x) do {fprintf(stderr,"%s\n",x);assert(0);} while(0)
