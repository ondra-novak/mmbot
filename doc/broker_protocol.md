# Broker protocol

## Communication

MMBot communicates with the broker through **stdin**, **stdout** and **stderr**. The broker
is started when it is needed and it is kept running until it is no longer needed. 

**stdin** - the broker reads stdin to receive commands
**stdout** - the broker uses stdout to send replies and results
**stderr** - the broker uses stderr to send log or debug messages

Only one command is send at once. No pipelineing

### Command format

Command is send as single line JSON array. Inside of the array are allowed tha all types
of JSON elements.

```
[ "function" ]                  (1)
[ "function", <arguments> ]     (2)

```

 - **(1)** - function without arguments
 - **(2)** - function with argument. Only one argument is allowed. Multiple arguments
 must be packed as an array or an object


## Response format

```
[ true ]                          (1)
[ true , <return value> ]         (2)
[ false , "error description" ]   (3)
```
 - **(1)** - Command has been executed, but did not return any value
 - **(2)** - Command has been executed and returns a value
 - **(3)** - Command failed to execute, other value contains an error message

## Timeouts

There is a timeout to response, it can be set in config (broker_timeout) and default value
is 10sec. 

If the broker needs more time, it can send an empty line, which resets the timer. 

## Logging

The broker process can use **stderr** to send log messages. This output is copied
to MMBot's log file - every line also contains date and time and name of the broker

**NOTE!** - Every log line must be terminated with **\n** (a new line character). 
A missing terminating character can cause deadlock, because MMBot stucks waiting on 
terminating charactes. 

**Multithreading note** - Only the thread which is processing messages should 
send log messages. The log messages can be send only when MMBot waiting for a reply. 

## What is new

### trading instances (v2)

In v2 protocol, old calls marked as v1 are deprecated, and the new brokers don't need to
implemented at all. The v2 protocol allows to have trading instances. The instance is
represented as arbitrary JSON object, which is carried as argument of a call and if the
broker make a change in the instance, it must be also carried with the result out. Then the
trading instance is stored in the state of the trader. 

The purpose of trading instance is to help brokers to store some intermediate data with
the state of the trader. It also helps to track orders and distinguish orders placed by the robot and orders placed by the user manually. Optionally it can also track fills made by orders placed by the robots so fills made by orders placed manually cannot influence the trading (however this feature is planned to mmbot 3)

The broker is still considered as stateless, it doesn't need to store and manage trading instances. The trading instance can be anytime discarded without noticing the broker. To cover this case, the broker should include whole state to any new instance (which is requested by passing `null` as instance) - all open orders (but not fills) and anything what was related to other instances in the past.

### new trading calls (v2)

there are two new trading calls that replace old deprecated calls

* getTradingStatus
* placeOrders

### downloadMinuteData doesn't support OHLC

Currently this call doesn't support OHLC, so it is expected, that each minute is passed as single number. To cover whole OHLC, it is better to use 4min chart with agregated O,H,L,C passed in this order, or use 5 min chart with O,H,(H+L)/2,L,C passed in this order. Backtest is unable to benefit from more accurate data.


## Functions

### General

#### getBrokerInfo 

```
[ "getBrokerInfo" ]
```

Returns basic informations about the broker process

- **name** (string) - name of the exchange
- **url** (string) - url to exchange's homepage
- **version** (string) - version of the broker process (ex: 1.0.0)
- **licence** (string) - text of the licence
- **trading_enabled** (boolean) - must be **true** to trade. The value **false** is used to indicate, that broker is not properly set up (for example, missing API keys)
- **settings** (boolean) - contains **true**, when the broker supports additional settings (getSettings / setSettings / restoreSettings)
- **subaccounts** (boolean) - contains **true**, when the broker supports subaccounts
- **favicon** (string) - contains icon image in png format encoded as base64
                       

#### reset

```
[ "reset" ]
```

Called by the robot before every cycle. The broker can perform some cleanup, for example
clean any cached market data

The most common technique to reduce trafic is to cache market data. They should be cached
on the first read and cleared on **reset**

#### enableDebug

```
[ "enableDebug", true/false ]
```


Enables or disables debug output. This function is called as very first function right
after connection to the broker is established. The parameter contains boolean value

**true** - enable debug output. Verbose any action to log output
**false** - disable debug output. Use log output only for error messages

 


#### subaccount

