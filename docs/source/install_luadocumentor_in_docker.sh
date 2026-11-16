#!/bin/bash -e

# OPENFALLOUT_LUADOC_INSTALL=1 allows it outside Docker, on a machine with a home folder of its own (a Mac, which has
# no Docker by default). The Documentation workflow uses the image of docs/Dockerfile.
if [ ! -f /.dockerenv ] && [ ! -f /home/docs/omw_luadoc_docker ] && [ "${OPENFALLOUT_LUADOC_INSTALL:-}" != 1 ]; then
    echo 'This script installs lua-5.1, luarocks, and openmwluadocumentor to $HOME. Should be used only in docker.'
    exit 1
fi

check_sha256() {
    if command -v sha256sum > /dev/null; then sha256sum -c; else shasum -a 256 -c; fi
}

echo "Install lua 5.1"
cd ~
curl -R -O https://gitlab.com/OpenMW/openmw-deps/-/raw/main/lua/lua-5.1.5.tar.gz
echo "2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333  lua-5.1.5.tar.gz" | check_sha256
tar -zxf lua-5.1.5.tar.gz
rm lua-5.1.5.tar.gz
cd lua-5.1.5/
if [ "$(uname -s)" = Darwin ]; then make macosx; else make linux; fi
cd ~
PATH=$PATH:~/lua-5.1.5/src

echo "Install luarocks"
luarocksV="3.9.2"
curl -R -O https://gitlab.com/OpenMW/openmw-deps/-/raw/main/lua/luarocks-$luarocksV.tar.gz
echo "bca6e4ecc02c203e070acdb5f586045d45c078896f6236eb46aa33ccd9b94edb  luarocks-$luarocksV.tar.gz" | check_sha256
tar zxpf luarocks-$luarocksV.tar.gz
rm luarocks-$luarocksV.tar.gz
cd luarocks-$luarocksV/
./configure --with-lua-bin=$HOME/lua-5.1.5/src --with-lua-include=$HOME/lua-5.1.5/src --prefix=$HOME/luarocks
make build
make install
cd ~
rm -r luarocks-$luarocksV
PATH=$PATH:~/luarocks/bin

echo "Install openmwluadocumentor"
git clone https://gitlab.com/openmw/openmw-luadocumentor.git
cd openmw-luadocumentor
git checkout 542006fceec9f0cc2a53cc9c7b06ce185f20237b
luarocks make luarocks/openmwluadocumentor-0.2.1-1.rockspec
cd ~
rm -r openmw-luadocumentor
