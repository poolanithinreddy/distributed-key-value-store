FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake git ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKV_BUILD_TESTS=OFF && cmake --build build --parallel

FROM debian:bookworm-slim
RUN useradd --create-home kv
COPY --from=build /src/build/kv_node /usr/local/bin/kv_node
USER kv
WORKDIR /home/kv
ENTRYPOINT ["kv_node"]