```
[ "subaccount", [ "id", "function" ] ]             (1)
[ "subaccount", [ "id", "function", <args> ] ]     (2)
```

Execute an command under given subaccount. **function** can be any supported function,
except **subaccount** itself.  

The **id** of subaccount can be any arbitrary id (can contain only alphanumeric characters). The broker process should store any data under this ID to separate data
from main account.

#### testCall

This function is intended for debugging purpose. The broker can define any function which is accessed by this call and this can be used to test other features which
are not accessible by other functions defined on the API.

```
[ "testCall",  "cmd" ]                  (1)
[ "testCall",  ["cmd"] ]                (2)
[ "testCall",  ["cmd", <args>] ]        (3)
```

- **(1)** - call command without aruments
- **(2)** - call command without aruments
- **(2)** - call command with aruments


**Return value**

Function can return any arbitrary JSON value if function is supported. If function is not supported, returns standard error response

**Note** - Function can be implemented through **testCall()** defined in **api.h**



### Market data

#### getMarkets

```
[ "getMarkets" ]
```

Returns all tradable markets in structured JSON object

```
[ true, {....} ]
```

Each item uses key as label, which can be displayed to user. The value can
be either another object, or string. This allows to group markets to thematic groups.
The string value contains the market/pair identifier.

Example:

```
{
   "Leveraged": {
                "BTC/USD":"BTC-PERP",
                "ETH/USD":"ETH-PERP"
                },
   "Spot": {
           "BTC/USD":"BTC_USD",
           "ETH/USD":"ETH_USD"
           }
}
```

#### getInfo

```
[ "getInfo", "<market>" ]
```

Retrieves basic informations about specific market. Return value is JSON object:

* **asset_symbol** (string) - contains symbol which is traded (to buy or sell)
* **asset_step** (number) - contains minimal step in which asset can be traded (step of amount).
* **currency_symbol** (string) - contains symbol which is used to pay trades or to quote the price - unless **inverted_symbol** is used
* **currency_step** (number) - contains minimal step of the quote.
* **min_size** (number) - minimum amount allowed to trade - set 0 if there is no such minimum
* **min_volume** (number) - minimum volume allowed to trade (price * size) - set 0 if there is no such minimum
* **fees** (number) - fees (maker fees) in base form (1% is written as 0.01)
* **feeScheme** (string) - how fees are applied
    * "income" - fees are extracted from incoming asset or currency. This means 'assets' for buy, or 'currency' for sell
    * "outcome" - fees are extracted from outcoming asset or currenct. This means 'currency' for buy or 'assets' for sell
    * "currency" - fees are always extracted from currency
    * "assets" - fees are always extracted from assets
* **leverage** (number) - there is 0 for non-leveraged markets, or >0 for leveraged markets. The number specifies the available leverage. If there is variable leverage, this number should contain most representative leverage for the market. Note when leverage is enabled, the currency is used as collateral, not actual balance
* **invert_price** (boolean) - the value **true** indicates inverted futures. In this case, the price is reported in inverted value - for example, if BTC is quoted 10000, inverted price is 0.0001. 
* **inverted_symbol** (string) - this value is used only when **invert_price** is **true**.  This value contains symbol used to quote price. **currency_symbol** is used as collateral symbol, and **asset_symbol** as contract symbol. Example: - for inverted future BTC/USD, the **asset_symbol** is contract which is quoted in USD, **currency_symbol** is BTC, and **inverted_symbol** is USD. The price is reported in inverted form
* **simulator** (boolean) - set to **true** if the market is only simulator, not live market. This includes exchange sandboxes.
* **private_chart** (boolean) - set to **true** when prices read from this broker cannot be shared. This can be because price is not actual price, but it is derived from other market parameters.
* **wallet_id** (string) - if the exchange uses multiple wallets for set of markets, for example it has separate wallet for spot and leveraged markets. There can be any arbitrary string which identifies the wallet (for example: "spot" for spot markets and "leveraged" for leveraged markets) - this helps to determine, which symbols shares the same balance. If there is no such feature, leave this field empty


#### getTicker

```
[ "getTicker", "<market>" ]
```

Returns ticker for given market

* **bid** (number) - bid price
* **ask** (number) - ask price
* **last** (number) - last price
* **timestamp** (number) - timestamp in milliseconds	 

Note- ticker can be cached, it should be updated on or after **reset**


