# MP_MAP.md — карта кода Subnautica Mobile для кооператива
Источник: dump.cs (Il2CppDumper, metadata v31, Unity 6000.0.78f1), versionCode 83811.
VA/RVA из dump.cs/script.json. Все имена — [ПРОВЕРЕНО] по dump.cs.
ВАЖНО (A4): в моде резолвить по именам через il2cpp_class_from_name/il2cpp_class_get_method_from_name; RVA — только fallback с проверкой versionCode==83811. Ничего здесь [НЕ ПРОВЕРЕНО НА УСТРОЙСТВЕ].

## 1) Игрок — позиция, поворот, камера, анимации, ник
- `Player : Living` (TypeDefIndex 1503). Статические: `Player.main` (0x8), `Player.mainObject` (0x10), `Player.mainCollider` (0x18).
  - `Player.Awake()` RVA 0x2AB518C — точка инициализации. Хук для старта мода. [ПРОВЕРЕНО]
  - `Player.Update()` RVA 0x2AB8AA0 (virtual), `Player.Start()` RVA 0x2AB3380.
  - `Player.ManagedUpdate()` / `ManagedLateUpdate()` / `ScheduledUpdate()` — есть. [ПРОВЕРЕНО]
- Позиция/поворот: через `UnityEngine.Transform` компонента `Player.mainObject`.
  - `Transform.get_position` RVA 0x52076A0, `get_rotation` RVA 0x5207BA8, `get_forward` RVA 0x5208070.
  - set_position/set_rotation — для применения удалённого состояния (не извлекал RVA, резолвить по имени).
- Камера: `MainCamera.get_camera` RVA 0x26C963C.
- Анимации (для «призрака»): `SafeAnimator.SetFloat` 0x2A2A9F4, `SetBool` 0x2A29E48, `SetInteger` 0x2A2AA08, `SetTrigger` 0x2A2AA1C.
- Ник: отдельного поля ника у Player нет (singleplayer). Вводить свой, из конфига мода.
- Синхронизировать: position (Vector3), rotation (Quaternion), forward, набор аним-параметров (min), ник. Частота ~10–15 Гц + интерполяция.
- Точки перехвата: Player.Update (чтение локального состояния), отдельный GameObject для призрака.
- Сложность: **низкая–средняя** (рендер призрака = клон модели игрока; анимации — средняя).

## 2) Время суток и глобальные события
- `DayNightCycle : MonoBehaviour` (TypeDefIndex 646). Статическое `DayNightCycle.main` (0x0).
  - `get_timePassed` RVA 0x2949DE8, `get_timePassedAsFloat` 0x2949DC8, `get_timePassedSinceOrigin` 0x2949DD4.
  - `SetTimePassed(...)` RVA 0x2949E84 — установка времени (для синка хост→клиент).
  - `get_dayNightSpeed` 0x2949DA0, `get_deltaTime` 0x2949DA8.
- Синхронизировать: timePassed (float/double) хост авторитетен, клиенты подтягивают.
- Точка перехвата: периодический пуш хостом; клиент — SetTimePassed.
- Сложность: **низкая**.
- Неизвестное: глобальные события (аврора, спасение и пр.) — классы не извлекал, выяснить по мере надобности.

## 3) Чат / сообщения
- `ErrorMessage : MonoBehaviour` (TypeDefIndex 814):
  - `ErrorMessage.AddMessage` RVA 0x297CF68, `AddError` 0x297CF10, `AddWarning` 0x297CF6C, `AddDebug` 0x297CF70. [ПРОВЕРЕНО]
  - Подходит для вывода диагностики/чата внутри игры (без ПК).
- Ввод: мобильный сенсорный ввод — отдельный UI (uGUI). Классы `uGUI_*` есть; конкретный text-input для чата не извлечён.
- Синхронизировать: текст + ник + timestamp. Точка перехвата: свой UI → AddMessage на обоих концах.
- Сложность: **низкая** (вывод), **средняя** (сенсорный ввод).

## 4) Инвентарь, контейнеры, подбор/выброс
- `Inventory : MonoBehaviour` (TypeDefIndex 1198): `get_container` 0x2A22FF8, `get_equipment` 0x2A23000, `get_quickSlots` 0x2A23008, `get_storageRoot` 0x2A23018.
- `InventoryItem` (TypeDefIndex 1200), `ItemsContainer.AddItem` RVA 0x29F53C8, `RemoveItem` RVA 0x29E7F1C, `AddItemToMap` 0x29F6218.
- `Pickupable : HandTarget` (TypeDefIndex 1393): `Pickup()` RVA 0x2A8CC28, `Drop(...)` RVA 0x2A8E378.
- Синхронизировать: AddItem/RemoveItem (techType, count, container id), Pickup/Drop (id предмета в мире, позиция).
- Точки перехвата: ItemsContainer.AddItem/RemoveItem, Pickupable.Pickup/Drop.
- Сложность: **средняя–высокая** (конфликты одновременного подбора одного предмета; нужен арбитраж хостом + уникальные id).

