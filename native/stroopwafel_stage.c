#include "stroopwafel_stage.h"
#include <string.h>
#define RTL_STROOPWAFEL_API 0x010000u
#define RTL_ENTRY_MARKER 0x52544c50u
static int ready_ops(const RtlIosStroopwafelOps*o){return o&&o->api_version&&o->map&&o->write&&o->execute;}
static int advance(RtlIosStroopwafelStage*s,enum RtlIosLoaderPhase p,const RtlIosMapping*m,const RtlIosEntryCheck*e){return rtl_ios_loader_advance(&s->loader,p,m,e);}
int rtl_ios_stroopwafel_stage_prepare(RtlIosStroopwafelStage*s,const RtlIosStroopwafelOps*o,const RtlIosMapping*m,uint32_t image_bytes,uint32_t entry,uint32_t result_word)
{
    uint32_t version=0;
    RtlIosEntryCheck check={m ? m->virtual_start : 0,image_bytes,entry,result_word,0};
    if(!s || !ready_ops(o) || rtl_ios_validate_entry_layout(m,&check) ||
       o->api_version(o->user,&version) || version!=RTL_STROOPWAFEL_API) return -1;
    memset(s,0,sizeof(*s));
    s->expected_api=RTL_STROOPWAFEL_API;
    s->profile=*rtl_ios_profile_556();
    if(rtl_ios_profile_validate(&s->profile) ||
       rtl_ios_profile_validate_callbacks(&s->profile,s->profile.text_start,s->profile.text_end)) return -1;
    s->loader.backend=RTL_IOS_LOADER_STROOPWAFEL;
    s->loader.phase=RTL_IOS_LOADER_NEW;
    s->loader.entry=check;
    if(advance(s,RTL_IOS_LOADER_ALLOCATED,NULL,NULL) || o->map(o->user,m) ||
       advance(s,RTL_IOS_LOADER_MAPPED,m,NULL)) return -1;
    return 0;
}
int rtl_ios_stroopwafel_stage_copy(RtlIosStroopwafelStage*s,const RtlIosStroopwafelOps*o,uint32_t address,const void*image,uint32_t bytes){if(!s||!ready_ops(o)||!image||s->loader.phase!=RTL_IOS_LOADER_MAPPED||address!=s->loader.mapping.virtual_start||bytes!=s->loader.entry.image_bytes)return -1;if(o->write(o->user,address,image,bytes))return -1;if(advance(s,RTL_IOS_LOADER_COPIED,NULL,NULL)||advance(s,RTL_IOS_LOADER_CACHE_CLEAN,NULL,NULL))return -1;return 0;}
int rtl_ios_stroopwafel_stage_execute_probe(RtlIosStroopwafelStage*s,const RtlIosStroopwafelOps*o,uint32_t entry,uint32_t result_word,uint32_t*marker){uint32_t config=result_word,out=0;if(!s||!ready_ops(o)||!marker||s->loader.phase!=RTL_IOS_LOADER_CACHE_CLEAN||entry!=s->loader.entry.entry||result_word!=s->loader.entry.result_word)return -1;if(o->execute(o->user,entry,&config,sizeof(config),&out,sizeof(out))||out!=RTL_ENTRY_MARKER)return -1;*marker=out;s->loader.entry.marker=out;return advance(s,RTL_IOS_LOADER_ENTRY_VERIFIED,NULL,&s->loader.entry);}
int rtl_ios_stroopwafel_stage_arm(RtlIosStroopwafelStage*s){if(!s||s->loader.phase!=RTL_IOS_LOADER_ENTRY_VERIFIED||rtl_ios_validate_entry_check(&s->loader))return -1;return advance(s,RTL_IOS_LOADER_ARMED,NULL,NULL);}
