"""Bundle local allocation experiment and verified restoration file for review.
This is not an installer. It reads no OTP, firmware image or console memory.
"""
import hashlib
import json
import shutil
import subprocess
import zipfile
from pathlib import Path
from compare_mocha_rpx import compare
ROOT=Path(__file__).resolve().parents[1]
EXPECTED_ORIGINAL="16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529"

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    project=ROOT/"build/mocha-alloc-experiment"
    original=ROOT/"evidence/2026-11-09/mocha-installed/00_mocha.rpx"
    candidate=project/"00_mocha.rpx"
    if digest(original)!=EXPECTED_ORIGINAL:raise ValueError("unverified recovery file")
    report=compare(candidate,project)
    if not all(v["exact_matches"] for v in report["embedded_payloads"].values()):
        raise ValueError("RPX embeds different ARM payloads")
    review=json.loads((project/"source/ios_net/ios_net.review.json").read_text())
    if review["activation_allowed"] is not False: raise ValueError("review gate missing")
    if digest(project/"source/ios_net/ios_net.elf")!=review["candidate_sha256"]:
        raise ValueError("stale ELF review")
    output=ROOT/"dist/mocha-allocation-review"
    output.mkdir(parents=True,exist_ok=True)
    originals=output/"restore"; originals.mkdir(exist_ok=True)
    experiments=output/"experiment"; experiments.mkdir(exist_ok=True)
    shutil.copy2(original,originals/"00_mocha.rpx")
    original_probe=ROOT/"evidence/2026-11-09/mocha-boot-state/probe-readback.wuhb"
    if digest(original_probe)!="fae78cebaf9899629f8f937b57d8e240821bfa7f9407e26e81e44948d8bec13b":
        raise ValueError("unverified original identity probe")
    shutil.copy2(original_probe,originals/"rtl8153_mocha_boot_state.wuhb")
    shutil.copy2(candidate,experiments/"00_mocha.rpx")
    probe=ROOT/"dist/mocha-boot-state/wiiu/apps/rtl8153_mocha_boot_state.wuhb"
    probe_profile=ROOT/"build/mocha-boot-state/profile.json"
    profile=json.loads(probe_profile.read_text())
    from check_mocha_alloc_candidate import elf_sections
    for module in ("ios_net","ios_kernel"):
        raw=(project/"source"/module/(module+".elf")).read_bytes()
        sec,_=elf_sections(raw);s=sec[".text"]
        if profile["candidate"][module]["sha256"]!=hashlib.sha256(raw[s[4]:s[4]+s[5]]).hexdigest():
            raise ValueError("boot-state probe profile is stale")
    shutil.copy2(probe,output/"rtl8153_mocha_boot_state.wuhb")
    shutil.copy2(probe_profile,output/"boot-state-profile.json")
    shutil.copy2(ROOT/"docs/MOCHA_BOOTSTRAP_INVESTIGATION.md",output/"INVESTIGATION.md")
    shutil.copy2(ROOT/"docs/MOCHA_ALLOCATION_HARDWARE_TEST.md",output/"HARDWARE-TEST.md")
    shutil.copy2(ROOT/"docs/MOCHA_RELOAD_DIAGNOSTIC_AUDIT.md",output/"DIAGNOSTIC-AUDIT.md")
    (output/"embedded-payloads.json").write_text(json.dumps(report,indent=2)+"\n")
    (output/"net-candidate-review.json").write_text(json.dumps(review,indent=2)+"\n")
    content="""# Комплект для локального ревью — НЕ УСТАНАВЛИВАТЬ

Это ещё не пакет установки драйвера. Он не включает готовый аппаратный сценарий.

restore/00_mocha.rpx — исходный файл с SD владельца, дважды скачанный по FTP.
Он побайтно совпадает с официальным MochaPayload v0.2.5.

experiment/00_mocha.rpx — локальный Mocha с allocation-only NET probe.
Он не захватывает USB и не запускает драйвер в выделенной памяти. При успешной
исходной инициализации NET он выделяет страницу 4096 байт, проверяет два слова,
освобождает страницу и печатает RTL_NET_ALLOC.
IOS-NET BSS явно расширяется на 88 байт до 0x1288d080: 24 байта выравнивания
и 64 байта результата по адресу 0x1288d040. Boot State читает этот результат
только при совпадении экспериментального кода и установленного NET hook.
Commit подтверждает порядок записи кода, но когерентность кэша и очистка BSS
реальным загрузчиком пока не проверены. Новый WUHB включён для будущего теста.
Hook добавляется только в образ
IOS при kernel_launch_ios; установка файла и обычный reboot не доказывают запуск.
Включено также исправление исходного MCP timeout table: literal 0xffffffff
заменён указателем на слово. Это отдельное изменение пути reload.

Локальные ARM-тесты используют заглушки IOS/копирования/CP15/bootrom. Они не
подтверждают реальные права исполнения, DMA, кэш и аппаратный IOS reload.
Локальный MCP/PPC-бинарник отличается от установленного официального релиза.

Если после отдельного согласованного теста понадобится восстановление:
1. Полностью выключить консоль и вынуть SD.
2. На ПК вернуть restore/00_mocha.rpx в
   SD:/wiiu/environments/aroma/modules/setup/00_mocha.rpx.
3. Сверить SHA-256 с manifest.json, вернуть SD и включить консоль.
Резервная копия хранится на ПК, а не как второй загружаемый RPX в setup/.

Сейчас ничего из experiment/ на SD не копировать. Перед аппаратным тестом
нужно подготовить IOS-reload, ранний TCP log и подтвердить восстановление через SD.
"""
    (output/"README.md").write_text(content,encoding="utf-8")
    files={}
    for name in ("restore/00_mocha.rpx","experiment/00_mocha.rpx","INVESTIGATION.md",
                 "embedded-payloads.json","net-candidate-review.json","README.md","HARDWARE-TEST.md","DIAGNOSTIC-AUDIT.md"):
        p=output/name;files[name]={"bytes":p.stat().st_size,"sha256":digest(p)}
    for name in ("rtl8153_mocha_boot_state.wuhb","boot-state-profile.json",
                 "restore/rtl8153_mocha_boot_state.wuhb"):
        p=output/name;files[name]={"bytes":p.stat().st_size,"sha256":digest(p)}
    commit=subprocess.check_output(["git","-C",str(ROOT/"upstream/MochaPayload"),"rev-parse","HEAD"],text=True).strip()
    manifest={"status":"REVIEW_ONLY_NOT_INSTALLABLE","activation_allowed":False,
              "mocha_source_commit":commit,"verified_original_release":"v0.2.5",
              "toolchains":{"devkitARM":"15.2.0","devkitPPC":"16.1.0","modified":False},
              "checks":{"host_suite":"passed","ARM_allocation_cases":30,
                        "ARM_full_reload_writes":88,"ARM_full_reload_copies":222,
                        "ARM_boot_scenarios":8,
                        "real_console_native_execution":False},
              "remaining":["real IOS reload sequence","real BSS zeroing/status cache visibility",
                           "heap execute/cache checks","USB handoff and IOS-NET callbacks"],"files":files}
    (output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    archive=ROOT/"dist/mocha-allocation-review.zip"
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as z:
        for name in list(files)+["manifest.json"]:z.write(output/name,name)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for name,v in files.items():assert hashlib.sha256(z.read(name)).hexdigest()==v["sha256"]
    print("Review archive:",archive,"sha256",digest(archive))
    print("Original recovery RPX verified; embedded candidate payloads match; activation disabled.")

if __name__=="__main__":main()

