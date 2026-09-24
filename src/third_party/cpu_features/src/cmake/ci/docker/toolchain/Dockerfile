# Create a virtual environment with all tools installed
# ref: https://hub.docker.com/_/ubuntu
FROM ubuntu:latest AS env
LABEL maintainer="corentinl@google.com"
# Install system build dependencies
ENV PATH=/usr/local/bin:$PATH
RUN apt-get update -qq \
&& DEBIAN_FRONTEND=noninteractive apt-get install -yq \
 git wget libssl-dev \
 build-essential cmake ninja-build \
 python3 python3-venv \
 pkgconf libglib2.0-dev \
&& apt-get clean \
&& rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
ENTRYPOINT ["/usr/bin/bash", "-c"]
CMD ["/usr/bin/bash"]

FROM env AS devel
WORKDIR /home/project
COPY . .

ARG TARGET
ENV TARGET=${TARGET:-unknown}

FROM devel AS build
RUN cmake -version
RUN ./scripts/run_integration.sh build

FROM build AS test
RUN ./scripts/run_integration.sh qemu
RUN ./scripts/run_integration.sh test
