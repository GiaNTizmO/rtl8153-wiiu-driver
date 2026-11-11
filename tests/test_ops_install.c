#include "../native/ops_table.h"
#include "../native/discovery_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t mem[4]; static int fail_after=-1,calls;
static int rd(void *u,uint32_t a,uint32_t *v){assert(u==mem);if(a==0x124ce314)*v=mem[0];else if(a==0x124ce318)*v=mem[1];else return -1;return 0;}
static int wr(void *u,uint32_t a,uint32_t v){assert(u==mem);if(fail_after>=0&&calls++==fail_after){fail_after=-1;return -1;}if(a==0x124ce314)mem[0]=v;else if(a==0x124ce318)mem[1]=v;else return -1;return 0;}
int main(void)
{
 uint8_t table[60],bad[60];
 RtlIosOpsSpec s={0x534,0x4000,8,8,0x4000,2,0x12500100,0x12500140,0x12500180,0x125001c0,0x12500200,0x12500240,0x12500280};
 assert(rtl_ios_build_ops(table,sizeof(table),&s)==0&&table[0]==0x0b&&table[1]==0xda&&table[2]==0x81&&table[3]==0x53);
 assert(rtl_ios_validate_ops(table,sizeof(table),0x0bda8153,0x12500000,0x12510000)==0);
 memcpy(bad,table,sizeof(bad));bad[0]=0;assert(rtl_ios_validate_ops(bad,sizeof(bad),0x0bda8153,0x12500000,0x12510000)<0);
 s.open=0;assert(rtl_ios_build_ops(table,sizeof(table),&s)<0);s.open=0x12500100;
 mem[0]=0x12456dd0;mem[1]=0;RtlIosDiscoveryTxn t;
 assert(rtl_ios_discovery_prepare(&t,mem,rd,wr,0x124ce314,0x12456dd0,0x12500000,0x124c6000,0x1288d028)==0);
 assert(rtl_ios_discovery_commit(&t)==0&&mem[0]==0x12500000&&mem[1]==1);
 assert(rtl_ios_discovery_commit(&t)<0);assert(rtl_ios_discovery_rollback(&t)==0&&mem[0]==0x12456dd0&&mem[1]==0);
 mem[0]=0x12456dd0;mem[1]=0;fail_after=1;calls=0;assert(rtl_ios_discovery_prepare(&t,mem,rd,wr,0x124ce314,0x12456dd0,0x12500000,0x124c6000,0x1288d028)==0);assert(rtl_ios_discovery_commit(&t)<0&&mem[0]==0x12456dd0&&mem[1]==0);
 puts("native ops install: BE RTL table, range, sentinel, readback, rollback and fail-closed guards passed");
}
