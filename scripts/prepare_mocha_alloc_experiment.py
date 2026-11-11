"""Prepare an isolated source experiment under build/. Never installs it.

Run only on a fresh git archive of the recorded Mocha source. Source originals
and SDK directories remain unchanged. The hook runs on IOS reload only.
"""
import argparse
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path, old, new):
    source = path.read_text()
    if source.count(old) != 1:
        raise ValueError(f"source context mismatch: {path.name}")
    path.write_text(source.replace(old, new))


# "alloc" reproduces the console-run allocation-only experiment unchanged.
# "entry" adds one cache-cleaned heap-page call; see docs/MOCHA_ENTRY_DIAGNOSTIC.md.
VARIANTS = {
    "alloc": dict(
        files=(("mocha_alloc_probe.c", "rtl_alloc_probe.c"),
               ("mocha_alloc_status.h", "mocha_alloc_status.h"),
               ("mocha_alloc_probe_entry.S", "rtl_alloc_entry.s")),
        imports="",
        hook="rtl_mocha_net_init_alloc_probe",
        addresses="", expected="",
        comment="Experimental allocation-only probe"),
    "entry": dict(
        files=(("mocha_entry_probe.c", "rtl_entry_probe.c"),
               ("mocha_entry_status.h", "mocha_entry_status.h"),
               ("mocha_entry_probe_entry.S", "rtl_entry_entry.s"),
               ("mocha_heap_entry_blob.S", "rtl_heap_entry_blob.s")),
        imports="PROVIDE(rtl_net_flush_dcache = 0x123e44e4);\n",
        hook="rtl_mocha_net_init_entry_probe",
        # FlushDCache svc 0x52 stub; local heap creation (ldr r0,=0x124c6060;
        # mov r1,#0x40000; bl svc 0x24) and its literal pool word.
        addresses=", 0x123e44e4, 0x123e44e8, 0x12300aac, 0x12300ab0, 0x12300ab4, 0x12300c18",
        expected=", 0xe7f052f0, 0xe12fff1e, 0xe59f0164, 0xe3a01701, 0xeb038e2e, 0x124c6060",
        comment="Experimental single heap-page entry probe"),
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("--variant", choices=sorted(VARIANTS), default="alloc")
    a = p.parse_args()
    v = VARIANTS[a.variant]
    out = a.source.resolve()
    if not out.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("experimental source must remain under project build/")
    net = out / "source/ios_net"
    kernel = out / "source/ios_kernel/source"
    # section_write takes a pointer, not the literal value 0xffffffff.
    # Upstream's reload-only MCP timeout entry otherwise dereferences -1.
    replace_once(kernel / "ios_mcp_patches_asm.s",
                 "     .word 0x05022474,      0xFFFFFFFF,                             4",
                 "     .word 0x05022474,      rtl_timeout_word,                      4")
    with (kernel / "ios_mcp_patches_asm.s").open("a") as f:
        f.write("\n.align 2\nrtl_timeout_word:\n    .word 0xffffffff\n")
    for original, name in v["files"]:
        shutil.copyfile(ROOT / "native" / original, net / "source" / name)
    # Absolute firmware import values are checked against the owner ELF by
    # check_mocha_alloc_candidate.py after linking this actual experiment.
    with (net / "imports.ld").open("a") as f:
        f.write("\nPROVIDE(rtl_net_original_init = 0x12300a74);\n"
                "PROVIDE(rtl_net_alloc = 0x123e4394);\n"
                "PROVIDE(rtl_net_free = 0x123e439c);\n"
                "PROVIDE(rtl_net_printf = 0x123ccfbc);\n" + v["imports"])
    replace_once(net / "link.ld", "    /DISCARD/ : {", """
    .rtl_alloc_status 0x1288d040 (NOLOAD) : { *(.rtl_alloc_status) }
    .rtl_unexpected_bss (NOLOAD) : { *(.bss*) *(COMMON) }
    /DISCARD/ : {""")
    with (net / "link.ld").open("a") as f:
        f.write('\nASSERT(SIZEOF(.rtl_unexpected_bss) == 0, "unreserved candidate BSS");\n')
        f.write('ASSERT(SIZEOF(.rtl_alloc_status) == 64, "status ABI mismatch");\n')
    path = kernel / "ios_net_patches.c"
    helper = """
/* """ + v["comment"] + """. Not deployed or enabled live.
 * Inspect words in the already-loaded boot ELF before installing the hook.
 * The enclosing Mocha loader owns/validates the ELF buffer lifetime/size. */
static int rtl_boot_word(u32 image, u32 address, u32 *word) {
    Elf32_Ehdr *h = (Elf32_Ehdr *) image;
    if (!IS_ELF(*h) || h->e_type != ET_EXEC || h->e_machine != EM_ARM ||
        h->e_ident[EI_CLASS] != 1 || h->e_ident[EI_DATA] != ELFDATA2MSB ||
        h->e_phentsize != sizeof(Elf32_Phdr) || h->e_phnum > 128 ||
        h->e_phoff > 0x10000 || (address & 3)) return 0;
    for (u32 i = 0; i < h->e_phnum; ++i) {
        Elf32_Phdr *s = (Elf32_Phdr *)(image + h->e_phoff + i * h->e_phentsize);
        if (s->p_type != PT_LOAD || s->p_vaddr != 0x12300000 ||
            ((s->p_flags >> 20) & 255) != 7 ||
            s->p_filesz > 0x140000 || s->p_filesz < 4 ||
            s->p_offset > 0x02000000 || address < s->p_vaddr ||
            address - s->p_vaddr > s->p_filesz - 4) continue;
        *word = *(u32 *)(image + s->p_offset + address - s->p_vaddr);
        return 1;
    }
    return 0;
}
static int rtl_alloc_profile_matches(u32 image) {
    const u32 addresses[] = {0x12300ae4, 0x12300ae8, 0x12300a74,
                            0x123e4394, 0x123e4398, 0x123e439c, 0x123e43a0""" + v["addresses"] + """};
    const u32 expected[] = {0xebffffe2, 0xe3500000, 0xe52de004,
                           0xe7f028f0, 0xe12fff1e, 0xe7f029f0, 0xe12fff1e""" + v["expected"] + """};
    for (u32 i = 0; i < sizeof(addresses)/sizeof(addresses[0]); ++i) {
        u32 word = 0;
        if (!rtl_boot_word(image, addresses[i], &word) || word != expected[i]) return 0;
    }
    /* The status mailbox is owned only after this exact NET BSS is extended.
     * Require a unique original segment and no intersecting loaded region. */
    Elf32_Ehdr *h = (Elf32_Ehdr *) image;
    u32 found = 0;
    for (u32 i = 0; i < h->e_phnum; ++i) {
        Elf32_Phdr *s = (Elf32_Phdr *)(image + h->e_phoff + i * h->e_phentsize);
        if (s->p_type != PT_LOAD) continue;
        if (s->p_vaddr == 0x124c6000) {
            if (s->p_filesz || s->p_memsz != 0x3c7028 ||
                s->p_flags != 0x700006 || s->p_paddr != s->p_vaddr) return 0;
            ++found;
        }
        if (s->p_memsz > 0xffffffffu - s->p_vaddr) return 0;
        if (s->p_vaddr < 0x1288d080 && s->p_vaddr + s->p_memsz > 0x1288d028)
            return 0;
    }
    return found == 1;
}
"""
    replace_once(path, '#include "types.h"', '#include "types.h"\n#include "elf_abi.h"\n' + helper)
    replace_once(path, "void net_run_patches(u32 ios_elf_start) {",
                 "void net_run_patches(u32 ios_elf_start) {\n"
                 "    int rtl_alloc_enable = rtl_alloc_profile_matches(ios_elf_start);")
    kernel_path = kernel / "kernel_patches.c"
    replace_once(kernel_path, '#include "types.h"',
                 '#include "types.h"\n\n'
                 '/* Reload-path diagnostics only; no USB/network side effects. */\n'
                 'volatile u32 rtl_mocha_reload_diag[4] __attribute__((section(".data"))) = {0,0,0,0};\n')
    replace_once(kernel_path, "void kernel_launch_ios(u32 launch_address, u32 L, u32 C, u32 H) {",
                 "void kernel_launch_ios(u32 launch_address, u32 L, u32 C, u32 H) {\n"
                 "    rtl_mocha_reload_diag[0] = 0x524c4445; /* RLDE: entered */")
    replace_once(kernel_path,
                 "        u32 ios_elf_start = launch_address + 0x804 - 0x300;",
                 "        rtl_mocha_reload_diag[1] = 0x48445250; /* HDRP: header accepted */\n\n"
                 "        u32 ios_elf_start = launch_address + 0x804 - 0x300;")
    replace_once(kernel_path,
                 "        net_run_patches(ios_elf_start);",
                 "        net_run_patches(ios_elf_start);\n"
                 "        rtl_mocha_reload_diag[2] = 0x4e455450; /* NETP: patcher returned */\n"
                 "        /* kernel_run_patches copied kernel before NETP; persist final diagnostics. */\n"
                 "        section_write(ios_elf_start, (u32)rtl_mocha_reload_diag,\n"
                 "                      (const void *)rtl_mocha_reload_diag, sizeof(rtl_mocha_reload_diag));")
    # Append at the end of net_run_patches only; original DLP patches remain.
    original = path.read_text()
    closing = original.rfind("}")
    if closing < 0: raise ValueError("missing NET patch function")
    original = original[:closing] + """
    if (rtl_alloc_enable) {
        /* Explicitly extend NET BSS to own the mailbox, never live RAM. */
        section_write_bss(ios_elf_start, 0x1288d040, 64);
        section_write_word(ios_elf_start, 0x12300ae4,
                           ARM_BL(0x12300ae4, """ + v["hook"] + """));
    }
""" + original[closing:]
    path.write_text(original)
    print(f"Prepared isolated {a.variant} Mocha source; no live patch or upload")


if __name__ == "__main__": main()
