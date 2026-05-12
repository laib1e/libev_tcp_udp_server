# libev TCP/UDP Echo Server

Неблокирующий echo-server на C++11, POSIX sockets и libev. Проект содержит две транспортные реализации: TCP и UDP. Общий класс `Server<Protocol>` владеет event loop и делегирует транспортно-специфичную логику классу протокола.

## Что реализовано

- TCP echo-server:
  - неблокирующий listening socket;
  - `accept()` в цикле до `EAGAIN` / `EWOULDBLOCK`;
  - отдельное состояние для каждого TCP-клиента;
  - `read_watcher_` и `write_watcher_` на каждое клиентское соединение;
  - per-client `out_buffer_` и `out_buffer_offset_` для частичных `send()`;
  - безопасное закрытие клиентских fd через RAII-обёртку `Fd`.

- UDP echo-server:
  - один неблокирующий UDP socket;
  - один `read_watcher_` и один `write_watcher_` на транспорт;
  - `recvfrom()` читает датаграммы целиком;
  - `sendto()` отправляет ответ отправителю;
  - если отправка временно невозможна, ответ сохраняется в очередь датаграмм.

- Dependency management:
  - сначала используется системный `libev`, если он найден CMake;
  - если системного `libev` нет, собирается из `third_party/libev`.

## Архитектура

```text
Server<Protocol>
  ├── ev_loop* loop_
  └── Protocol proto_

TCP protocol
  ├── listen socket
  ├── accept_watcher_
  └── unordered_map<int, unique_ptr<ClientConnection>> clients_

ClientConnection
  ├── Fd socket_
  ├── ev_io read_watcher_
  ├── ev_io write_watcher_
  ├── vector<char> out_buffer_
  └── size_t out_buffer_offset_

UDP protocol
  ├── Fd socket_
  ├── ev_io read_watcher_
  ├── ev_io write_watcher_
  └── deque<Datagram> datagrams
```

Главное отличие TCP от UDP: TCP работает с потоком байтов и требует per-client state, а UDP работает с целыми датаграммами и не создаёт отдельного socket fd на клиента.

## Сборка

```bash
cmake -S . -B build
cmake --build build
```

После сборки исполняемый файл находится здесь:

```bash
./build/libev_tcp_udp_server
```

Если CMake нашёл системный `libev`, будет использована системная библиотека. Иначе будет собрана из `third_party/libev`.

## Запуск

В текущей версии транспорт выбирается в `main.cpp` через шаблонный параметр:

```cpp
auto serv = std::unique_ptr<Server<UDP>>(new Server<UDP>(port));
```

Для UDP:

```cpp
auto serv = std::unique_ptr<Server<UDP>>(new Server<UDP>(port));
```

Для TCP:

```cpp
auto serv = std::unique_ptr<Server<TCP>>(new Server<TCP>(port));
```

После выбора транспорта пересоберите проект:

```bash
cmake --build build
./build/libev_tcp_udp_server
```

## Проверка UDP

В одном терминале запустить сервер:

```bash
./build/libev_tcp_udp_server
```

В другом терминале можно проверить через `nc`:

```bash
echo "hello" | nc -u 127.0.0.1 port
```

Или через клиент из соседнего проекта `libev_tcp_udp_client`.

## Проверка TCP

В `main.cpp` выбрать `Server<TCP>`, пересобрать сервер и запустить:

```bash
./build/libev_tcp_udp_server
```

Проверка через `nc`:

```bash
nc 127.0.0.1 port
```

## Важные детали реализации

### Почему `Fd` оборачивает raw fd

POSIX file descriptor — это обычный `int`, но он выражает владение ресурсом неявно. `Fd` делает владение явным:

- закрывает fd в деструкторе;
- запрещает копирование;
- разрешает move-семантику;
- предоставляет `reset()` для безопасной замены fd.

Это защищает от утечек и двойного освобождения ресурсов.

### Почему TCP использует `out_buffer_ + offset`

TCP — byte stream. Один `send()` не обязан отправить весь буфер. Поэтому отправленные байты не удаляются из `vector` сразу. Вместо этого хранится `out_buffer_offset_`, указывающий на первую ещё не отправленную позицию.

Когда весь буфер отправлен:

```text
out_buffer_.clear()
out_buffer_offset_ = 0
write_watcher_ выключается
```

### Почему UDP использует очередь датаграмм

UDP сохраняет границы сообщений. Одна входная датаграмма должна стать одним echo-ответом. Если `sendto()` временно возвращает `EAGAIN`, датаграмма сохраняется целиком вместе с адресом получателя и позже отправляется из `write_cb`.

Для UDP нет `ClientConnection`, потому что нет `accept()` и отдельного client fd.

## Ограничения

- Максимальный размер payload ограничен `MTU = 1400`.
- Нет лимита на размер TCP `out_buffer_` для медленных клиентов; для production-кода нужен backpressure / max pending bytes.
- UDP очередь также должна иметь лимит в production-варианте.
- В текущей версии выбор TCP/UDP делается изменением `main.cpp`, а не CLI-аргументом.

## Что можно улучшить

- Добавить лимиты очередей и backpressure.
- Добавить логирование peer address.
