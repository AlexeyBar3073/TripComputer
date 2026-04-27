# Файл: push_firmware.py

"""
ТЕГ: OTA_TOOLS/FIRMWARE_PUSHER

ФАЙЛ: scripts/push_firmware.py

МЕСТОНАХОЖДЕНИЕ: /scripts/

НАЗНАЧЕНИЕ ФАЙЛА И ПРИНЦИП РАБОТЫ:
Скрипт автоматизации для работы с прошивками БК.
1. В режиме PlatformIO (авто): регистрируется как хук и выполняется после сборки.
2. В режиме CLI (ручной): позволяет принудительно отправить последний собранный бинарник на устройство.

ОТВЕТСТВЕННОСТЬ: Переименование firmware.bin с учетом версии и доставка на Android через ADB.

АРХИТЕКТУРНЫЙ ПРИНЦИП: Scripting/Automation (SCons + CLI).

СВЯЗИ С ДРУГИМИ ФАЙЛАМИ:
- Читает: src/core/version.h (в проекте прошивки)
- Используется: PlatformIO (extra_scripts)
- Взаимодействует: Android Device (adb shell, am broadcast)
"""

import os
import shutil
import re
import subprocess
import sys

# --- НАСТРОЙКИ ПОЛЬЗОВАТЕЛЯ ---
# Если скрипт не находит adb автоматически, вставьте путь к нему здесь.
# Пример: MANUAL_ADB_PATH = r"C:\Users\Admin\AppData\Local\Android\Sdk\platform-tools\adb.exe"
MANUAL_ADB_PATH = r""

# Попытка импорта окружения SCons (для PlatformIO)
try:
    from SCons.Script import Import
    # Если мы в PIO, это сработает
    if "env" not in globals():
        Import("env")
except Exception:
    # Если запуск прямой, env будет эмулирован ниже
    env = None

# --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

def find_sdk_from_local_properties():
    """
    Пытается найти путь к Android SDK в файле local.properties проекта.
    Безопасно обрабатывает отсутствие __file__ в контексте PlatformIO.
    """
    possible_props = []

    # 1. Пробуем фиксированный путь к Android проекту (самый надежный для этой среды)
    possible_props.append("C:/Project/BluetoothCar/local.properties")

    # 2. Пытаемся определить путь относительно скрипта, если доступно __file__
    try:
        # В режиме CLI __file__ определен
        script_dir = os.path.dirname(os.path.abspath(__file__))
        possible_props.append(os.path.join(script_dir, "..", "local.properties"))
    except NameError:
        # В режиме PlatformIO __file__ может отсутствовать
        pass

    for prop_path in possible_props:
        if os.path.exists(prop_path):
            try:
                # Читаем с кодировкой utf-8, так как в путях может быть кириллица
                with open(prop_path, 'r', encoding='utf-8') as f:
                    for line in f:
                        if line.startswith("sdk.dir="):
                            # Извлекаем значение после знака =
                            path = line.split("=", 1)[1].strip()
                            # В Windows пути в local.properties часто экранированы (двоеточия и слеши)
                            # Например: C\:\\Android studio SDK
                            path = path.replace("\\:", ":").replace("\\\\", "\\")
                            return path
            except Exception as e:
                # В случае ошибки чтения конкретного файла пробуем следующий
                continue
    return None

def get_adb_command():
    """
    Пытается найти исполняемый файл adb.
    1. Проверяет ручной путь.
    2. Проверяет local.properties проекта.
    3. Проверяет системный PATH.
    4. Проверяет переменные окружения ANDROID_HOME и ANDROID_SDK_ROOT.
    5. Проверяет стандартный путь установки на Windows (%LOCALAPPDATA%).
    """
    # Список для отладки поиска
    searched_paths = []

    # 0. Проверка ручного пути (Приоритет №1)
    if MANUAL_ADB_PATH and os.path.exists(MANUAL_ADB_PATH):
        return f'"{MANUAL_ADB_PATH}"'
    elif MANUAL_ADB_PATH:
        searched_paths.append(f"Manual Path (Not Found: {MANUAL_ADB_PATH})")

    # 1. Проверка через local.properties (Приоритет №2 - самый надежный для Android Studio)
    sdk_from_props = find_sdk_from_local_properties()
    if sdk_from_props:
        adb_in_props = os.path.join(sdk_from_props, "platform-tools", "adb.exe")
        if os.path.exists(adb_in_props):
            return f'"{adb_in_props}"'
        searched_paths.append(f"local.properties ({adb_in_props})")

    # 2. Если adb уже в PATH, используем его
    if shutil.which("adb"):
        return "adb"
    searched_paths.append("System PATH")

    # 2. Пробуем найти через стандартные переменные окружения Android SDK
    for env_var in ["ANDROID_HOME", "ANDROID_SDK_ROOT"]:
        sdk_path = os.environ.get(env_var)
        if sdk_path:
            adb_exe = os.path.join(sdk_path, "platform-tools", "adb.exe")
            if os.path.exists(adb_exe):
                return f'"{adb_exe}"'
            searched_paths.append(f"{env_var} ({adb_exe})")

    # 3. Проверка стандартного пути Windows: %LOCALAPPDATA%\Android\Sdk
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        std_sdk_path = os.path.join(local_app_data, "Android", "Sdk", "platform-tools", "adb.exe")
        if os.path.exists(std_sdk_path):
            return f'"{std_sdk_path}"'
        searched_paths.append(f"Windows Standard Path ({std_sdk_path})")

    # Если ничего не нашли, выводим информацию для пользователя
    print(f"[OTA_SCRIPT] ВНИМАНИЕ: adb не найден. Проверено: {', '.join(searched_paths)}")
    return "adb"

