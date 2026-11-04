# Автономный кэш Arduino AVR

Набор файлов для установки Arduino AVR Core и сборки Arduino-прошивок проекта без загрузки пакетов из интернета. Архив рассчитан на Arduino CLI в Linux x86-64.

- `arduino-avr-offline-cache.tar.gz` — индексы пакетов, Arduino AVR Core 1.8.8, AVR GCC, avrdude и вспомогательные инструменты.
- `arduino-avr-offline-cache.tar.gz.sha256` — контрольная сумма архива.

Проверка целостности из каталога репозитория:

```sh
sha256sum -c arduino-avr-offline-cache.tar.gz.sha256
```
