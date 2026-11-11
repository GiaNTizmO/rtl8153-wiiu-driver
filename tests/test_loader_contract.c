#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../native/loader_contract.h"

int main(void)
{
    RtlIosLoaderState s = {RTL_IOS_LOADER_STROOPWAFEL,
                           RTL_IOS_LOADER_NEW, {0}, {0}};
    RtlIosMapping m = {0x20000000u, 0x12200000u, 0x2000u,
                       RTL_IOS_DOMAIN_NET,
                       RTL_IOS_MAP_READ | RTL_IOS_MAP_WRITE | RTL_IOS_MAP_EXEC, 0};
    RtlIosEntryCheck e = {0x12200000u, 24u, 0x12200000u,
                          0x12201000u, 0x52544c50u};

    assert(rtl_ios_loader_advance(&s, RTL_IOS_LOADER_ALLOCATED, NULL, NULL) == 0);
    assert(rtl_ios_loader_advance(&s, RTL_IOS_LOADER_MAPPED, &m, NULL) == 0);
    assert(rtl_ios_loader_advance(&s, RTL_IOS_LOADER_COPIED, NULL, NULL) == 0);
    assert(rtl_ios_loader_advance(&s, RTL_IOS_LOADER_CACHE_CLEAN, NULL, NULL) == 0);
    assert(rtl_ios_loader_advance(&s, RTL_IOS_LOADER_ENTRY_VERIFIED, NULL, &e) == 0);
    assert(rtl_ios_loader_advance(&s, RTL_IOS_LOADER_ARMED, NULL, NULL) == 0);
    assert(s.phase == RTL_IOS_LOADER_ARMED);

    m.domain = 1;
    assert(rtl_ios_validate_mapping(&m) != 0);
    m.domain = RTL_IOS_DOMAIN_NET;
    m.flags &= ~RTL_IOS_MAP_EXEC;
    assert(rtl_ios_validate_mapping(&m) != 0);
    m.flags |= RTL_IOS_MAP_EXEC;
    m.flags |= 0x80000000u;
    assert(rtl_ios_validate_mapping(&m) != 0);
    m.flags &= ~0x80000000u;
    assert(rtl_ios_validate_entry_layout(&m,&e)==0);
    RtlIosEntryCheck bad=e;
    bad.result_word=e.image_start;
    assert(rtl_ios_validate_entry_layout(&m,&bad)!=0);
    bad=e; bad.result_word++;
    assert(rtl_ios_validate_entry_layout(&m,&bad)!=0);
    bad=e; bad.result_word=m.virtual_start+m.bytes-4;
    assert(rtl_ios_validate_entry_layout(&m,&bad)==0);
    bad.result_word+=4;
    assert(rtl_ios_validate_entry_layout(&m,&bad)!=0);
    bad=e; bad.image_bytes=1;
    assert(rtl_ios_validate_entry_layout(&m,&bad)!=0);
    bad=e; bad.image_bytes=5; bad.entry=e.image_start+4;
    assert(rtl_ios_validate_entry_layout(&m,&bad)!=0);
    bad=e; bad.image_bytes=m.bytes;
    assert(rtl_ios_validate_entry_layout(&m,&bad)!=0);
    s.phase=RTL_IOS_LOADER_CACHE_CLEAN;
    RtlIosLoaderState before=s;
    bad=e; bad.marker=0;
    assert(rtl_ios_loader_advance(&s,RTL_IOS_LOADER_ENTRY_VERIFIED,NULL,&bad)!=0);
    assert(memcmp(&s,&before,sizeof(s))==0);
    bad=e; bad.result_word=e.image_start;
    assert(rtl_ios_loader_advance(&s,RTL_IOS_LOADER_ENTRY_VERIFIED,NULL,&bad)!=0);
    assert(memcmp(&s,&before,sizeof(s))==0);
    s.backend=RTL_IOS_LOADER_NONE; s.phase=RTL_IOS_LOADER_NEW;
    assert(rtl_ios_loader_advance(&s,RTL_IOS_LOADER_ALLOCATED,NULL,NULL)!=0);
    s.backend=RTL_IOS_LOADER_MOCHA;
    assert(rtl_ios_loader_advance(&s,RTL_IOS_LOADER_ALLOCATED,NULL,NULL)==0);
    puts("native loader contract: domain, page, phase, entry and fail-closed checks passed");
    return 0;
}
