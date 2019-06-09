# install sequence

Instalation has been performed on clean ubuntu bionic (chroot, debootstrap, minverse)

```
$ apt install nano
```

```
$ nano /etc/apt/sources.list
```

Add **universe** 

```
$ apt update
$ apt install cmake make g++ git libcurl4-openssl-dev libssl-dev libcurlpp-dev
```

choose directory for source

```
$ git clone https://github.com/ondra-novak/mmbot.git
$ cd mmbot
$ git submodule update --init
$ cmake .
$ make all
$ mkdir data
$ mkdir log

```

## next steps

* configuration
* command line interface
* dry-run
* live
* web interface
 