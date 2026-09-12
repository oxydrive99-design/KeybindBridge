# Заметки о backend на SmSdk InputManager

`InputManager.hpp` остаётся хорошей точкой для второго backend, но его нельзя
безопасно включить в общий релиз, пока singleton берётся по фиксированному адресу.

Если backend будет добавлен, в нём важны два исправления поверх удобных helper-ов
SmSdk:

1. Состояние «клавиша сейчас нажата» — это `EKeyState_Press` **или**
   `EKeyState_Hold`. Один `IsKeyHeld()` теряет первый кадр.
2. Индекс массива должен быть `uint8_t`/`unsigned char`. Параметр `char` на MSVC
   может быть знаковым, и коды 128–255 иначе способны обратиться до начала
   `m_eKeyStates[256]`.

Безопасная форма чтения после проверки адреса:

```cpp
const auto index = static_cast<std::uint8_t>(virtualKey);
const auto state = inputManager->m_eKeyStates[index];
const bool down = state == SM::EKeyState_Press || state == SM::EKeyState_Hold;
```

До разыменования singleton нужно либо найти актуальную сигнатуру инструкции,
которая загружает `SM_INPUT_MANAGER`, либо сверить PE timestamp/hash поддерживаемой
версии. При несовпадении backend обязан отключиться и вернуть диагностику, а не
продолжать с неподтверждённым адресом.

Публичный Lua API менять не понадобится: `isDown`, `pressSerial` и захват клавиши
останутся теми же, изменится только источник событий.
