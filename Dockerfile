# syntax=docker/dockerfile:1.6
#
# Multi-stage build for svlens.
# Stage 1 (builder): pull build prerequisites, vendor slang, compile a static-leaning
# release binary against system libstdc++.
# Stage 2 (runtime): debian:stable-slim with only libstdc++ and the svlens binary.
#
# Build:    docker build -t svlens:dev .
# Run:      docker run --rm -v "$PWD:/work" -w /work svlens:dev conn -f rtl/filelist.f --top top
#
ARG DEBIAN_BASE=debian:stable-slim

# ---- builder ---------------------------------------------------------------
FROM ${DEBIAN_BASE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        python3 \
        ca-certificates \
        catch2 \
        libyaml-cpp-dev \
        libfmt-dev \
        zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy only what setup-deps needs first to leverage layer caching for slang.
COPY scripts/setup-deps.sh scripts/
RUN ./scripts/setup-deps.sh --prefix /usr/local

# Now bring in the rest of the source tree.
COPY . .

RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/usr/local \
        -DSVLENS_FETCH_DEPS=OFF \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build --prefix /usr/local

# ---- runtime ---------------------------------------------------------------
FROM ${DEBIAN_BASE} AS runtime

ENV DEBIAN_FRONTEND=noninteractive
# Install runtime libraries. libfmt-dev/libyaml-cpp-dev are used to avoid
# pinning a version-suffixed runtime package that varies between Debian
# releases; the dev metadata is small and keeps the image working across
# stable transitions.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libyaml-cpp-dev \
        libfmt-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Binary plus any shared libs installed alongside it (slang / svlens internals
# may live under /usr/local/lib).
COPY --from=builder /usr/local/bin/svlens /usr/local/bin/svlens
COPY --from=builder /usr/local/lib/ /usr/local/lib/

ENV LD_LIBRARY_PATH=/usr/local/lib

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/svlens"]
CMD ["--help"]
