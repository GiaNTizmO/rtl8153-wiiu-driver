#include "../native/ios_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){RtlIosFirmwareProfile p=*rtl_ios_profile_556();assert(rtl_ios_profile_validate(&p)==0);assert(rtl_ios_profile_validate_callbacks(&p,p.text_start,p.text_end)==0);p.old_ops=0x124c6000u;assert(rtl_ios_profile_validate(&p)!=0);p=*rtl_ios_profile_556();p.eth_tx++;assert(rtl_ios_profile_validate_callbacks(&p,p.text_start,p.text_end)!=0);puts("native firmware profile: segments, callback ranges and fail-closed checks passed");}
