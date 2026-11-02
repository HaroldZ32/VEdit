Empty on purpose.

The probe image build is invoked as:

    DOCKER_CONFIG=$PWD/probes/c17-overlay/image/.docker-config \
        docker build -t vprobe-qt6kf6:1 -f image/Dockerfile image/

This directory is only needed on a host where `~/.docker` is read-only (the
sandboxed development host). On a normal host `docker build` works without it;
nothing in the probe reads this directory at runtime.
