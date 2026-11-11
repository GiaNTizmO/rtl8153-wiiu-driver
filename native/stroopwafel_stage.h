#ifndef RTL8153_STROOPWAFEL_STAGE_H
#define RTL8153_STROOPWAFEL_STAGE_H
#include "loader_contract.h"
#include "ios_profile.h"
#include <stdint.h>
typedef struct { int (*api_version)(void *user,uint32_t *version); int (*map)(void *user,const RtlIosMapping *mapping); int (*write)(void *user,uint32_t address,const void *data,uint32_t bytes); int (*execute)(void *user,uint32_t entry,const void *config,uint32_t config_bytes,void *output,uint32_t output_bytes); void *user; } RtlIosStroopwafelOps;
typedef struct { RtlIosLoaderState loader; RtlIosFirmwareProfile profile; uint32_t expected_api; } RtlIosStroopwafelStage;
int rtl_ios_stroopwafel_stage_prepare(RtlIosStroopwafelStage *stage,const RtlIosStroopwafelOps *ops,const RtlIosMapping *mapping,uint32_t image_bytes,uint32_t entry,uint32_t result_word);
int rtl_ios_stroopwafel_stage_copy(RtlIosStroopwafelStage *stage,const RtlIosStroopwafelOps *ops,uint32_t image_address,const void *image,uint32_t image_bytes);
int rtl_ios_stroopwafel_stage_execute_probe(RtlIosStroopwafelStage *stage,const RtlIosStroopwafelOps *ops,uint32_t entry,uint32_t result_word,uint32_t *marker);
int rtl_ios_stroopwafel_stage_arm(RtlIosStroopwafelStage *stage);
#endif
