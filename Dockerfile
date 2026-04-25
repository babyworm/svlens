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
    && cmake --install build --prefix /usr/local \
    && /usr/local/bin/svlens --version \
    && if ldd /usr/local/bin/svlens | grep -q 'not found'; then \
           echo 'ERROR: svlens has unresolved dynamic dependencies'; \
           ldd /usr/local/bin/svlens; \
           exit 1; \
       fi

# Stage the runtime payload at a known path so the final stage uses an
# explicit allow-list rather than copying /usr/local/lib wholesale. With
# scripts/setup-deps.sh's default config slang builds STATIC, so the
# libslang*.so* glob may not match anything; that is expected and the
# binary is self-contained for slang. The libsvlens*.so* glob is kept as
# a forward guard in case a future cmake target ever produces a shared
# library.
RUN install -d /runtime/usr/local/bin /runtime/usr/local/lib \
    && install -m 0755 /usr/local/bin/svlens /runtime/usr/local/bin/svlens \
    && for lib in /usr/local/lib/libslang*.so* /usr/local/lib/libsvlens*.so*; do \
           [ -e "$lib" ] && cp -a "$lib" /runtime/usr/local/lib/ || true; \
       done \
    && ls -la /runtime/usr/local/bin/ /runtime/usr/local/lib/

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

# Copy only the staged runtime payload (binary + slang/svlens shared libs)
# rather than the entire /usr/local/lib tree. This keeps the image
# attack surface bounded to artefacts the build pipeline produced.
COPY --from=builder /runtime/usr/local/ /usr/local/

ENV LD_LIBRARY_PATH=/usr/local/lib

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/svlens"]
CMD ["--help"]
