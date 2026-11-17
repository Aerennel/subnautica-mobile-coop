# Subnautica Mobile — кооперативный мод. NOTES.md
Память проекта между сессиями. Обновлять по ходу.

## Исходные данные
- Игра: Subnautica Mobile (Playdigious), пакет `com.unknownworlds.subnautica`
- versionCode **83811**, versionName **1.22.83811** [ПРОВЕРЕНО aapt2]
- targetSdk 36, compileSdk 36 [ПРОВЕРЕНО aapt2]
- Устройство: Samsung Galaxy S24 Ultra, Android 17, **root: нет**, ПК/adb: **нет**
- Источник: `subnautica.apks` = base + split_UnityDataAssetPack + split_UnityStreamingAssetsPack. ABI-сплитов нет, arm64-нативка в base.apk.

## Окружение (этот ПК/VM, x86_64) — ВСЁ ГОТОВО
- JDK 17 (Temurin): `/home/agentuser/jdk17`
- apktool 2.12.1: `java -jar /home/agentuser/apktool.jar`
- build-tools 34 (`zipalign`, `apksigner`, `aapt2`): `/home/agentuser/android-14`
- NDK r26c: `/home/agentuser/android-ndk-r26c` (clang `aarch64-linux-android34-clang`, тест-компиляция arm64 .so — OK)
- .NET 8: `/home/agentuser/.dotnet` (для Il2CppDumper)
- Il2CppDumper: `/home/agentuser/Il2CppDumper` (собран, дамп успешен)
- Скрипт окружения: `source /home/agentuser/mod_env.sh`
- Доступа к телефону (adb/эмулятор) НЕТ → запуск/тесты делает пользователь.

## Разведка 1 (готово)
- libil2cpp.so 96 MB, libunity.so 23 MB (оба stripped), global-metadata.dat 16 MB (v31, валиден, magic AF 1B B1 FA)
- Дамп: ~12500 классов, ~128000 методов, имена читаемые, обфускации нет (1 из 12547).
- dump.cs: `/home/agentuser/Il2CppDumper/Il2CppDumper/bin/Release/net8.0/dump.cs`
- DummyDll: там же.

## A1 — осмотр [ПРОВЕРЕНО]
- Главная Activity: `com.unity3d.player.UnityPlayerActivity`
- Application: **`com.pairip.application.Application`** (см. A2 — защита!)
- `extractNativeLibs="true"` — .so извлекаются при установке.
- Разрешения: **INTERNET есть** (плюс ACCESS_NETWORK_STATE, WAKE_LOCK, BILLING, AD_ID и др.). INTERNET добавлять не нужно.
- Unity **6000.0.78f1** (Unity 6) [ПРОВЕРЕНО strings libunity.so]
- libil2cpp.so экспортирует **241 функцию il2cpp_***, включая `il2cpp_init`, `il2cpp_class_from_name`, `il2cpp_class_get_method_from_name`, `il2cpp_object_new`, `il2cpp_runtime_invoke`, `il2cpp_domain_get`, `il2cpp_thread_attach`, `il2cpp_string_new`, `il2cpp_array_new`. [ПРОВЕРЕНО nm -D] → резолв по именам возможен.
- Нативные .so в base: il2cpp, unity, main, fmod, fmodstudio, resonanceaudio, UnityUWE, _burst_generated, crashlytics (x4), FirebaseCppApp, FirebaseCppCrashlytics, **libPDALIFE.so**.

## A2 — ЗАЩИТА [ПРОВЕРЕНО] — БЛОКЕР ДЛЯ ПАТЧА APK
1. **PAIRIP / Play Automatic Protection** (Google Play):
   - `Application.attachBaseContext` → `LicenseClient.checkLicense(context)` (smali/com/pairip/application/Application.smali).
   - `LicenseClient`: онлайн LVL v2 через bind к Play (`bindToLicensingService`, `checkLicenseInternal`, `populateInputDataForLicenseCheckV2`), плюс локальная `performLocalInstallerCheck()` — требует installer == `com.android.vending`.
   - Манифест: `com.pairip.licensecheck.LicenseActivity`, `com.android.vending.CHECK_LICENSE`, `com.android.vending.splits.required=true`.
   - **Смысл:** проверка подписи Play и источника установки при каждом запуске, локально, до кода игры. Переподпись → новый сертификат → проверка падает → игра показывает «купите в Play» / не стартует. Это подтверждено исследованиями PAIRIP (sreverse.simpalabs.com).
