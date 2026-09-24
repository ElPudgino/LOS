# Тест радиоканала CC1101 на Arduino Nano

Набор скетчей для проверки двух модулей CC1101 на частоте 433.92 МГц.  Проект последовательно проверяет SPI, пакетный обмен и демонстрирует передачу текста из одного UART в другой через радио.

## Результат

Проверены два варианта рабочего пакета CC1101:

- скетчи с библиотекой `SmartRC_CC1101`;
- минимальные скетчи только с `SPI.h` и прямыми регистрами CC1101.

Bare-metal вариант передаёт 16 последовательных пакетов. Для рабочего профиля используются 2-FSK, 4.8 кбод, частота 433.92 МГц, 16-битное синхрослово `D391`, FIFO пакетов переменной длины и CRC.

## Стенд и подключение

Используются две Arduino Nano (ATmega328P) и два CC1101. На каждой плате:

| Сигнал CC1101 | Пин Arduino Nano | Назначение |
|---|---:|---|
| `CSn` | 10 (`SS`) | выбор микросхемы SPI |
| `MOSI` | 11 | SPI MOSI |
| `MISO` | 12 | SPI MISO |
| `SCK` | 13 | SPI clock |
| `GDO0` | 2 | граница пакета (sync -> конец пакета) |
| `VCC` | 3.3 V | питание модуля |
| `GND` | GND | общая земля |

Подробная схема: [`doc/cc1101-arduino-nano.md`](../doc/cc1101-arduino-nano.md).

### Важно: уровни напряжения

CC1101 питается от 3.3 В. Абсолютный максимум на его цифровых входах - 3.9 В.  Выводы Nano работают с логикой 5 В, поэтому на линиях **CS, MOSI и SCK** нужен понижающий преобразователь уровней до 3.3 В. `MISO` идёт от CC1101 к Nano и имеет уровень 3.3 В, его обычно можно соединять напрямую.

Не стоит рассчитывать на вывод `3V3` Nano как на универсальный мощный источник питания. Для постоянной работы предпочтителен отдельный стабильный регулятор 3.3 В с общей землёй с Arduino.

## Скетчи

### `cc1101-probe/`

**Назначение:** проверить, отвечает ли CC1101 по SPI, без сторонней библиотеки.

Скетч выполняет `SRES`, читает `PARTNUM`, `VERSION`, `MARCSTATE` и набор конфигурационных регистров. Он полезен первым при диагностике проводов, питания и SPI.

Ожидаемый признак исправной старой ревизии чипа:

```text
PARTNUM=00 VERSION=04 MARCSTATE=01
=> CC1101 OK
```

`VERSION=04` - допустимая старая ревизия CC1101. Подробнее о заголовках SPI, статус-регистрах и 6-битной адресации - в [`cc1101-probe/README.md`](cc1101-probe/README.md).

### `cc1101-link-tx/` и `cc1101-link-rx/`

**Назначение:** пакетный link test с библиотекой
`SmartRC-CC1101-Driver-Lib`.

Передатчик раз в секунду посылает четыре байта:

```text
A5 sequence 5A (sequence XOR 5A)
```

Приёмник проверяет структуру полезной нагрузки и печатает номер, RSSI и LQI.

В этих скетчах явно задано:

```cpp
radio.setModulation(0);        // 2-FSK
radio.setDRate(4.8);           // 4.8 кбод
radio.SpiWriteReg(0x08, 0x05); // normal FIFO packet mode, CRC, variable length
radio.SpiWriteReg(0x02, 0x06); // GDO0: sync -> конец пакета
```

Вывод RX:

```text
RX #0 rssi=-45 lqi=132
RX #1 rssi=-44 lqi=131
```

Для этих скетчей библиотека должна быть установлена в Arduino libraries, например в `~/Arduino/libraries/SmartRC-CC1101-Driver-Lib`.

### `cc1101-uart-radio-tx/` и `cc1101-radio-uart-rx/`

**Назначение:** наглядная демонстрация UART -> радио -> UART.

- `cc1101-uart-radio-tx` читает доступные байты UART, собирает до 32 байтов в один радио-пакет и отправляет его.
- `cc1101-radio-uart-rx` получает пакет и выводит его байты в UART без текстовых отладочных сообщений.

Это сохраняет пробелы, CR/LF и другие обычные байты.

### `cc1101-link-bare-tx/` и `cc1101-link-bare-rx/`

**Назначение:** минимальный link test без `SmartRC_CC1101`.

Эти скетчи включают только `SPI.h` и сами реализуют:

- последовательность сброса `SRES`;
- ожидание готовности CC1101 по MISO;
- запись фиксированного набора регистров;
- strobe-команды (`SIDLE`, `SFTX`, `STX`, `SRX`, `SFRX`);
- чтение/запись TX/RX FIFO;
- ожидание GDO0 на границе пакета;
- проверку CRC и содержимого тестового пакета.

Профиль фиксирован в массиве `CONFIG` в обоих файлах.

Ожидаемый вывод:

```text
bare TX ready
TX #0
TX #1
```

```text
bare RX ready
RX #0 rssi_raw=0x3D lqi=5
RX #1 rssi_raw=0x3D lqi=5
```

## Сборка и прошивка

Из каталога нужного скетча:

```sh
arduino-cli compile --fqbn arduino:avr:nano --build-path ./.build
arduino-cli upload --fqbn arduino:avr:nano --build-path ./.build --port /dev/ttyUSB0
```

Для второй платы укажите `/dev/ttyUSB1`. Например, для bare-metal link test:

```sh
cd cc1101-link-bare-tx
arduino-cli compile --fqbn arduino:avr:nano --build-path ./.build
arduino-cli upload --fqbn arduino:avr:nano --build-path ./.build --port /dev/ttyUSB0

cd ../cc1101-link-bare-rx
arduino-cli compile --fqbn arduino:avr:nano --build-path ./.build
arduino-cli upload --fqbn arduino:avr:nano --build-path ./.build --port /dev/ttyUSB1
```

## Чтение UART

Скорость всех скетчей - 9600 бод. Открытие USB-UART часто сбрасывает Nano по DTR, поэтому после открытия порта нужно дать скетчу стартовать.

Просмотр диагностического вывода:

```sh
stty -F /dev/ttyUSB1 9600 raw -echo -hupcl
cat /dev/ttyUSB1
```

Однократная отправка строки в UART-радиопередатчик (пауза даёт Nano завершить сброс после открытия порта):

```sh
(sleep 2; printf 'Radio demo: 123!\r\n') > /dev/ttyUSB0
```

Параллельно в другом терминале запустите чтение UART-радиоприёмника:

```sh
stty -F /dev/ttyUSB1 9600 raw -echo -hupcl
cat /dev/ttyUSB1
```
