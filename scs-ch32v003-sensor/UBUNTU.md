# Сборка в Ubuntu 24.04 LTS и 26.04 LTS

Инструкция одинакова для обоих выпусков Ubuntu. Все команды, кроме установки пакетов, выполняются от обычного пользователя.

## 1. Установить зависимости

```sh
sudo apt update
sudo apt install -y \
    build-essential \
    gcc-riscv64-unknown-elf \
    binutils-riscv64-unknown-elf \
    python3
```

Назначение пакетов:

- `gcc-riscv64-unknown-elf` и `binutils-riscv64-unknown-elf` — кросс-компилятор и утилиты для bare-metal RISC-V;
- `build-essential` — GNU Make, host-компилятор и стандартные C-заголовки для тестов;
- `python3` — запуск хост-тестов.

Несмотря на имя пакета с `riscv64`, этот toolchain поддерживает требуемые проекту `rv32ec_zicsr_zifencei` и `ilp32e`. SDK WCH и RISC-V libc для сборки прошивки не нужны.

## 2. Собрать прошивку

Перейдите в корень проекта и задайте префикс утилит Ubuntu:

```sh
cd /путь/к/scs-ch32v003-sensor
export CROSS_COMPILE=riscv64-unknown-elf-
make
```

`export` действует только в текущем терминале. После открытия нового терминала команду нужно повторить. Вместо этого можно указывать префикс в каждом вызове:

```sh
make CROSS_COMPILE=riscv64-unknown-elf-
```

Без переопределения `CROSS_COMPILE` Makefile использует префикс `riscv32-none-elf-`, которого у пакетов Ubuntu нет.

При успешной сборке появятся файлы:

```text
build/16/64-0/sleep-1-29/buffer-32/firmware.elf
build/16/64-0/sleep-1-29/buffer-32/firmware.bin
build/16/64-0/sleep-1-29/buffer-32/firmware.hex
build/16/64-0/sleep-1-29/buffer-32/firmware.map
build/16/64-0/sleep-1-29/buffer-32/firmware.lst
```

Для записи через Ardulink обычно используется `firmware.bin`; команда приведена в разделе «Прошивка через Ardulink» файла `README.md`.

## 3. Запустить хост-тесты

Из корня проекта:

```sh
python3 -m unittest discover -s tests
```

Ожидаемый результат:

```text
Ran 3 tests

OK
```

Тесты выполняются на компьютере и не заменяют проверку прошивки на микроконтроллере.

## 4. Собрать с другими параметрами

Если `CROSS_COMPILE` уже экспортирован, параметры задаются обычными аргументами Make:

```sh
make SAMPLES=131
make BUFFER_CAPACITY=128
make CHARGE_LOOPS=1200 DISCHARGE_LOOPS=2400
make STANDBY=0
```

Для разных сочетаний параметров создаются отдельные каталоги внутри `build/`. Удалить все результаты сборки:

```sh
make clean
```

## Проверка в Docker

Инструкция проверена в чистых официальных Docker-образах Ubuntu на архитектуре `amd64`. В каждом окружении заново устанавливались перечисленные пакеты, затем выполнялись `make` и `python3 -m unittest discover -s tests`.

| Образ | Версия в контейнере | Cross GCC | Сборка | Хост-тесты |
| --- | --- | --- | --- | --- |
| `ubuntu:24.04` | Ubuntu 24.04.5 LTS | 13.2.0 | Успешно | 3 из 3 |
| `ubuntu:26.04` | Ubuntu 26.04.1 LTS | 14.2.0 | Успешно | 3 из 3 |

В обоих случаях созданы все пять файлов `firmware.{elf,bin,hex,map,lst}`. Размер прошивки зависит от версии toolchain. Аппаратная запись и работа на CH32V003 в Docker не проверялись.
