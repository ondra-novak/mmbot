# install sequence 

## prepare system (as root)

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

choose directory for source. It is strongly recomended to create a separate user for the robot

### create user (as root)

```
$ adduser --disabled-password --gecos "" mmbot
$ su mmbot
$ cd ~

```
Now you should install under mmbot user. 


## install 

```
$ git clone https://github.com/ondra-novak/mmbot.git
$ cd mmbot
$ ./update

```

## update to newest version

Just type ...  

```
$ ./update
```

... in the root directory of the project.

Note, you probably made changes in configuration files. If there is
some update in configuration files, they will be merged. However, if
the merge fails, you will need to manually merge changes and then 
mark the conflict resolved by calling

```
git add conf/<name of conf>
```

otherwise next update fails until the conflict is resolved.

## configure and run

 * configuration is under `conf` directory
 * trading pairs are added into `conf/traders.conf`
 * you can set API keys for various stockmarkets into configurations under `conf/brokers/` 
 * to start bot, type `$ bin/mmbot start`
 * to start bot with maximum debug informations, type `$ bin/mmbot -d start`
 * to start in verbose mode, type `$ bin/mmbot -v run`
 * to stop bot (not in verbose mode) type `$ bin/mmbot stop`
 

## automatic start after reboot 

You can use crontab to initiate job after reboot

```
$ crontab -e
```
now append `@reboot /home/mmbot/mmbot/bin/mmbot start`
and save

(why such path? /home/<user>/<instdir>/bin/mmbot)

## enable web interface (without web server)

just append following lines to `conf/mmbot.conf`

```
[report]
http_bind=*:12345
```
(ensure, that very last line is empty, otherwise it will not work)

You can specify different port if you don't like '12345'. If you wish to prevent accessing web interface from the internet, use `localhost` instead `*` (`localhost:12345`). Then, only local connection will be accepted.

## enable web interface through the web server (nginx, apache)

just configure to web server to point at directory `www`

(for nginx `root /home/mmbot/mmbot/www;` instead default path)

It is strongly recommended to configure https access (see letsencrypt, certbot, etc)


  
