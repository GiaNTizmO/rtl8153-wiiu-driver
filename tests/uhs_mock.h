#ifndef RTL8153_UHS_MOCK_H
#define RTL8153_UHS_MOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
typedef struct { int unused; } UhsHandle;
typedef struct { unsigned controller_num; void *buffer; unsigned buffer_size; } UhsConfig;
typedef struct { uint8_t bLength,bEndpointAddress,bmAttributes; uint16_t wMaxPacketSize; } UhsEndpointDescriptor;
typedef struct { uint32_t if_handle; UhsEndpointDescriptor in_endpoints[16],out_endpoints[16]; } UhsInterfaceProfile;
typedef struct { unsigned match_params,vid,pid; } UhsInterfaceFilter;
typedef int64_t OSTime;
typedef struct { int signalled; } OSEvent;
#define FALSE 0
#define OS_EVENT_MODE_AUTO 0
#define MATCH_DEV_VID 1
#define MATCH_DEV_PID 2
#define UHS_ADMIN_EP_ENABLE 1
#define UHS_ADMIN_EP_DISABLE 2
#define UHS_ADMIN_EP_CANCEL 3
#define OSMillisecondsToTicks(ms) (ms)
#define OSMicrosecondsToTicks(us) (us)
#define OSTicksToMilliseconds(ticks) (ticks)
void DCFlushRange(void *,size_t);
void DCInvalidateRange(void *,size_t);
void OSSleepTicks(uint64_t);
OSTime OSGetTime(void);
void OSInitEvent(OSEvent *,int,int);
void OSSignalEvent(OSEvent *);
int OSWaitEventWithTimeout(OSEvent *,uint64_t);
int32_t UhsClientOpen(UhsHandle *,UhsConfig *);
int32_t UhsClientClose(UhsHandle *);
int32_t UhsQueryInterfaces(UhsHandle *,UhsInterfaceFilter *,UhsInterfaceProfile *,int);
int32_t UhsAcquireInterface(UhsHandle *,uint32_t,void *,void *);
int32_t UhsReleaseInterface(UhsHandle *,uint32_t,bool);
int32_t UhsAdministerEndpoint(UhsHandle *,uint32_t,int,uint32_t,uint32_t,uint32_t);
int32_t UhsSubmitControlRequest(UhsHandle *,uint32_t,void *,uint8_t,uint8_t,uint16_t,uint16_t,uint16_t,int32_t);
int32_t UhsSubmitBulkRequest(UhsHandle *,uint32_t,uint8_t,int,void *,int32_t,int32_t);
typedef void (*AxUhsBulkAsyncCallback)(void *,int,uint32_t,uint8_t,int,const void *,size_t);
int32_t UhsSubmitBulkRequestAsync(UhsHandle *,uint32_t,uint8_t,int,const void *,size_t,void *,AxUhsBulkAsyncCallback);
void rtl_log(const char *,...) __attribute__((format(printf,1,2)));
#define WHBLogPrintf rtl_log
int iosu_patch_apply(char *,unsigned);
#endif
