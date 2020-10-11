# Command Line Interface

The bot can be controlled from the command line. 

## Command line format

```
bin/mmbot [-vdt] [-f <config_file>] [-p <number>] <cmd> <args...>
```

Switches must appear before the <cmd> because all arguments after the <cmd> are passed as arguments of the command.

## Switches

### Switch -v

Redirects log to stderr ("verbose mode"). This switch cannot be used when the bot starts in daemon mode

### Switch -d

Temporarily enforces `debug` mode of logging ("debug")

### Switch -t

Enforces `dry_run=1` for all trading pairs ("test")

### Switch -f

Changes location of configuration file. Default location is at path `../conf/mmbot.conf` relative to bot's binary file

### Switch -p

Opens port to access web interface. Number specifies port to open. The port is bound to localhost - this option cannot be used to open port to the internet.


## Service control commands

### start

Starts the Bot in daemon mode

### stop

Stops the running Bot 

### restart

Restarts the running Bot (combination of stop+start)

### status

Shows status of the Bot

### pidof

If the Bot is already running, it shows its PID

### wait

Holds until the Bot is stopped
