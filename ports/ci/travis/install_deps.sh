#!/bin/sh

if [ "${DOCKERSYS}" = debian ]; then
    docker exec ${DOCKERSYS} apt-get -y update
    docker exec ${DOCKERSYS} apt-get -y upgrade

    docker exec ${DOCKERSYS} apt-get -y install \
        ccache \
        clang \
        pkg-config \
        linux-libc-dev \
        qt5-qmake \
        qtdeclarative5-dev \
        qtmultimedia5-dev \
        libqt5opengl5-dev \
        libqt5svg5-dev \
        libavcodec-dev \
        libavdevice-dev \
        libavformat-dev \
        libavutil-dev \
        libswresample-dev \
        libswscale-dev \
        libgstreamer-plugins-base1.0-dev \
        libpulse-dev \
        libjack-dev \
        libasound2-dev \
        libv4l-dev
elif [ "${DOCKERSYS}" = fedora ]; then
    docker exec ${DOCKERSYS} dnf install -y https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-${FEDORAVER}.noarch.rpm
    docker exec ${DOCKERSYS} dnf install -y https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-${FEDORAVER}.noarch.rpm
    docker exec ${DOCKERSYS} yum -y update

    docker exec ${DOCKERSYS} yum -y install \
        ccache \
        clang \
        make \
        gcc-c++ \
        qt5-qttools-devel \
        qt5-qtdeclarative-devel \
        qt5-qtmultimedia-devel \
        qt5-qtsvg-devel \
        ffmpeg-devel \
        gstreamer1-plugins-base-devel \
        libv4l-devel \
        alsa-lib-devel \
        pulseaudio-libs-devel \
        jack-audio-connection-kit-devel
elif [ "${DOCKERSYS}" = opensuse ]; then
    docker exec ${DOCKERSYS} zypper -n update

    docker exec ${DOCKERSYS} zypper -n in \
        ccache \
        clang \
        libqt5-linguist \
        libqt5-qtbase-devel \
        libqt5-qtdeclarative-devel \
        libqt5-qtmultimedia-devel \
        libqt5-qtsvg-devel \
        ffmpeg-devel \
        gstreamer-plugins-base-devel \
        libv4l-devel \
        alsa-devel \
        libpulse-devel \
        libjack-devel
elif [ "${TRAVIS_OS_NAME}" = osx ]; then
    brew install \
        ccache \
        pkg-config \
        qt5 \
        ffmpeg \
        gstreamer \
        gst-plugins-base \
        pulseaudio \
        jack \
        libuvc
fi
