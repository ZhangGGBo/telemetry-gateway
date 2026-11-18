FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake libboost-all-dev libsqlite3-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY tests tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2 && ctest --test-dir build --output-on-failure
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libsqlite3-0 && rm -rf /var/lib/apt/lists/* && useradd -m telemetry && mkdir /data && chown telemetry /data
COPY --from=builder /app/build/telemetry_server /usr/local/bin/
COPY config/docker.json /app/config.json
USER telemetry
WORKDIR /data
EXPOSE 9000 8080
ENTRYPOINT ["/usr/local/bin/telemetry_server","/app/config.json"]
