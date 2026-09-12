# Notes on an SmSdk InputManager backend

`InputManager.hpp` remains a useful entry point for a second input backend, but
it cannot be enabled safely in the general release while its singleton is
resolved from a fixed address.

Two corrections are required on top of the convenient SmSdk helpers:

1. A key is currently down when its state is `EKeyState_Press` **or**
   `EKeyState_Hold`. Checking only `IsKeyHeld()` misses the first frame.
2. The array index must be `uint8_t`/`unsigned char`. Plain `char` may be signed
   in MSVC, allowing key codes 128-255 to index before `m_eKeyStates[256]`.

Safe reading after validating the address:

```cpp
const auto index = static_cast<std::uint8_t>(virtualKey);
const auto state = inputManager->m_eKeyStates[index];
const bool down = state == SM::EKeyState_Press || state == SM::EKeyState_Hold;
```

Before dereferencing the singleton, the implementation must either find a
current instruction signature that loads `SM_INPUT_MANAGER`, or verify the PE
timestamp/hash of a supported game build. On mismatch, the backend must disable
itself and report diagnostics instead of continuing with an unverified address.

The public Lua API would not change: `isDown`, `pressSerial` and key capture
would remain the same while only the event source changes.