def find_project_root(start_path):
    """
    Ищет корень проекта PlatformIO, поднимаясь вверх до файла platformio.ini.
    """
    curr = os.path.abspath(start_path)
    while curr != os.path.dirname(curr):
        if os.path.exists(os.path.join(curr, "platformio.ini")):
            return curr
        curr = os.path.dirname(curr)
    return None

def rename_and_push_firmware(source=None, target=None, env=None):
    """
    Основная логика: переименование и отправка.
    Поддерживает вызов как из PIO, так и вручную.
    """
    if env is None:
        print("[OTA_SCRIPT] Ошибка: Окружение не определено.")
        return

    # 1. ПОЛУЧЕНИЕ ПУТЕЙ
    # PROJECT_DIR - корень, где лежит platformio.ini
    project_dir = env.get("PROJECT_DIR")
    if not project_dir or not os.path.exists(project_dir):
        print(f"[OTA_SCRIPT] Ошибка: Неверный путь проекта: {project_dir}")
        return

    # Директория сборки (обычно .pio/build)
    build_dir = env.get("PROJECT_BUILD_DIR", os.path.join(project_dir, ".pio", "build"))
    # Имя окружения
    env_name = env.get("PIOENV", "wemos_d1_mini32")

    firmware_path = os.path.join(build_dir, env_name, "firmware.bin")

    # 2. ИЗВЛЕЧЕНИЕ ВЕРСИИ
    version = "unknown"
    config_path = os.path.join(project_dir, "src", "core", "version.h")

    if os.path.exists(config_path):
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                content = f.read()
                match = re.search(r'#define\s+FW_VERSION_STR\s+"([^"]+)"', content)
                if match:
                    version = match.group(1).replace('.', '_')
        except Exception as e:
            print(f"[OTA_SCRIPT] Ошибка чтения version.h: {e}")

    new_name = f"firmCar_{version}.bin"
    new_path = os.path.join(build_dir, env_name, new_name)

    # 3. ПРОВЕРКА НАЛИЧИЯ И ОТПРАВКА
    if os.path.exists(firmware_path):
        shutil.copy2(firmware_path, new_path)
        print(f"\n[OTA_SCRIPT] Файл готов: {new_name}")

        try:
            android_dest = f"/sdcard/Download/{new_name}"
            adb_cmd = get_adb_command()
            print(f"[OTA_SCRIPT] ADB: Использую {adb_cmd}")
            print(f"[OTA_SCRIPT] ADB: Отправка в {android_dest}...")

            # shell=True критичен для Windows для поиска adb в PATH
            push_cmd = f'{adb_cmd} push "{new_path}" "{android_dest}"'
            subprocess.run(push_cmd, check=True, timeout=30, capture_output=True, shell=True)
            print(f"[OTA_SCRIPT] Успешно доставлено на устройство.")

            # Обновление индекса файлов Android
            scan_cmd = f'{adb_cmd} shell am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d "file://{android_dest}"'
            subprocess.run(scan_cmd, capture_output=True, shell=True)
            print(f"[OTA_SCRIPT] MediaStore обновлен. Файл виден в проводнике.")

        except subprocess.CalledProcessError as e:
            # На Windows системные ошибки часто в кодировке cp866
            try:
                err = e.stderr.decode('cp866').strip()
            except:
                err = e.stderr.decode('utf-8', errors='ignore').strip()

            if "not recognized" in err or "७  譥" in err:
                print(f"[OTA_SCRIPT] Ошибка: Команда 'adb' не найдена.")
                print("[OTA_SCRIPT] СОВЕТ: Добавьте путь к platform-tools в переменную PATH или установите ANDROID_HOME.")
            else:
                print(f"[OTA_SCRIPT] Ошибка ADB: {err}")
        except Exception as e:
            print(f"[OTA_SCRIPT] Критическая ошибка: {e}")
    else:
        print(f"[OTA_SCRIPT] Ошибка: Не найден исходный файл {firmware_path}")
        print("[OTA_SCRIPT] Убедитесь, что проект скомпилирован.")

# --- ТОЧКА ВХОДА ---

# 1. Режим PlatformIO (регистрация хука)
if env:
    env.AddPostAction("$BUILD_DIR/firmware.bin", rename_and_push_firmware)

# 2. Режим CLI (прямой запуск: python push_firmware.py)
if __name__ == "__main__":
    print("\n" + "="*40)
    print("[OTA_SCRIPT] РУЧНОЙ ЗАПУСК (CLI MODE)")
    print("="*40)

    # Определяем папку, где лежит сам скрипт
    current_dir = os.path.dirname(os.path.abspath(__file__))

    # Согласно уточнению пользователя, скрипт и platformio.ini на одном уровне
    # Проверяем, действительно ли мы в корне проекта TripComputer
    if os.path.exists(os.path.join(current_dir, "platformio.ini")):
        root = current_dir
    else:
        # Если нет, ищем вверх (на случай, если запустили из подпапки)
        root = find_project_root(current_dir)

    if root:
        print(f"[OTA_SCRIPT] Проект найден: {root}")
        mock_env = {
            "PROJECT_DIR": root,
            "PROJECT_BUILD_DIR": os.path.join(root, ".pio", "build"),
            "PIOENV": "wemos_d1_mini32"
        }
        # Запускаем основную логику
        rename_and_push_firmware(env=mock_env)
    else:
        print("[OTA_SCRIPT] КРИТИЧЕСКАЯ ОШИБКА: platformio.ini не найден!")
        print(f"[OTA_SCRIPT] Текущая директория: {current_dir}")

    print("="*40 + "\n")