## 5) Мировые сущности: ячейки/батчи, идентификаторы, детерминизм
- `LargeWorldEntity : MonoBehaviour` (TypeDefIndex 2277): поле `cellLevel` (0x2C), `updaterIndex` (0x48). `UpdateCell(streamer)` RVA 0x2C8DDD4.
- `LargeWorldStreamer` (TypeDefIndex ~2279): `get_batchLoadRings` 0x2C8F254, `add_onLoadActivity` 0x2C8F0A4 — события загрузки батчей/ячеек.
- `CellManager`, `EntitySlot` (TypeDefIndex 2262): `GetBiomeType` 0x2C86BDC, `IsTypeAllowed` 0x2C86BE4 — спавн по слотам.
- Идентификаторы: `UniqueIdentifier` (TypeDefIndex 5137): `get_Id` RVA 0x28A0158, `set_Id` 0x28A0188, `get_ClassId` 0x28A0BF4. `PrefabIdentifier : UniqueIdentifier` (5133). Это ключ к сетевым ID сущностей. [ПРОВЕРЕНО]
- Детерминизм: глобальный RNG — `UnityEngine.Random.get_state` RVA 0x51E9A70 / `set_state` 0x51E9AF0 / `InitState` 0x51E9A34. Отдельного «world seed» поля в извлечённых классах НЕ найдено.
- Сложность: **высокая**.
- НЕИЗВЕСТНО / ВЫЯСНИТЬ:
  - Как спавн лута/существ seeded: есть ли сохранённый сид в SaveLoadManager или он производный от UnityEngine.Random.state? Найти где InitState/set_state вызывается при новой игре/загрузке.
  - Стабильны ли UniqueIdentifier.Id между двумя клиентами с одинаковым сейвом (решает, можно ли матчить сущности по id без собственного протокола спавна).
  - Как зафиксировать общий сид: хост рассылает Random.state / сид новой игры клиентам до спавна.

## 6) Существа, транспорт, постройки, крафт
- `Creature : Living` (TypeDefIndex 370) — базовый класс существ. AI/состояние — по наследникам.
- `Vehicle` (SeaMoth : Vehicle TypeDefIndex 1781; Cyclops — через SubRoot 1959): `get_vehicleDefaultName` 0x2C69AEC, `get_playerFullyEntered` 0x2C69BFC, `get_turnedOn` 0x2C69BF4.
- Постройки (base building): классы `Base*` (Base.Direction и пр.) присутствуют; конкретные методы строительства не извлечены.
- Крафт: `CraftData.AddToInventory` (см. разведку 1), `CraftingAnalytics`. Точки крафта — `Crafter`/`Fabricator` (выяснить).
- Синхронизировать: позиция/состояние существ и транспорта, ghost/base pieces при строительстве, транзакции крафта.
- Сложность: **высокая** (AI существ авторитетен на хосте; постройки — структурная синка).
- НЕИЗВЕСТНО: конкретные классы крафта/строительства, выяснить при реализации.

## 7) Сохранение/загрузка и меню новой игры (добавить «Присоединиться»)
- `SaveLoadManager`: `SaveToTemporaryStorageAsync` 0x2B64A9C, `LoadAsync` 0x2B657BC, `LoadSlotsAsync` 0x2B64E34, `get_isSaving` 0x2B638F4, `get_autoSaveEnabled` 0x2B6382C.
- `uGUI_MainMenu`: `Awake` 0x2D791E0, `Start` 0x2D79448, `StartMostRecentSaveOrNewGame` 0x2D794DC. Новая игра: `uGUI_MainMenu.<StartNewGame>d__33` (корутина), `uGUI_MainMenuNewGameMenu.OnButtonDown` 0x29BAAB8.
- Для MVP: «Присоединиться» можно не встраивать в сенсорный UI — конфиг-файл с адресом хоста (читается модом при старте). Встраивание кнопки в uGUI_MainMenu — отдельная, более поздняя задача.
- Сложность: конфиг — **низкая**; кнопка в сенсорном UI — **средняя–высокая**.
- НЕИЗВЕСНО: структура сейв-слотов, как добавить пункт меню в мобильном uGUI без ПК.

## Приоритет для MVP (согласно goal): 1 → 3 → 2 → 4 → 5/6/7
