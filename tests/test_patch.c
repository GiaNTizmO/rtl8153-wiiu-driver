#define RTL8153_PATCH_TEST 1
#include "../aroma_module/iosu_patch.c"
#include <assert.h>
#include <string.h>
static uint32_t instruction;
static int init_error,read_error,write_error,readback_bad;
static unsigned writes,reads,deinits;
int Mocha_InitLibrary(void) {return init_error;}
void Mocha_DeInitLibrary(void) {deinits++;}
int Mocha_IOSUKernelRead32(uint32_t addr,uint32_t *p)
{assert(addr==GUARD_ADDR);reads++;*p=(readback_bad&&reads>1)?0:instruction;return read_error;}
int Mocha_IOSUKernelWrite32(uint32_t addr,uint32_t val)
{assert(addr==GUARD_ADDR&&val==GUARD_NOP);writes++;if(!write_error)instruction=val;return write_error;}
static void setup(uint32_t word)
{instruction=word;init_error=read_error=write_error=readback_bad=0;writes=reads=deinits=0;}
int main(void)
{
    char why[200];
    setup(GUARD_BEQ);assert(iosu_patch_apply(why,sizeof(why))==0&&writes==1&&reads==2&&deinits==1);
    setup(GUARD_NOP);assert(iosu_patch_apply(why,sizeof(why))==0&&writes==0&&deinits==1);
    setup(0x12345678);assert(iosu_patch_apply(why,sizeof(why))==-1&&!writes&&strstr(why,"unexpected"));
    setup(GUARD_BEQ);init_error=1;assert(iosu_patch_apply(why,sizeof(why))==-1&&!writes&&!reads&&!deinits);
    setup(GUARD_BEQ);read_error=1;assert(iosu_patch_apply(why,sizeof(why))==-1&&!writes&&deinits==1);
    setup(GUARD_BEQ);write_error=1;assert(iosu_patch_apply(why,sizeof(why))==-1&&writes==1&&deinits==1);
    setup(GUARD_BEQ);readback_bad=1;assert(iosu_patch_apply(why,sizeof(why))==-1&&strstr(why,"mismatch"));
    puts("IOSU patch: exact original opcode, already patched, mismatch refusal and IPC/readback failures passed");
}
