# INSTALL 

## FreeBSD

* Install required packages

```
$ pkg install cmake git gcc 
```
(you will also need openssl development package, but this package was found already installed on FreeBSD 12)

* Add an user

```
$ adduser
```
(now answer the questions)

* Switch to the user

```
$ su <user>
$ cd ~
```

* download from git and perform update

```
$ git clone https://github.com/ondra-novak/mmbot.git
$ cd mmbot
$ ./update
```

* first run using ssh tunnel

```
$ bin/mmbot -p <tunnel_port> start
```


You should install a webserver, and proxy_pass to `/home/<user>/mmbot/run/mmbot.socket`

### Debian/Ubuntu

* Install required packages

```
$ apt update
$ apt install git cmake make g++ libssl-dev
```

* Add and user and switch

```
$ adduser --disabled-password <user>
$ su <user>
$ cd ~
```

* download from git and perform update		

```
$ git clone https://github.com/ondra-novak/mmbot.git
$ cd mmbot
$ ./update
```

* first run using ssh tunnel

```
$ bin/mmbot -p <tunnel_port> start
```

You should install a webserver, and proxy_pass to `/home/<user>/mmbot/run/mmbot.socket`


### Fedora/CentOS

* Install required packages

```
$ yum install git gcc-c++ openssl-devel cmake make
```

* Add and user and switch

```
$ adduser <user>
$ su <user>
$ cd ~
```

* download from git and perform update		

```
$ git clone https://github.com/ondra-novak/mmbot.git
$ cd mmbot
$ ./update
```

* first run using ssh tunnel

```
$ bin/mmbot -p <tunnel_port> start
```

You should install a webserver, and proxy_pass to `/home/<user>/mmbot/run/mmbot.socket`





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


## automatic start after reboot 

You can use crontab to initiate job after reboot

### Debian/Ubuntu

```
$ crontab -e
```
now append `@reboot /home/mmbot/mmbot/bin/mmbot start`
and save

(why such path? /home/<user>/<instdir>/bin/mmbot)

### FreeBSD

* Create a file under `/usr/local/etc/rc.d/mmbot.sh`
* Put following command to the file:

```
#!/bin/sh
su <user> -c "/home/<user>/mmbot/bin/mmbot start"
```

## Set server for using https on your web domain
This tutorial is written for Ubuntu 18.04

### What do you need:

1. web server software - for this purpose we used [nginx](https://nginx.org/en/docs/)
2. your domain
3. [certbot](https://certbot.eff.org/) - for creating certificate etc.

### Install and setting web server nginx

It is recommended to upgrade your system.

So update your list:

```
sudo apt-get update
```

after updating info about packages let's upgrade them:

```
sudo apt-get upgrade
```

then install nginx:

```
sudo apt install nginx
```

Done!

So far you have to run your bot on some port. With nginx server you can run on IPv4 adress of your server directly, but we have to set in nginx the location of running socket.

For setting the location of mmbot socket you have to edit file default.

This file you should find in path:

```
/etc/nginx/sites-enabled/default
```
File default we have to edit, below is just exmaple how could you edit the file:
```
vim default
```
or
```
nano default
```

Examples above means - you move to folder sites-enabled and run file default with any editor you like for example vim or nano.

Inside the default file you will find some text for us is important this part:

```
location / {
    # First attempt to serve request as file, then
    # as directory, then fall back displaying a 404.
    try files $uri $uri/ =404;
 }
```
This part you have to change for location of your mmbot socket as:

```
location / {
    proxy_pass http://unix:/home/your_user/mmbot/run/mmbot.socket;
 }
```
With this settings location in nginx we will connect mmbot and nginx. When you edit file default save it.


It is goof practice to check if everything was done succesful.

You can check it as root by this command:
```
nginx -t
```
If everything alright you should get this message:
```
nginx: the configuration file /etc/nginx/nginx.conf syntax is ok
nginx: configuration file /etc/nginx/nginx.conf test is successful
```
Finally reload your nginx:
```
service nginx reload
```

At this moment nginx is set up for mmbot you can check it when you write IP of your server to your browser, you should get mmbot service. Now you can closed your port.
Just restart your mmbot without specific port to close the port:
```
your_user@name_your_server:~/mmbot$ bin/mmbot restart
```

Congratulations ! Your nginx is done with mmbot.

### Get your domain

It is necessary to get your domain because certbot is not able to create https with just IPv4 adress.

For this purpose you have to buy your domain which convert your IP to readebale and easy remembered form.
When you buy your domain you have to connect your domain with IP of your server:
Add your IP adress to **DNS reports** of your domain. This reports you will find on configuration website of your domain provider.
Then you have to wait. Process of adding new IP takes a quite long time (12 - 24h). Other servers has to confirm (detect) your change. Be patient !

### Start https on your domain via certbot

Installing certbot on your server as root:
```
apt install python-certbot-nginx
```

Add your domain to nginx:
* for adding edit file default on your server on path
```
/etc/nginx/sites-enabled/default
```
* inside file rewrite:

from
```
server_name _;
```

to

```
server_name your_domain.com;
```

Save and exit the file default.


Run this command to get a certificate and have Certbot edit your Nginx configuration automatically to serve it.

```
certbot --nginx
```

Fill your email and choose domain where you want to do https ceritficate.
Now certbot done everything automatically what is necessary to set for using https.


At the end restart your nginx:
```
service nginx restart
```

Now try to reload you website in browser you should see that your website is https and your transfer is secure.
