#include <stdint.h>
#define MOCHA_RESULT_SUCCESS 0
int Mocha_InitLibrary(void);
void Mocha_DeInitLibrary(void);
int Mocha_IOSUKernelRead32(uint32_t,uint32_t *);
int Mocha_IOSUKernelWrite32(uint32_t,uint32_t);
