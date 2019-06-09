# Command Line Interface

The bot can be controlled from the command line. 

## Command line format

```
bin/mmbot [-vdt] -f <config_file> <cmd> <args...>
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

### logrotate

Closes and re-opens logs. Useful as postrotate action of `logrotate`

## Bot's control commands

### calc_range

Calculates available trading range (min and max price) for each trading pair. 

### get\_all\_pairs <_broker_>

Prints all trading pairs for given broker

```
$ bin/mmbot get_all_pairs poloniex
```

### erase\_trade <_trader_> <_trade-id_>

Erases single trade from the history. It is usefull when you need to hide a trade
executed out of strategy.Erased trade disappears from charts and calculations.

To retrieve <trade-id> click and hold on trade line in the web browser. 

You cannot erase the most recent trade, because it is used to find last trade-id. So if the recent trade is erased, it reappears after a while

The effect of the deletion can be seen in web browser after 2 or 3 minutes

### resync\_trades\_from <_trader_> <_trade-id_>

Erases all recent trades starting by specified <trade-id>. This causes that the Bot resynces all missing trades from the stockmarket. 

To retrieve <trade-id> click and hold on trade line in the web browser. 

The function doesn't work in dry_run mode. It cannot perform the resync after erase.

  