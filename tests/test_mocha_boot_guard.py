"""Run the actual generated boot guard against an endian-converted private ELF.
Offline host test only; the converted fixture is not bootable firmware.
"""
import json
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
# Optional argument: experiment directory (default: allocation experiment).
experiment = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/mocha-alloc-experiment"
source = (experiment / "source/ios_kernel/source/ios_net_patches.c").read_text()
guard = source[source.index("static int rtl_boot_word("):source.index("#define NET_CODE_BASE_PHYS_ADDR")]
header = experiment / "source/ios_kernel/source/elf_abi.h"
folder = root / "evidence/firmware-5.5.6/analysis"
image = bytearray((folder / "ios.elf").read_bytes())
assert hashlib.sha256(image).hexdigest() == "8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23"
mapping = json.loads((folder / "map.json").read_text())
fields = struct.unpack_from(">HHIIIIIHHHHHH", image, 16)
struct.pack_into("<HHIIIIIHHHHHH", image, 16, *fields)
for i in range(fields[9]):
    at = fields[4] + fields[8] * i
    struct.pack_into("<8I", image, at, *struct.unpack_from(">8I", image, at))
# Exact signature list of the generated, variant-specific guard.
listed = re.search(r"const u32 addresses\[\] = \{([^}]*)\}", guard).group(1)
addresses = tuple(int(x, 16) for x in listed.split(",") if x.strip())
assert addresses[:7] == (0x12300ae4, 0x12300ae8, 0x12300a74, 0x123e4394, 0x123e4398, 0x123e439c, 0x123e43a0)
net =next(s for s in mapping["segments"] if s["type"] == 1 and s["vaddr"] == 0x12300000)
for address in addresses:
    at = net["offset"] + address - net["vaddr"]
    struct.pack_into("<I", image, at, struct.unpack_from(">I", image, at)[0])
fixture = experiment / "guard-fixture.host-only.bin"
fixture.write_bytes(image)
test = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned char image[32*1024*1024] __attribute__((aligned(64)));
int main(int argc, char **argv) {
    assert(argc == 2 && (uintptr_t)image <= UINT32_MAX-sizeof(image));
    FILE *f = fopen(argv[1], "rb"); assert(f);
    size_t n = fread(image, 1, sizeof(image), f); fclose(f);
    assert(n > 52 && n < sizeof(image));
    u32 base = (u32)(uintptr_t)image;
    assert(rtl_alloc_profile_matches(base));
    Elf32_Ehdr *h = (Elf32_Ehdr *)image;
    Elf32_Phdr *net = NULL;
    for (unsigned i=0; i<h->e_phnum; ++i) {
        Elf32_Phdr *s = (Elf32_Phdr *)(image+h->e_phoff+i*sizeof(*s));
        if (s->p_type == PT_LOAD && s->p_vaddr == 0x12300000) net=s;
    }
    assert(net);
    Elf32_Phdr *bss = NULL;
    for (unsigned i=0; i<h->e_phnum; ++i) {
        Elf32_Phdr *s = (Elf32_Phdr *)(image+h->e_phoff+i*sizeof(*s));
        if (s->p_type==PT_LOAD && s->p_vaddr==0x124c6000) bss=s;
    }
    assert(bss);
    u32 save_bss=bss->p_memsz; bss->p_memsz++;
    assert(!rtl_alloc_profile_matches(base)); bss->p_memsz=save_bss;
    u32 save_pa=bss->p_paddr; bss->p_paddr+=4096;
    assert(!rtl_alloc_profile_matches(base)); bss->p_paddr=save_pa;
    u32 save_filesz=bss->p_filesz; bss->p_filesz=4;
    assert(!rtl_alloc_profile_matches(base)); bss->p_filesz=save_filesz;
    Elf32_Phdr *other = (Elf32_Phdr *)(image+h->e_phoff);
    Elf32_Phdr original_other=*other;
    other->p_type=PT_LOAD; other->p_vaddr=0x1288d040; other->p_memsz=64;
    assert(!rtl_alloc_profile_matches(base)); *other=original_other;
    other->p_type=PT_LOAD; other->p_vaddr=0xfffffff0; other->p_memsz=64;
    assert(!rtl_alloc_profile_matches(base)); *other=original_other;
    const u32 addresses[] = {ADDRESS_LIST};
    for (unsigned i=0; i<sizeof(addresses)/sizeof(addresses[0]); ++i) {
        u32 *word = (u32 *)(image+net->p_offset+addresses[i]-net->p_vaddr);
        *word ^= 1; assert(!rtl_alloc_profile_matches(base)); *word ^= 1;
    }
    h->e_ident[EI_CLASS]=2; assert(!rtl_alloc_profile_matches(base)); h->e_ident[EI_CLASS]=1;
    u32 saved=net->p_filesz; net->p_filesz=3;
    assert(!rtl_alloc_profile_matches(base)); net->p_filesz=saved;
    saved=net->p_flags; net->p_flags=0;
    assert(!rtl_alloc_profile_matches(base)); net->p_flags=saved;
    saved=h->e_phoff; h->e_phoff=0xffffffff;
    assert(!rtl_alloc_profile_matches(base)); h->e_phoff=saved;
    u32 value=0;
    assert(!rtl_boot_word(base,0x12300ae5,&value));
    assert(!rtl_boot_word(base,0x12440000,&value));
    assert(rtl_alloc_profile_matches(base));
    printf("Mocha boot guard: actual generated C, %u signature-word failures, invalid metadata and address boundaries passed (host fixture only)\n",
           (unsigned)(sizeof(addresses)/sizeof(addresses[0])));
    return 0;
}
'''
test = test.replace("ADDRESS_LIST", ",".join("0x%08x" % a for a in addresses))
out = experiment / "guard-test.c"
out.write_text('#include <stdint.h>\ntypedef uint32_t u32;\n#include "' + header.as_posix() + '"\n' + guard + test)
binary = experiment / "guard-test"
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-int-to-pointer-cast", "-fsanitize=address,undefined", "-no-pie", str(out), "-o", str(binary)], check=True)
subprocess.run([str(binary), str(fixture)], check=True)

