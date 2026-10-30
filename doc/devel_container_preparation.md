### Installing and starting the REST API server
```console
cd /src/api
python3 -m venv /tmp/venv
source /tmp/venv/bin/activate

poetry lock --no-update
poetry install --all-extras
poetry run poe codegen
poetry run poe run
```

### Regenerating API stubs
```
pip install fastapi-code-generator

poetry run poe generate_routes
```

### Building and executing the transcoder application
#### Compiling the transcoder application
```console
cd /src/
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ../transcoder
make -j
```

#### Installing additional GStreamer plugins (must be done from the root user context)
```console
cd /src/build
for p in $(find ./plugins/gstreamer/ -name *.so); do cp -av $p /usr/lib/x86_64-linux-gnu/gstreamer-1.0/; done
```

#### Building gst-plugins-bad on an older version of Ubuntu or locally (devel).
```console
DEB_BUILD_OPTIONS=nocheck debuild -us -uc -nc -i.* -I
```

#### Starting the transcoder application from the command line
```console
cd /src/build
./app/transcoder -c ../transcoder/config/example/srt_ts_to_ts.json -l ../transcoder/license/dev/licenses/offline.lic
```

#### Instalation of debug version of gstreamer libraries

```console

apt install ubuntu-dbgsym-keyring lsb-release

echo "deb http://ddebs.ubuntu.com $(lsb_release -cs) main restricted universe multiverse
deb http://ddebs.ubuntu.com $(lsb_release -cs)-updates main restricted universe multiverse
deb http://ddebs.ubuntu.com $(lsb_release -cs)-proposed main restricted universe multiverse" | \
tee -a /etc/apt/sources.list.d/ddebs.list

apt update
apt install libc6-dbg \
        libgstreamer1.0-0-dbgsym \
        gstreamer1.0-libav-dbgsym \
        gstreamer1.0-plugins-base-dbgsym \
        gstreamer1.0-plugins-good-dbgsym \
        gstreamer1.0-plugins-ugly-dbgsym

```