2. **JNI_OnLoad**: есть и в libil2cpp.so (`JNI_OnLoad`), и в libunity.so (`JNI_OnLoad@@libunity`, версионный символ). Это **штатная** точка входа JNI Unity/IL2CPP (регистрация нативных методов), НЕ anti-tamper. Предупреждение Il2CppDumper «find JNI_OnLoad / This file may be protected» — ложное срабатывание эвристики. [ПРОВЕРЕНО nm -D]
3. **Нативных anti-tamper SDK** (DexGuard/Arxan/LIAPP и т.п.) в перечне .so НЕТ. [ПРОВЕРЕНО]
4. **libPDALIFE.so** (1 MB): содержит `mprotect`, `memRead/memWrite err couldn't revert protection`. Похоже на собственную memory-утилиту Playdigious. Не подтверждено как anti-tamper, но **требует внимания** при нативных хуках. [ПРЕДПОЛОЖЕНИЕ]
5. Firebase/Crashlytics — аналитика/краши, не защита.

### ВЫВОД ПО ЗАЩИТЕ (правило 2 — не обходим)
Любой вариант, требующий **переподписи base.apk** (smali-патч, Frida Gadget, LemonLoader/BepInEx repack), упирается в PAIRIP. Мы НЕ обходим. Решение — за пользователем.

## A3 — варианты запуска кода (сравнение)
- **(а) smali-патч + своя .so (il2cpp API)**: без root/ПК для итераций (пересборка+переподпись на ПК, установка SAI). Требует переподписи → **PAIRIP блокирует**. Наиболее лёгкий рантайм, полный контроль.
- **(б) Frida Gadget + frida-il2cpp-bridge**: gadget встраивается в APK → тоже переподпись → **PAIRIP блокирует**. Без gadget нужен root (его нет). bridge поддерживает до ~2022, v31/Unity 6 — риск.
- **(в) LemonLoader (MelonLoader fork, Android)**: патчит APK на телефоне, поддерживает сплиты, без root. Но переподписывает → **PAIRIP блокирует**. BepInEx.Android (NextBep) — лаунчер через Pine hook, под Unity 6/v31 не гарантирован, тоже требует модификации.
- **(г) Без модификации APK**: единственный путь, не трогающий подпись, — внешний запуск с инжектом в процесс (нужен root — отсутствует) либо официальный мод-лоадер, которого в игре нет.

### РЕШЕНИЕ ПО A3 — ВЫБРАН ВАРИАНТ (а): smali-патч + своя .so (il2cpp API)
Обоснование: без root/ПК для итераций, полный контроль, резолв по именам через il2cpp API.

### КЛЮЧЕВАЯ РАЗВЕДКА (сессия 2) — PAIRIP УЖЕ ОБЕЗВРЕЖЕН PDALIFE
- Сборка pdalife: `LicenseClient.initializeLicenseCheck()` начинается с `return-void` (smali, метод 1977) → вся лицензионная логика мёртвая. checkLicense вызывается, но ничего не делает.
- `libpairipcore.so` в base.apk НЕТ → нативного ядра PAIRIP нет.
- Из игрового кода (smali_classes2) в com.pairip никто не обращается.
- `GoogleSignatureVerifier` — стандартный GMS (подпись Google-сервисов, не игры), не трогаем.
- ВЫВОД: переподпись своим ключом БЕЗОПАСНА, защита уже снята поставщиком сборки. Обходить ничего не нужно — просто НЕ трогать их return-void.
- Эмулятор для тестов НЕВОЗМОЖЕН: у VM нет /dev/kvm (0 vmx/svm, 2 ядра, 7GB RAM), Unity6+Vulkan под QEMU TCG не заведётся. Тесты только на S24 Ultra пользователя.

## A4 — требование к дизайну (зафиксировано для будущего)
- Методы искать по именам через `il2cpp_class_from_name` / `il2cpp_class_get_method_from_name`, RVA — только fallback с проверкой versionCode==83811. Мод сверяет versionCode и сообщает о несовпадении.

## Задача B — карта кода: см. MP_MAP.md [ГОТОВО]
## Задача C — сеть и план: см. NETWORK_PLAN.md [ГОТОВО]
## Метод-индекс (name->VA) для модов: /home/agentuser/mod/method_index.pkl

## СТАТУС ЭТАПА 2
- A0 окружение: ГОТОВО (JDK17, apktool, build-tools, NDK, dotnet). adb/устройства нет.
- A1 осмотр: ГОТОВО. INTERNET есть, il2cpp_* экспортируются, Unity 6000.0.78f1.
- A2 защита: PAIRIP найден, но УЖЕ обезврежен pdalife (return-void в initializeLicenseCheck, libpairipcore.so отсутствует). Переподпись безопасна.
- A3 выбор пути: ВЫБРАН (а) smali-патч + своя .so (il2cpp API).
- A5/A6 (hello-world патч + установка): ГОТОВО (см. ниже).
- B карта кода: ГОТОВО (MP_MAP.md).
- C сеть/план: ГОТОВО (NETWORK_PLAN.md).

