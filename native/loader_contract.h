#ifndef RTL8153_LOADER_CONTRACT_H
#define RTL8153_LOADER_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#define RTL_IOS_PAGE_SIZE 0x1000u
#define RTL_IOS_DOMAIN_NET 7u

enum RtlIosLoaderBackend {
    RTL_IOS_LOADER_NONE = 0,
    RTL_IOS_LOADER_STROOPWAFEL = 1,
    RTL_IOS_LOADER_MOCHA = 2,
};

enum RtlIosMappingFlags {
    RTL_IOS_MAP_READ = 1u << 0,
    RTL_IOS_MAP_WRITE = 1u << 1,
    RTL_IOS_MAP_EXEC = 1u << 2,
    RTL_IOS_MAP_DMA = 1u << 3,
};

typedef struct {
    uint32_t physical_start;
    uint32_t virtual_start;
    uint32_t bytes;
    uint32_t domain;
    uint32_t flags;
    uint32_t cached;
} RtlIosMapping;

typedef struct {
    uint32_t image_start;
    uint32_t image_bytes;
    uint32_t entry;
    uint32_t result_word;
    uint32_t marker;
} RtlIosEntryCheck;

enum RtlIosLoaderPhase {
    RTL_IOS_LOADER_NEW = 0,
    RTL_IOS_LOADER_ALLOCATED,
    RTL_IOS_LOADER_MAPPED,
    RTL_IOS_LOADER_COPIED,
    RTL_IOS_LOADER_CACHE_CLEAN,
    RTL_IOS_LOADER_ENTRY_VERIFIED,
    RTL_IOS_LOADER_ARMED,
};

typedef struct {
    enum RtlIosLoaderBackend backend;
    enum RtlIosLoaderPhase phase;
    RtlIosMapping mapping;
    RtlIosEntryCheck entry;
} RtlIosLoaderState;

int rtl_ios_validate_mapping(const RtlIosMapping *mapping);
/* Geometry only: does not prove execute permissions, cache state or entry. */
int rtl_ios_validate_entry_layout(const RtlIosMapping *mapping,
                                  const RtlIosEntryCheck *entry);
int rtl_ios_validate_entry_check(const RtlIosLoaderState *state);
int rtl_ios_loader_advance(RtlIosLoaderState *state,
                           enum RtlIosLoaderPhase next,
                           const RtlIosMapping *mapping,
                           const RtlIosEntryCheck *entry);

#endif
