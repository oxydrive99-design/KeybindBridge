# Актуальная структура Blocks and Parts

Каждый локальный мод должен иметь `description.json` версии 2:

```json
{
  "allow_add_mods": true,
  "custom_icons": true,
  "name": "Название мода",
  "description": "Описание мода",
  "type": "Blocks and Parts",
  "version": 2,
  "localId": "UUID мода"
}
```

Обязательная база деталей:

```text
Objects/
└── Database/
    ├── rotationsets.rotationset
    ├── shapesets.shapedb
    └── ShapeSets/
        └── название.shapeset
```

Для автономного мода без блоков папка `ShapeSets` не нужна. Оставь обязательные
файлы базы пустыми:

```json
// Objects/Database/shapesets.shapedb
{
  "shapeSetList": []
}
```

```json
// Objects/Database/rotationsets.rotationset
{
  "rotationSet": []
}
```

Чтобы KeybindBridge сам запустил такой мод, рядом с `description.json` добавь:

```json
// keybindbridge.json
{
  "version": 1,
  "script": "Scripts/KeybindBridgeExtension.lua"
}
```

После добавления манифеста игру нужно перезапустить: DLL ищет расширения при
старте процесса. Готовый пример находится в `examples/autonomous-flashlight-mod`.

`shapesets.shapedb`:

```json
{
  "shapeSetList": [
    "$CONTENT_DATA/Objects/Database/ShapeSets/название.shapeset"
  ]
}
```

`rotationsets.rotationset`:

```json
{
  "rotationSet": []
}
```

Дополнительные проверки перед тестом:

- ShapeSet должен иметь расширение `.shapeset`, а не `.json`.
- При `custom_icons: true` UUID каждой детали должен присутствовать в
  `Gui/IconMap.xml`, а файл текстуры карты иконок должен существовать.
- Материал `PoseAnim...` требует хотя бы `pose0` в соответствующем LOD.
- Количество элементов `subMeshList` должно соответствовать используемому mesh.
- Автономный фонарик использует собственный узкий `spotLight`, но его база
  `Effects` находится в `KeybindBridge Core`: runtime исполняется в sandbox
  основного мода. При первой установке нужны обе папки; обновление только
  скрипта фонарика требует замены лишь `KeybindBridge-Flashlight`.
