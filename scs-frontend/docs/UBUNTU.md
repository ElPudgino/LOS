# Установка, сборка и запуск в Ubuntu

Инструкция проверена в чистых контейнерах **Ubuntu 24.04 LTS** (Python 3.12.3) и **Ubuntu 26.04.1 LTS** (Python 3.14.4). Требуется Python 3.11 или новее; Ubuntu 22.04 со штатным Python 3.10 не подходит.

## 1. Установить системные зависимости

```sh
sudo apt update
sudo apt install -y git make python3 python3-venv python3-pip
```

`python3-venv` нужен для изолированного окружения: устанавливать зависимости проекта через `sudo pip` не следует.

## 2. Получить исходники и создать окружение

Если репозиторий ещё не скопирован, выполните `git clone <URL-репозитория>` и перейдите в созданный каталог. Если исходники уже есть, сразу перейдите в корень проекта:

```sh
cd scs-frontend
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e '.[test]'
```

Последняя команда устанавливает проект в режиме разработки и зависимости для тестов. В новом терминале перед командами `python` нужно снова выполнить `source .venv/bin/activate` из корня проекта.

## 3. Запустить тесты

```sh
make test
```

Эквивалентная команда без Make:

```sh
python -m unittest discover -s tests -v
```

## 4. Запустить сервис

Из корня проекта, с активированным окружением:

```sh
python -m scs.app
```

Откройте <http://127.0.0.1:8000>. База данных по умолчанию создаётся в `data/telemetry.sqlite3` относительно текущего каталога. Для остановки нажмите Ctrl-C.

Другой путь к БД и HTTP-порт задаются так:

```sh
python -m scs.app --database /absolute/path/telemetry.sqlite3 --port 8001
```

Сервис продолжает работать, если последовательный порт `/dev/ttyACM0` отсутствует, и повторяет попытки подключения. Для работы с реальным приёмником пользователю нужны права на serial-порт, обычно через группу `dialout`:

```sh
sudo usermod -aG dialout "$USER"
```

После добавления в группу завершите сеанс и войдите снова. Проверить фактическое имя устройства можно командой `ls /dev/ttyACM* /dev/ttyUSB*`. Порт можно изменить в настройках интерфейса. Не открывайте его одновременно другой программой.

## Сборка дистрибутива

Для обычного запуска этот шаг не нужен. Чтобы собрать source archive и wheel:

```sh
python -m pip install build
python -m build
```

Готовые файлы появятся в `dist/`:

- `scs_telemetry-0.1.0.tar.gz` — исходный дистрибутив;
- `scs_telemetry-0.1.0-py3-none-any.whl` — устанавливаемый wheel.

Проверить установку wheel можно в отдельном чистом окружении:

```sh
python3 -m venv /tmp/scs-wheel-venv
/tmp/scs-wheel-venv/bin/python -m pip install dist/scs_telemetry-0.1.0-py3-none-any.whl
cd /tmp
/tmp/scs-wheel-venv/bin/python -m scs.app --database /tmp/scs-telemetry.sqlite3
```

Статические файлы интерфейса входят в wheel. После проверки временное окружение можно удалить: `rm -rf /tmp/scs-wheel-venv /tmp/scs-telemetry.sqlite3`.

## Возможные ошибки

- **`error: externally-managed-environment`** — команды `pip` выполняются вне виртуального окружения. Создайте `.venv` и активируйте его, как описано выше.
- **`ensurepip is not available`** — не установлен пакет `python3-venv`.
- **`No module named ...`** — не активировано окружение либо не выполнена установка `python -m pip install -e '.[test]'`.
- **Нет данных с приёмника** — проверьте имя serial-порта, права группы `dialout` и то, что порт не занят другой программой.
