#!/usr/bin/env bash
# Сборка Yasura (ARK-4 для PSP-E1000) на Ubuntu 24.04
# Запускать из корня репозитория: ./build-yasura.sh
set -e
sudo apt-get install -y p7zip-full genisoimage python3-pycryptodome python3-ecdsa zlib1g-dev libmpfr-dev build-essential git curl
git submodule update --init --depth 1
sudo 7z x -y -o/usr/local contrib/PC/PSPSDK/pspdev.7z && sudo chown -R "$USER" /usr/local/pspdev
sudo ln -sf /usr/lib/x86_64-linux-gnu/libmpfr.so.6 /usr/lib/x86_64-linux-gnu/libmpfr.so.4
export PSPDEV=/usr/local/pspdev PATH=/usr/local/pspdev/bin:$PATH ARKROOT="$PWD"
command -v sign_np >/dev/null || ( cd /tmp && rm -rf sign_np && git clone https://github.com/swarzesherz/sign_np.git && sed -i 's/o64/o/' sign_np/sign_np.c && make -C sign_np && mv sign_np/sign_np $PSPDEV/bin/ )
command -v mkpsxiso >/dev/null || ( cd /tmp && curl -sSLO https://github.com/Lameguy64/mkpsxiso/releases/download/v2.03/mkpsxiso-2.03-Linux.deb && sudo apt-get install -y ./mkpsxiso-2.03-Linux.deb )
make
echo "Готово: файлы в dist/"
