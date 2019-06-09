## Robot -> Broker protocol

The communication between The MMBot (The Robot) and a crypto-market is provided through a broker. The broker is small program which is executed as single
linux process and comunnicates using pipes.

The Robot starts this broker once it is needed and keeps it running, until the robot is stopped. The connection is made using three pipes connected to **stdin**, **stdout** and **stderr**. So the broker reads its **stdin**, and replies to **stdout**. It can also log its actions to **stderr**, which is routed to the robot's logfile 

The communication is synchronous: request-reply style. The broker must only send one response to each request. The broker can also use stderr only if the response is expected (note: the whole line need to be written to the stderr, including "\n", otherwise, the communication can deadlock)

Every request is send on single line terminated by a new line character '\n'
The response should be send as a single line too.

There is no timeout. Every request must have response. The robot waits for response for infinite time.

Both request and response are valid JSONs (stderr can handle any arbitrary text)

### Request 

```
["function_name"]\n
["function_name", <argument>]\n
```

(the character '\n' represents the new line character)

There can be zero or one argument - multiple arguments are passes as a JSON object  

### Successful response

```
[ true ]\n
[ true, <return_value> ]\n
```

The function can return zero or one result. Multiple results are returnes as a JSON object

### Error response

```
[ false, "<error message>" ]\n
```

Errors are carried as exception through the robot's code and it depends on the situation how it is handled. In most cases, the robot skips the cycle and tries the to repeat the operation after a while.
If "placeOrder" fails, the order is considered as "not placed".

## Functions

### reset

```
["reset"]
```
Request to reset internal state of the broker. It is ideal place to clear any cache 
or perform garbage collecting. The function is called at the begining of every cycle.

#### Response
```
[ true ]
```

### getBalance

```
["getBalance", <symbol>]
```
Return wallet's balance for given <symbol>. Note that complete balance is required 
(blocked money for orders must not be substracted). Result is sent as single number

```
[ true, <number> ]
```

#### TIP: 
The broker can read balance for all symbols and cache the result for fast access. It
should clear the cache on **reset**


### getTicker

```
["getTicker", <pair>]
```

Returns ticker for given pair 

```
[ true, {"bid": <number>, "ask": <number>, "last": <number>, "timestamp": <number>} ]
```

#### TIP: 
To reduce count of requests, the broker can read multiple symbols and server them from cache until the "reset"

### getTrades

```
["getTrades", {"lastId": <value>, "fromTime": <number>, "pair": <pair>} ]
```

Returns trades associated with user account. 
* **lastId** - (optional) contains **id** of last received trade. It allows to receive
  trades created after that id. If not specified, the all trades are returned
* **fromTime** - (optional) returns only trades newer then specified time
* **pair** - specifies pair

```
[ true, { "id": <value> , 
        "time": <number>, 
        "size":<number>, 
       "price":<number>, 
    "eff_size":<number>, 
   "eff_price":<number>,
    } ]
```
* **id** - id of trade (can be string or number)
* **time** - timestamp
* **size** - amount of assets. The number is positive for buy, or negative for sell,
* **price** - trade price (rate)
* **eff_size** - effective amount after fees are removed (if applied on assets)
* **eff_price** - effective price after fees are removed (if applied on currency)

**example**

When fees are applied on currency, and we have following trade

```
BUY 2.1 BTC at 7520, fees 0.12%

size = 2.1
price = 7520
eff_size = 2.1
eff_price = 7529.024
```

```
SELL 1.5 BTC at 7654, fees 0.12%

size = -1.5
price = 7654
eff_size = -1.5
eff_price = 7644.8262
```


### getOpenOrders

```
["getOpenOrders", <pair>]
```
Returns open trades for given **pair**

```
[ true, {"id":<value>, "clientOrderId": <value>, "size": <number>, "price":<number> } ]
```

* **id** - id of order (string or number)
* **clientOrderId** - value passed with placeOrder as client's id. The robot uses a number
* **size** - size of order, amount of assets to trade. positive - buy, negaive - sell
* **price** - price of the order (LIMIT price)  
 
### placeOrder

```
["placeOrder", {"pair": <string>, 
                "size": <number>, 
               "price": <number>, 
       "clientOrderId": <value>, 
      "replaceOrderId": <value> }]
```

Places new order. The Robot always places LIMIT order. 

* **pair** - pair
* **size** - size of the order. Specify **negative** value for **SELL** and **positive** value to **BUY**
* **price** - LIMIT price
* **clientOrderId** - custom ID stored along with the order. The robot uses the custom ID to find its orders
* **replaceOrderId** - (optional), contains ID of order to replace. This order should be canceled before the new order is placed. If the argument is not given, the function places a new order

### getInfo 
```
["getInfo", <pair>]
```

Returns information about a trading pair

```
[ true, { "asset_step": <number>,
		   "currency_step": <number>,
		   "asset_symbol": <string>,
		   "currency_symbol": <string>,
		   "min_size": <number>,
		   "fees": <number>,
		   "feeScheme", "currency|assets|income|outcome"
		    }]
```

- **asset_step** - step on which the amount of the order can be specified (count
   of decimal places, but is represented as minimal fraction. For example 0.0001 represents 4 decimal numbers. 
- **currency_step** - step on which the price of the order can be specified (count
   of decimal places, but is represented as minimal fraction. For example 0.0001 represents 4 decimal numbers. 
- **asset_symbols** - string contains symbol for the assets. This symbol is used to
search the balance. Example: "XBT" 
- **currency_step** - string contains symbol for the currency. This symbol is used to search the balance. Example: "USD"
- **min_size** - minimal size of the order
- **fees** - maker fees as fraction of 1. If the fees are 0.1%, the correct value for this field is 0.001
- **feeScheme** - specifies how fees are substracted 
    - **currency** - fees are substracted from currency  
    - **assets** - fees are substracted from assets
    - **income** - fees are substracted from currency when sell, or assets when buy  
    - **outcome** - fees are substracted from currency when buy, or assets when sell

### getFees
```
["getFees", <pair> ]
```

Returns current fees for the pair. The method is called more often than getInfo, so this is the reason, why it exists separatedly. It is possible to cache this value. On some stockmarkets, there is possible fee discount on large volume, so robot can get always updated fees before it starts to trade on next cycle.

```
[ true, <number> ]
```

The returned value is franction of 1. Example 0.1% fees is sent as 0.001

### getAllPairs
```
["getAllPairs"]
```

Returns all available tradable pairs on stockmarket


```
[ true, [<string>,<string>,<string>,...] ]
```

## Debugging

To debug communaction, start the robot with the switch **-d**. The communaction is copied to the log file on **debug level**

 
 

 