### API keys


#### getApiKeyFields

```
[ "getApiKeyFields" ]
```

Returns definition of a form which is filled by a user to supply the API keys. The filled fields are passed as JSON object to the function **setApiKey**. The format
of the definition is specified by the function **getSettings**

NOTE: It is forbidden to return current API key by this function

#### setApiKey

```
[ "setApiKey", {...} ]
[ "setApiKey", null ]
```

Sets new API key. Because each exchange have different key format, the parameter can by any arbitrary JSON unless **null** is used to delete current key

The parameter is retrieved from a form filled by a user. The form is built using the definition returned by function **getApiKeyFields**


### Account data and trading



#### getWallet

```
[ "getWallet" ]
```

Returns amount for all available assets on the account. Result is divided by wallets and symbols

```
{
   "wallet_name" : {
   			"symbol1":<amount>,
   			"symbol2":<amount>,
   			"symbol3":<amount>
   	},
   "wallet_name_2" :{ 
   
   
   }
}
```

**NOTE**: New version V2 calls. Some brokers don't need to support V1 calls

#### getTradingStatus (v2)

```
[ "getTradingStatus", [ <pair> , <instance> ] ]
```

Retrieves complete informations about the trading. This function replaces some V1 functions

* **pair** - pair symbol
* **instance** - arbitrary JSON which is under control of the broker. On the very first
call, you should pass **null**. The instance is returned in result and should be carried
to the next call of this function. It is also passed to the `placeOrders`.

Return value

```
[ {
    "instance": <instance>,
    "orders": [ {
                "id":<any>,
                "client_id":<any>,
                "size":<number>,
                "price":<number>,
                "partial":<bool>
                }...],
    "fills": [ {
                "size":<number>,
                "time":<number>,
                "price":<number>,
                "eff_price":<number>,
                "eff_size":<number>,
                "id":<any>,
                },...],
    "ticker": {"ask":<number>,
               "bid":<number>,
               "last":<number>, 
               "time":<number>}
    "position":<number>,
    "balance":<number>
}]
```
* **instance** - arbitrary JSON which must be preserved for the next call (optional, if not returned, then instance was not modified)
* **orders** - open orders related to the current instance, so the only orders placed for this instance should be returned. However, if the instance is set to **null** then all opened orders must be returned.
    * **id** - id of the order
    * **client_id** - id associated with the order, for V2 protocol is no longer used and it is presented for compatibility reasons. It is planned to be removed in future versions
    * **size** - size of the order, >0 buy, <0 sell (remaining size)
    * **price** - price of the LIMIT order
    * **partial** - if set true, the order is in partial execution state. This field can be filled only if this information is available.
* **fills** - new fills executed from last call
    * **size** - size of the execution (how many items has been traded)
    * **time** - timestamp of execution (in milliseconds UTC)
    * **price** - price of execution
    * **eff_price** - effective price, which is calculated as equivalent price of the trade after fees are substracted, for example, for fee 1%, effective price for execution made on 2100 USD is 2121 USD - so to buy assets at price 2100 USD with such fee is equivalent to buy at 2121 USD without fees
    * **eff_size** - effective size, which is actual increase or decrease of the position. In some cases, fees can be substracted from the assets (position), not from the balance. This field reflects this size apropriately
    * **id** - unique id of the trade (unique on given pair)    
* **ticker** - ticker
    * **bid** - bid
    * **ask** - ask
    * **last** - last price. If not available, set this field to sqrt(bid*ask)
    * **time** - timestamp of ticker informaton (in milliseconds UTC). The trading is stopped, if the ticker doesn't change 
* **position** - position (amount of assets)
* **balance** - balance (currency)



### placeOrders (v2)

```
[ "placeOrders", [ <pair> ,[{

        "price": <number>,
        "size": <number>,
        "id_replace": <any>,
        "size_replace": <number>,

    }...], <instance> ]
```

Places multiple orders 

