## Reports API ##

```
GET /api/reports/
```

List of supported enpoints - not all reporting brokers can support all endpoints

```
{"entries":["...",...]}
```

----

```
GET /api/reports/report
```

Returns daily performance report (the same format as sent to SSE channel)

----

```
GET /api/reports/options
```
Returns array of options for some filters


----

```
GET /api/reports/traders
```
Returns list of all recorded traders 

* **id** - unique trader's id 
* **broker** - broker
* **asset** - asset
* **currency** - currency
* **started** - timestamp in milliseconds when trader was started
* **stopped** - timestamp in milliseconds when trader was stopped (last trade)
* **rpnl** - total realized profit
* **eq** - total equity change (includes upnl)
* **trades** - total trades
* **volume** - total volume

---
```
GET /api/reports/query
```
Execute query and return list 

Arguments

* **year** - query specific year - mutable with **start_date**
* **month** - query specific month - if not specified, whole year is returned
* **start_date** - specifies begin of the range - timestamp in milliseconds. However, time is rounded to near whole day - mutable with **year**
* **end_date** - specifies end of the range. Optional, however, works only when **start_date** is used
* **asset** - filter for specified asset
* **currency** - filter for specified currency
* **broker** - filter for specified broker
* **trader** - filter for specified trader
* **skipdel** - (=1) skip deleted records 
* **aggregate** - (=1) aggregate trades to whole days - includes skipdel

Result

**aggregate=1**

List of days - each day contains a date and an object with currencies. Each currency contains two numbers [rpnl, equity_change]

**aggregate=0**

List of trades, each as single object

* **uid** - record uid
* **tim** - timestamp in milliseconds
* **trd** - trade's id
* **ast** - asset
* **cur** - currency
* **brk** - broker
* **tid** - trade id
* **prc** - price
* **tsz** - trade size (+) buy, (-) sell, (0) alert
* **pos** - final position
* **chg** - equity change before trade happened
* **rpnl** - realized profit
* **inv** - inverted price 
* **del** - deleted record

----

```
POST /api/reports/deltrade
```

Controls deletion flag for particular trade. When trade is deleted, it is removed from aggregations.
However, deleted trade is still available and can be undeleted anytime later

Arguments are specified as query string. Body is empty

* **uid** record uid
* **tim** record time
* **trd** trader identifier
* **del** delete (=1), undelete (=0)

Note, **uid**, **tim** and **trd** must be exact copy from list of trades returned by **query** otherwise operation can fail. This is needed to ensure, that correct record has been specified.

Returns 202 Accepted. The body contains application/json content "OK"




