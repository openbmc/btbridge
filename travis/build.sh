#!/bin/bash
set -evx

Dockerfile=$(cat << EOF
FROM ubuntu:16.04
RUN DEBIAN_FRONTEND=noninteractive apt-get update && apt-get upgrade -yy
RUN DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -yy make gcc libsystemd-dev libc6-dev pkg-config
RUN mkdir /var/run/dbus
RUN groupadd -g ${GROUPS} ${USER} && useradd -d ${HOME} -m -u ${UID} -g ${GROUPS} ${USER}
USER ${USER}
ENV HOME ${HOME}
RUN /bin/bash
EOF
)

docker pull ubuntu:16.04
docker build -t temp - <<< "${Dockerfile}"

sudo cp ./travis/org.openbmc.HostIpmi.conf.test /etc/dbus-1/system.d/org.openbmc.HostIpmi.conf
sudo service dbus restart

gcc --version

mkdir -p linux
wget https://raw.githubusercontent.com/openbmc/linux/dev-4.3/include/uapi/linux/bt-host.h -O linux/bt-host.h

docker run --cap-add=sys_admin --net=host --rm=true --user="${USER}" \
 -w "${PWD}" -v "${HOME}":"${HOME}" -t temp make KERNEL_HEADERS=$PWD

docker run --cap-add=sys_admin --net=host -v /var/run/dbus:/var/run/dbus --rm=true --user="${USER}" \
 -w "${PWD}" -v "${HOME}":"${HOME}" -t temp ./travis/run_tests.sh

