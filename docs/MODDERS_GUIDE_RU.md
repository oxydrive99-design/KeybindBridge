# KeybindBridge для мододелов

## Какой способ выбрать

| Задача | Рекомендуемый способ |
|---|---|
| Открыть локальную карту, GUI, фонарик | Автономное расширение с `keybindbridge.json` |
| Добавить бинд в уже существующий Tool/Part/Player-скрипт | `KeybindAction.lua` |
| Передать команду на сервер | `KeybindAction.lua` + `self.network` + серверная проверка |
| Назначить клавишу отдельному блоку конкретного сиденья | Raw API; используй Keybind Logic как эталон |

Автономное расширение загружается DLL автоматически и не требует размещения
предмета. Оно удобно для чисто клиентских функций. Обычный scripted-object
получает `self.network`, `self.storage` и другие возможности экземпляра игры,
поэтому лучше подходит для серверного состояния.

## Вариант 1: автономное расширение

Минимальная структура:

```text
YourMod/
├── description.json
├── keybindbridge.json
├── Gui/
│   └── IconMap.xml
├── Objects/
│   └── Database/
│       ├── rotationsets.rotationset
│       └── shapesets.shapedb
└── Scripts/
    └── KeybindBridgeExtension.lua
```

Для мода без блоков `shapesets.shapedb` должен содержать пустой
`shapeSetList`, а `rotationsets.rotationset` — пустой `rotationSet`. Полный
готовый шаблон находится в `examples/sdk-template-mod`.

### Манифест

```json
{
  "version": 1,
  "script": "Scripts/KeybindBridgeExtension.lua"
}
```

Путь должен быть относительным, находиться внутри мода и использовать `/`.
После добавления, удаления или переименования манифеста полностью перезапусти
игру: поиск выполняется при загрузке DLL.

### Одиночное нажатие

```lua
local ACTION = "author.mod.open_map"

local extension = {
    id = "author.mod.map",
    actions = {
        { id = ACTION, label = "Open map", defaultKey = "M" }
    }
}

function extension:onActionPressed(actionId)
    if actionId == ACTION then
        -- Локальная функция мода.
    end
end

KeybindBridgeRuntime.registerExtension(extension)
```

Runtime сам блокирует `onActionPressed`, пока открыто меню биндов, и поглощает
нажатие, чтобы оно не повторилось после закрытия.

### Несколько действий

Добавь несколько элементов в `actions`. Один callback получает ID конкретного
действия:

```lua
actions = {
    { id = "author.mod.map", label = "Open map", defaultKey = "M" },
    { id = "author.mod.backpack", label = "Open backpack", defaultKey = "B" }
}
```

У действий могут совпадать клавиши. ID совпадать не должны.

### Удержание и отпускание

`onActionPressed` сообщает только о переднем фронте. Для удержания используй
`actionIsDown` в `onUpdate`:

```lua
function extension:onUpdate()
    local menuOpen = sm.keybind.isGameMenuOpen()
    local down = not menuOpen and sm.keybind.actionIsDown(ACTION)

    if down and not self.wasDown then
        -- Начало удержания.
    elseif not down and self.wasDown then
        -- Отпускание.
    end
    self.wasDown = down
end
```

Для ограничения частоты обновлений добавь в таблицу расширения
`updateIntervalMs = 16`. Вызовы `onActionPressed` этим интервалом не
задерживаются.

## Вариант 2: KeybindAction в существующем скрипте

Скопируй `Scripts/KeybindAction.lua` из SDK в свой мод и загрузи его:

```lua
dofile("$CONTENT_DATA/Scripts/KeybindAction.lua")
```

Создай действие один раз в клиентском экземпляре:

```lua
function Example.client_onCreate(self)
    self.backpackKey = KeybindAction.create(
        "author.mod.backpack",
        "Open backpack",
        "B")
end
```

Проверяй его в клиентском обновлении:

```lua
function Example.client_onFixedUpdate(self)
    if self.backpackKey:wasPressed() then
        self:openBackpack()
    end
end
```

Методы helper:

- `isAvailable()` — установлена ли DLL;
- `isDown()` — удерживается ли назначенная клавиша;
- `wasPressed()` — одно событие нажатия;
- `wasReleased()` — одно событие отпускания;
- `getKey()` — текущий числовой VK-код;
- `getKeyName()` — отображаемое имя клавиши.

Helper поглощает события, созданные в открытом меню биндов. Если DLL не
установлена, методы безопасно возвращают `false`/`nil`, а мир не падает.

Не создавай отдельный helper в сотнях одинаковых объектов для одной глобальной
функции: каждый экземпляр будет независимо получать одно и то же событие.
Глобальный бинд должен иметь одного логического владельца.

## Мультиплеер и безопасность

Нажатие определяется на клиенте. Оно не является доказательством права изменить
серверное состояние.

```lua
function Example.client_onFixedUpdate(self)
    if self.action:wasPressed() then
        self.network:sendToServer("sv_requestAction")
    end
end

function Example.sv_requestAction(self, _, player)
    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then return end

    -- Проверить расстояние, владельца, экипировку, предметы и состояние мира.
    if (character.worldPosition - self.shape.worldPosition):length() > 5 then
        return
    end

    -- Только теперь менять серверное состояние.
end
```

Для рюкзака сервер должен хранить содержимое, проверять экипированный тип
рюкзака, переносить вещи в стандартный мешок смерти и синхронизировать клиенту
только разрешённое представление. KeybindBridge решает ввод, но не заменяет эту
игровую логику.

## Постоянные ID

Хороший формат:

```text
author.mod.feature.action
```

Допустимы латинские буквы, цифры, `.`, `_`, `-`, `:`; длина — до 96 байт.
Название, показываемое игроку, может меняться. ID после публикации менять нельзя:
назначения хранятся по нему.

## Клавиши по умолчанию

Можно передать имя (`"F"`, `"Tab"`, `"Mouse4"`, `"LeftShift"`, `"F12"`) или
числовой Windows VK-код. `End` зарезервирован для меню и не назначается.

Не рассчитывай, что выбранная по умолчанию клавиша свободна. Пользователь может
оставить конфликт намеренно или изменить назначение в меню.

## World-зависимые функции

Некоторые клиентские callback имеют Lua-окружение, но временно не имеют
world-контекста. `sm.effect`, `sm.audio` и похожие вызовы могут завершиться
ошибкой. Для важного однократного действия сохраняй запрос и повторяй его в
следующем `onUpdate`. Автономный фонарик показывает очередь такого типа.

## Перед публикацией

- новый `localId` мода;
- уникальные ID расширения и действий;
- отсутствуют абсолютные пути и `..` в `keybindbridge.json`;
- мод безопасно работает при отсутствии DLL;
- клиентские запросы повторно проверяются на сервере;
- нажатия из меню биндов не запускают игровую функцию;
- пройден тест после повторного входа в мир без перезапуска игры;
- пройден тест хоста и подключённого игрока;
- в архив не попали логи, Cache, `.pdb` и временные файлы.