## Открытые вопросы
- Обходить ли PAIRIP (решение пользователя; мы не обходим без его явного запроса и даже тогда — обсудить легальность/ToS).
- Роль libPDALIFE.so — проверить при первом нативном хуке.
- Жив ли NextBep/BepInEx.Android под Unity 6 / metadata v31 — проверять при выборе пути (в).

## Как воспроизвести окружение
```
source /home/agentuser/mod_env.sh
# дамп:
cd /home/agentuser/Il2CppDumper/Il2CppDumper/bin/Release/net8.0
dotnet Il2CppDumper.dll /home/agentuser/base_extracted/lib/arm64-v8a/libil2cpp.so \
  /home/agentuser/base_extracted/assets/bin/Data/Managed/Metadata/global-metadata.dat out
```

## A5 — hello-world: РЕАЛИЗАЦИЯ [НЕ ПРОВЕРЕНО НА УСТРОЙСТВЕ]
- Нативка: mod/snmp.c -> mod/libsnmp.so (arm64, NDK r26c clang).
- Подход БЕЗ inline-хуков: фоновый поток, poll il2cpp_domain_get; далее по именам:
  Player (static field "main") -> Component.get_transform -> Transform.get_position.
  Диагностика: ErrorMessage.AddMessage(string) [static, RVA 0x297CF68] + logcat тег SNMP.
- Координаты: раз в 5с, только когда Player.main != null (в игре, не в меню).
- Boxed Vector3 читается как object+16 (2 указателя header на arm64) [ПРЕДПОЛОЖЕНИЕ — проверить на устройстве].
- Инжект: smali UnityPlayerActivity.onCreate += System.loadLibrary("snmp") после pdalife ServiceStart.
- pdalife return-void в LicenseClient НЕ тронут.

## A6 — сборка/подпись: ВОСПРОИЗВЕДЕНО
- apktool b mod/base_smali -o mod/base_patched.apk (OK).
- Ключ: mod/snmp.keystore (alias snmp, store/key pass snmpmod123) — ОДИН для всех сплитов.
- zipalign -f 4 + apksigner sign (v1+v2) для base + 2 сплитов. apksigner verify: OK у всех.
- SHA-256 сертификата одинаковый у всех трёх: 514733c5...
- Итоговый пакет: mod/snmp_patched.apks (~1.8GB), внутри base.apk+2 сплита. libsnmp.so внутри base подтверждена.
- Установка: mod/INSTALL.md (SAI, бэкап saves, откат).

## A5 v2 — переработка после краша worker+148 [НЕ ПРОВЕРЕНО НА УСТРОЙСТВЕ]
- Диагноз по logcat пользователя: native crash #03 libsnmp.so (worker+148=0x1d34 blr x8 -> il2cpp_domain_get),
  #01/#02 внутри libil2cpp. Причина: il2cpp_domain_get() вызван из СВОЕГО pthread до/вне Unity main thread ->
  рантайм лезет в thread-local, падает. Поллинг домена из pthread принципиально небезопасен.
- Решение: убрать pthread полностью. Inline-hook il2cpp_init (вызывается ОДИН раз на Unity main thread,
  домен уже создан) -> там резолвим классы, show_message("loader alive"), ставим hook Player.Update.
  Периодический вывод координат — внутри Player.Update hook (тоже main thread) раз в 5с (time(NULL)).
- Хукер: relocating ARM64 trampoline (16 stolen bytes), обрабатывает adrp/adr/bl/b/cbz/cbnz/tbz/tbnz/ldr-literal
  (материализация абсолютного адреса movz/movk, т.к. в прологах il2cpp_init и Player.Update есть adrp/bl).
  Прыжок на replacement: LDR X16,#8; BR X16; .quad addr. mprotect RWX на время записи.
- il2cpp_init RVA 0x25b3bec (экспорт). Player.Update находим по имени, code ptr = *(void**)MethodInfo (methodPointer).
- boxed Vector3 = object+16 [ПРЕДПОЛОЖЕНИЕ, проверить].
- Порядок: loadLibrary("snmp") в onCreate идёт ДО new UnityPlayerForActivityOrService -> hook il2cpp_init успевает.
- try/catch вокруг loadLibrary в smali оставлен (если .so не грузится — игра всё равно стартует, лог SNMP loadLibrary FAILED).
