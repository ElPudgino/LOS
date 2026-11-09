# minichlink

**minichlink** — открытая утилита командной строки для прошивки и отладки микроконтроллеров CH32V003 через программатор WCH-LinkE (CH-LinkE). Она позволяет записывать и считывать прошивку, работать с памятью и регистрами, управлять питанием и состоянием микроконтроллера, а также запускать GDB-сервер для отладки.

Корневой `Makefile` запускает сборку в каталоге `minichlink` и создаёт исполняемый файл `minichlink/minichlink`.

## Сборка в Ubuntu

Установите компилятор, `make` и заголовочные файлы libusb1 и libudev:

```sh
sudo apt update
sudo apt install build-essential libusb-1.0-0-dev libudev-dev
```

Запустите сборку из корня репозитория:

```sh
make
```

Для удаления собранных файлов выполните:

```sh
make clean
```

## Проверка сборки в Docker

Команда ниже проверяет сборку на чистых образах Ubuntu 24.04 LTS и 26.04 LTS. Исходный каталог монтируется только для чтения, а сборка выполняется на его копии внутри контейнера.

```sh
for ubuntu_version in 24.04 26.04; do
  echo "=== Ubuntu ${ubuntu_version} ==="
  docker run --rm \
    --volume "$PWD:/src:ro" \
    "ubuntu:${ubuntu_version}" \
    sh -c '
      set -eu
      apt-get update
      DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential libusb-1.0-0-dev libudev-dev
      cp -a /src /build
      make -C /build
      test -x /build/minichlink/minichlink
    '
done
```
