# Test-side X server + input injector for the M0 host matrix (T12).
#
# This is TEST TOOLING, not a product build image: the project's build images
# (scripts/docker/Dockerfile.build) are untouched. It provides a headless X
# server so the tested container can use the xcb platform without a display,
# and xdotool so the harness can act as "the user" (window lookup + key events).
#
# Built with:  docker build -t vedit-hostmatrix-xside:local -f tests/packaging/sidecar.Dockerfile tests/packaging
FROM docker.m.daocloud.io/library/ubuntu@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends xvfb xdotool imagemagick ca-certificates \
 && rm -rf /var/lib/apt/lists/*

ENV DISPLAY=:99
CMD ["sh", "-c", "Xvfb :99 -screen 0 1280x800x24 -nolisten tcp & sleep 1; exec tail -f /dev/null"]
