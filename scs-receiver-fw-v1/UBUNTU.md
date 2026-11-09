# Сборка проекта в Ubuntu 24.04 LTS и 26.04 LTS

Инструкция проверена на x86-64 в чистых Docker-контейнерах `ubuntu:24.04` и `ubuntu:26.04`. Для обеих систем используется Arduino CLI 1.5.1 и Arduino AVR Core 1.8.8. 

## 1. Установка зависимостей

`build-essential` предоставляет `make` и компилятор C++ для хост-тестов. Внешние Arduino-библиотеки не требуются: используется библиотека `SPI` из Arduino AVR Core.

```sh
sudo apt update
sudo apt install -y build-essential curl ca-certificates
```

В Ubuntu 24.04 пакета `arduino-cli` в штатном репозитории нет. Чтобы на обеих версии Ubuntu использовать одинаковую версию инструмента, установите официальный бинарный выпуск:

```sh
ARDUINO_CLI_VERSION=1.5.1
curl -fL \
  "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz" \
  -o /tmp/arduino-cli.tar.gz
sudo tar -xzf /tmp/arduino-cli.tar.gz -C /usr/local/bin arduino-cli
rm /tmp/arduino-cli.tar.gz
arduino-cli version
```

Последняя команда должна показать версию `1.5.1`. Для системы не на x86-64 нужен архив соответствующей архитектуры со [страницы выпусков Arduino CLI](https://github.com/arduino/arduino-cli/releases); эта инструкция такие системы не проверяет.

## 2. Установка Arduino AVR Core

Команды выполняются один раз для текущего пользователя. `sudo` не нужен.

```sh
arduino-cli core update-index
arduino-cli core install arduino:avr
arduino-cli core list
```

В выводе `core list` должна быть строка `arduino:avr`; при проверке инструкции установилась версия `1.8.8`.

### Автономная установка Core из архива

Если на целевой машине нет доступа к `downloads.arduino.cc`, используйте архив `arduino-avr-offline-cache.tar.gz` с кэшем Arduino CLI. Одного архива самого Core недостаточно: нужны также AVR GCC и инструменты, которые CLI 1.5.1 устанавливает при первом запуске. Контрольная сумма находится в `arduino-avr-offline-cache.tar.gz.sha256`.

В архиве должны быть индексы:

```text
package_index.json
package_index.json.sig
library_index.json
library_index.json.sig
```

И каталог `staging/packages/` с девятью файлами для Linux x86-64:

```text
avr-1.8.8.tar.bz2
arduinoOTA-1.3.0-linux_amd64.tar.bz2
avr-gcc-7.3.0-atmel3.6.1-arduino7-x86_64-pc-linux-gnu.tar.bz2
avrdude_8.0-arduino.1_Linux_64bit.tar.gz
ctags-5.8-arduino11-pm-x86_64-pc-linux-gnu.tar.bz2
dfu-discovery_v0.1.2_Linux_64bit.tar.gz
mdns-discovery_v1.1.0_Linux_64bit.tar.gz
serial-discovery_v1.5.2_Linux_64bit.tar.gz
serial-monitor_v0.15.0_Linux_64bit.tar.gz
```

Это официальные файлы с `downloads.arduino.cc`; размер готового архива вместе с индексами — около 62 МиБ. Имена и URL всех файлов Core и его зависимостей также указаны в `package_index.json`. Нужны именно перечисленные версии: им должен соответствовать Arduino CLI 1.5.1.

На машине без сети распакуйте кэш и установите Core:

```sh
sha256sum -c arduino-avr-offline-cache.tar.gz.sha256
mkdir -p "$HOME/.arduino15"
tar -xzf arduino-avr-offline-cache.tar.gz -C "$HOME/.arduino15"
arduino-cli core install arduino:avr
arduino-cli core list
```

Команду `core update-index` при автономной установке выполнять не нужно: она обращается к сети. Если в `~/.arduino15` уже есть другие платы или библиотеки, предварительно сохраните этот каталог.

## 3. Тесты и сборка прошивки

Перейдите в корень репозитория, где находится `Makefile`:

```sh
cd /path/to/scs-receiver-fw-v1
make clean
make test
make compile
```

Успешные хост-тесты завершаются сообщениями:

```text
burst tests passed
radio tests passed
loop/heartbeat tests passed
```

По умолчанию прошивка собирается для `arduino:avr:nano`. Результаты находятся в `.build/arduino/`, основные файлы:

- `scs-receiver.ino.hex` — прошивка для загрузки;
- `scs-receiver.ino.elf` — ELF с отладочной информацией и таблицей символов.

При проверке компилятор сообщил:

```text
Sketch uses 4656 bytes (15%) of program storage space. Maximum is 30720 bytes.
Global variables use 881 bytes (43%) of dynamic memory, leaving 1167 bytes for local variables. Maximum is 2048 bytes.
```

## Варианты сборки

Для Arduino Nano со старым загрузчиком:

```sh
make compile FQBN=arduino:avr:nano:cpu=atmega328old
```

Для изменения параметров компиляции, например таймаута группирования принятых пакетов:

```sh
make compile EXTRA_FLAGS='-DBURST_TIMEOUT_MS=1500UL'
```

Для повторной полной проверки после изменения исходников:

```sh
make clean && make test && make compile
```

## Проверка в Docker

В чистых контейнерах обеих версий Ubuntu были успешно выполнены:

1. установка зависимостей и Arduino CLI 1.5.1;
2. установка Arduino AVR Core 1.8.8;
3. `make clean` и `make test`;
4. `make compile` для `arduino:avr:nano`;
5. `make compile FQBN=arduino:avr:nano:cpu=atmega328old`;
6. `make compile EXTRA_FLAGS='-DBURST_TIMEOUT_MS=1500UL'`;
7. проверка наличия непустого `.build/arduino/scs-receiver.ino.hex`.

Во всех проверках использовались одинаковые исходники проекта, монтируемые в контейнер только для чтения; сборка выполнялась в отдельном рабочем каталоге контейнера.

После включения VPN установка была повторена в чистом контейнере с пустым `~/.arduino15`: `core update-index` и `core install arduino:avr` успешно загрузили все файлы с `downloads.arduino.cc`. Из них создан архив. Затем архив проверен в свежих контейнерах Ubuntu 24.04 и 26.04 с Docker-опцией `--network none`: в обоих случаях установка Core из кэша и компиляция прошивки завершились успешно без обращения к сети.