* **pair** - specifies pair
* **price** - price of the LIMIT order. Should not be zero. Zero value is reserved for MARKET order, however, it is not yet implemented.
* **size** - size of the order >0 buy, <0 sell. If set to 0 while `id_replace` is defined, the command just cancels the order
* **id_replace** - *optional*: specifies the order to amend, replace, or cancel, depend on
situation. Some exchanges doesn't support amend, so the order is replaced. The replacement
should be handled carefuly to avoid possibility of double execution. For example, if the
order is in state PARTIAL_EXECUTED, it should not be replaced, but error should be emited.
* **size_replace** - *optional*: specifies minimum remaining size of the order in the time
of replacement to finish the operation. If the order has remaining minimum size less than
specified size, the order should not be replaced. If the replacing order cannot be safely
examined without canceling, failing this condition can be solved as not placing the new order and emit an error instead. (note the brokers should favor detection of PARTIAL_EXECUTED orders, which should not be replaced in general, they can be however canceled and let the robot consider how new order will be placed
* **instance** - an arbitrary JSON retrieved during `getMarketStatus` - contains trading instance


**Return value**

```
[ true, [ [<ok_or_error>,...], <instance> ] ]
```

* **ok_or_error** - this field can be either `bool` while it is set to true or false if the order was placed (or not placed if cancel requested), or a `string` contains error received by the exchange. The error is shown to user, there is no additional processing of these errors

* **instance** - *optional* : The broker can store anything to the instance, then it must return updated revision of the instance through this result. If the field missing, instance is not changed

                

#### getBalance (v1 - deprecated)

```
[ "getBalance", {"pair":"<market>","symbol":"<symbol>" }]
```

Retrieves balance of specified symbol. There is also market symbol to retrieve balance from correct wallet (if there are multiple wallets).

**return value**: number

**NOTE**: for leveraged markets, the symbol can be asset for position, or currency for collateral. The collateral must be in quote currency - if collateral is in
different currency, then it must be converted to quote currency. For position, it is also allowed to return negative value (short).

**TIP**: The most APIs returns account balance for all available symbols at once. The response can be cached for subsequent calls until the **reset** called

#### syncTrades (v1 - deprecated)

```
[ "syncTrades" , {"pair":"<market>"} ] 
[ "syncTrades" , {"pair":"<market>", "lastId": <any> } ] 
```

Synchronizes trades. Used to update trades incrementally. There is two ways how the function is called

##### without lastId

In this case, the function returns **lastId**, which will be used by next call, without returning any trade. It just stores **lastId** to be ready to read new trades. The returned **lastId** is used for the next call.

```
{"lastId": <any> }
```

Content of **lastId** can be any arbitrary JSON, it is not limited to single value. It can be object or array as well

##### with lastId

In this case, the function reads new trades created after the **lastId**. The function returns an array **trades** which contains new trades and new value of **lastId** which will be also used for the next call

```
{"lastId": <any>, "trades": [...] }
```
 
##### the structure of the trades

It is an array of objects. Objects are ordered by time of creation.

* **size** (number) - amount of traded. Negative value is sell, positive value is buy
* **price** (number) - price of the trade
* **time** (number) - timestamp of the trade in milliseconds
* **id** (any) - id of trade - can be any arbitrary JSON
* **eff_size** (any) - actual amount of assets added to wallet (or substracted from wallet) as it original size can be affected by a fee.
* **eff_price** (any) - calculated price (after fee is substracted) of the trade. Actually it is the price on which the equivalent trade could happen in case that fee is zero. For example, to buy on 1% fee, the **eff_price** will be 1% above the **price**.

  
#### getOpenOrders (v1 - deprecated)

```
[ "getOpenorders", "<market>" ] 
```

Retrieves currently opened (active) orders (LIMIT orders) 

**Return value**: an array of objects

* **id** - id of the order - can be any arbitrary JSON (not null)
* **price** - price of the order
* **size** - size of the order. The negative value is SELL, the positive value is BUY
* **clientOrderId** - contains an ID associated with the order which was set by **placeOrder** call. The robot uses this ID to mark orders to distinguish its orders from orders created by the user manually.

#### placeOrder (v1 - deprecated)

```
[ "placeOrder", {
      "size":<number>,
      "price":<number>,
      "clientOrderId:<number>}]   (1)
[ "placeOrder", {
      "size":<number>,
      "price":<number>,
      "clientOrderId:<number>,
      "replaceOrderId:<any>,
      "replaceOrderSide:<number>
      }]                          (2)
[ "placeOrder", {
      "size":0,
      "price":0,
      "replaceOrderId:<any>
      }]                          (3)
```

* **(1)** place new LIMIT order
* **(2)** replace or edit existing LIMIT order
* **(3)** cancel existing LIMIT order
 
* **size** - size of the order, the negative value is SELL, the positive value is BUY
* **price** - price of the order 
* **clientOrderId** - ID associated with the order. The ID is stored along with the order. The ID can be duplicated (some exchanges doesn't support duplicate ID's, it is broker's job to handle this situation correctly). (some exchanges doesn't support client id's at all, this is also broker's job to emulate such feature)
* **replaceOrderId** - if the order is replaced or edited, this field contains ID of the order (ID returned by getOpenOrders, not clientOrderId). In this case, old order should be canceled and new placed, or edited, if this feature is supported by the exchange
* **replaceOrderSize** - contains minimum remaining size (absolute value, regardless on whether it is sell or buy) to replace the order. If the order is already partially executed and remaining size is below specified size, the operation should be rejected complete. However many exchanges doesn't support such feature, they can only cancel the order and then read remaining size. So it is allowed to cancel the order and if the remaining size felt below required size, it should reject placing a new order. 


**NOTE** - if the **size** is zero, function should only cancel specified order, skip to place new one.
 
**Return value**: Function returns ID of the newly created order, **null** if the order was not placed, because **replaceOrderSize** was above remaining size. In case of cancel only operation the function should return **null**.

If the exchange rejects to place the order, the error message must be returned through error response

```
[ false, "Insufficient balance" ]
```

**NOTE** - function places only LIMIT order. It is also recommended to enable **post only** orders, especially when there are different fees for taker orders


### Settings

The broker must enable settings through **getBrokerInfo**. Settings are defined using custom format which defines a form to be filled by a user. The format is stored as structured JSON.

#### The form definition

The definition is parsed by the **formBuilder()** defined in **common.js**

##### Format basics

The format is JSON array. Each item in the array is object, which describes single control.

```
[
   {
     "type":"<type>",
     "name":"control1",
     "label":"label of control 1",
     "default":"default value of control 1,
     "options":{
     			"label1":"val1",
     			"label2":"val2",
     			"label3":"val3",...},
     "showif": {
                "control":[val1,val2,val3,...]
                ...
                },
     "...",
   },
   {
     "type":"<type>
     "name":"control2",
     "label":"label of control 2",
     "default":"default value of control 2,
   },
   ...
]
```

* **type** - specifies type of control
  * *string* - input line to enter a text
  * *number* - input line to enter a number
  * *textarea* - textarea
  * *enum* - combobox with predefined options
* **name** - name of the field. The entered value s stored under this field name
* **label** - just label of the control 
* **default** - optional, contains pre-filled value 
* **options** - an object consists from key-value items, where key is used as label and value is actual value stored to the field if the item is selected. This property is used only along with **enum** type
* **showif** - allows to hide certain controls depen on value of other controls. In most cases it is used along **enum** when user can select various modes and the form hides or show additional controls. The format of this property is key-value object, where each item contains key - name of control - and value - list of values, when the control is visible 

#### getSettings

```
[ "getSettings", "<market>" ]
```

Returns complete form defintion of the form with pre-filled values to modify values by the user. The argument **market** can be used as hint, where the function has been invoked by the user (i.e. which market has been selected). THe setting can be related to selected market (but this is not mandatory)

#### setSettings

```
[ "setSettings", {....} ]
```

Called after user fills or modifies the form and save the settings. The function receives object of key-value items, where key contains the name of the control and the value contains filled value.

**Return value**: Function can optionally return a complete settings as any arbitrary JSON, this allows to store settings inside of robot configuration, so the broker don't need to store settings on disk. When the broker is restarted, the settings is restored through function **restoreSettings**


#### restoreSettings

```
[ "restoreSettings", <any> ]
```

Called right after broker starts, and carries stored settings (which has been stored by function **setSettings**)

Function has no return value

### Misc

#### fetchPage
 
```
[ "fetchPage" ,{
		"method":<string>,
		"path":<string>,
		"headers":{....},
		"body":<string>
		}]
```

**Return value:** 


```
[ true ,{
		"code":<number>,
		"headers":{....},
		"body":<string>
		}]
```

Allows to broker to extend its settings by a web page. The posibilities of this feature is not limited

* **method** - POST, GET, PUT, DELETE, etc
* **path** - path relative to /brokers/<name>/page
* **headers** - headers as key-value - doesn't support duplicated headers
* **body** - content of request or response body
* **code** - response code (200,404, etc)


 
