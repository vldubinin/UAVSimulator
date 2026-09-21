"""
Діагностика: чому GetAsyncKeyState не бачить Numpad4/Numpad6 (або будь-яку іншу клавішу).

Друкує в реальному часі, які саме віртуальні коди клавіш (VK) зараз натиснуті,
плюс стан NumLock. Тримай клавішу і дивись, який рядок з'являється — це і є
код, який реально приходить від ОС для цієї фізичної клавіші.

Навіщо це потрібно: keyboard_attitude_test_client.py читає VK_NUMPAD4=0x64 і
VK_NUMPAD6=0x66 напряму. Якщо ці коди насправді ніколи не запалюються, а
натомість запалюється щось інше (наприклад VK_LEFT/VK_RIGHT при вимкненому
NumLock, або зовсім інший код на ноутбуці без окремого numpad-блоку, де
Numpad4/6 емулюються через Fn+J/Fn+L чи подібне), — це видно одразу тут.

Запуск:
    python debug_numpad_keys.py
Зупинка: Ctrl+C.
"""

from __future__ import annotations

import ctypes
import sys
import time

_user32 = ctypes.windll.user32

# Всі клавіші, які варто перевірити: цифровий блок, +/-/*//,  Enter, Del,
# а також звичайні стрілки та NumLock — для порівняння.
CANDIDATE_KEYS: dict[str, int] = {
    "NUMPAD0": 0x60, "NUMPAD1": 0x61, "NUMPAD2": 0x62, "NUMPAD3": 0x63,
    "NUMPAD4": 0x64, "NUMPAD5": 0x65, "NUMPAD6": 0x66, "NUMPAD7": 0x67,
    "NUMPAD8": 0x68, "NUMPAD9": 0x69,
    "ADD (Numpad +)": 0x6B, "SUBTRACT (Numpad -)": 0x6D,
    "MULTIPLY (Numpad *)": 0x6A, "DIVIDE (Numpad /)": 0x6F,
    "DECIMAL (Numpad .)": 0x6E, "SEPARATOR (Numpad Enter, іноді)": 0x6C,
    "LEFT (стрілка)": 0x25, "UP (стрілка)": 0x26,
    "RIGHT (стрілка)": 0x27, "DOWN (стрілка)": 0x28,
    "INSERT": 0x2D, "DELETE": 0x2E, "HOME": 0x24, "END": 0x23,
    "PRIOR (PgUp)": 0x21, "NEXT (PgDn)": 0x22, "CLEAR": 0x0C,
}
VK_NUMLOCK = 0x90


def is_key_down(vk_code: int) -> bool:
    return bool(_user32.GetAsyncKeyState(vk_code) & 0x8000)


def is_num_lock_on() -> bool:
    return bool(_user32.GetKeyState(VK_NUMLOCK) & 0x0001)


def main() -> None:
    if sys.platform != "win32":
        print("Потрібна Windows (ctypes + user32).", file=sys.stderr)
        sys.exit(1)

    print(__doc__)
    print("Тисни клавіші по одній (особливо ті, що не працюють) і дивись, що загориться нижче.\n")

    try:
        while True:
            pressed = [name for name, vk in CANDIDATE_KEYS.items() if is_key_down(vk)]
            num_lock = "ON" if is_num_lock_on() else "OFF"
            line = f"NumLock={num_lock}   Натиснуто: {', '.join(pressed) if pressed else '(нічого)'}"
            sys.stdout.write("\r" + line.ljust(120))
            sys.stdout.flush()
            time.sleep(0.05)
    except KeyboardInterrupt:
        print("\nЗупинено.")


if __name__ == "__main__":
    main()
