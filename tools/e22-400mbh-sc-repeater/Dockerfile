FROM python:3.13-slim@sha256:6771159cd4fa5d9bba1258caf0b82e6b73458c694d178ad97c5e925c2d0e1a91

ARG PLATFORMIO_VERSION=6.1.19

RUN python -m pip install --no-cache-dir --disable-pip-version-check --root-user-action=ignore \
    "platformio==${PLATFORMIO_VERSION}"

ENV PLATFORMIO_DISABLE_UPGRADE_CHECK=true

WORKDIR /workspace
ENTRYPOINT ["pio"]
CMD ["--help"]
