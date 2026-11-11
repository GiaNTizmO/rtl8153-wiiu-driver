#include "ops_table.h"
#include <string.h>
static void put_be32(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static uint32_t get_be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
int rtl_ios_build_ops(void *out,size_t bytes,const RtlIosOpsSpec *s)
{
    if(!out||!s||bytes<sizeof(IosEthDriverOps)||!s->controller_bytes||s->tx_capacity>0x4000||
       !s->tx_header_bytes||!s->tx_pending||!s->rx_capacity||!s->rx_pending||!s->open||!s->close||
       !s->get_link||!s->send_async||!s->receive_async||!s->completion||!s->post_link_interrupt)return -1;
    uint8_t *p=out;memset(p,0,sizeof(IosEthDriverOps));put_be32(p,0x0bda8153u);
    put_be32(p+4,s->controller_bytes);put_be32(p+8,s->tx_capacity);put_be32(p+12,s->tx_header_bytes);
    put_be32(p+16,s->tx_pending);put_be32(p+20,s->rx_capacity);put_be32(p+28,s->rx_pending);
    put_be32(p+32,s->open);put_be32(p+36,s->close);put_be32(p+40,s->get_link);
    put_be32(p+44,s->send_async);put_be32(p+48,s->receive_async);put_be32(p+52,s->completion);
    put_be32(p+56,s->post_link_interrupt);return 0;
}
int rtl_ios_validate_ops(const void *table,size_t bytes,uint32_t expected,uint32_t start,uint32_t end)
{
    if(!table||bytes<sizeof(IosEthDriverOps)||start>=end)return -1;
    const uint8_t *p=table;
    if(get_be32(p)!=expected||expected!=0x0bda8153u)return -1;
    for(unsigned i=8;i<15;++i){uint32_t v=get_be32(p+i*4);if(v<start||v>=end||(v&3))return -1;}
    return 0;
}
