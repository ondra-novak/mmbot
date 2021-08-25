# Backtest API - CZ

Popis protokolu pro generování backtestů přes REST api robota

## Endpoint

**admin/api/backtest2**

Method: **POST**, data: **application/json**

## Volba funkce

Funkce se volí pomocí pomocí URI - jméno funkce je přidané jako další položka cesty endpointu

**admin/api/backtest2/XXXXXX**

## Autorizace

Standardni autorizace "Basic "

## Motivace

Hlavní motivací je umožnit spouštění backtestů vícero klienty aniž by došlo ovlivnění výsledků mezi klienty.

Pro zajištění rychlého výpočtu a minimalizace přenosů dat mezi klientem a serverem, jsou data pro výpočet ukládána 
také na serveru. Stará verze systému ale trpí způsobem implementace cachování, který neumožňuje možnost používání
funkce vícero klienty naráz. Nová verze zavádí systém cachování, která toto umožňuje. Je však potřeba významně
upravit klienta, aby s novým systémem uměl pracovat

## Za data je zodpovědný klient

Klient je povinný držet všechna data, která jsou potřeba pro výpočet backtestu. Je případně povinný aktualizovat
obsah cache na serveru, aby server měl k dispozici data pro výpočet

### Základní schéma výpočtu

Optimální způsob

```
    data(klient) ---> backtest(server) ---> výsledky(klient)
```

Toto schéma trpí nízkou mírou efektivity, protože pro každou iteraci je nutné zasílat kompletní data na server, 
přestože se mohou lišit jen minimálně. Proto mmbot zavádí možnost cachování dat na serveru

```
	data(klient)--> upload(server) --> parametry(klient) --> backtest(server) --> výsledky(klient)
```

Toto schéma umožňuje opakovat backtest se stejnými daty, pouze změnou parametrů bez nutnosti znovu uploadovat data.
Současná podoba backtestu se skládá z dvou částí. V první části se počítá spread, v druhé pak probíhá vlastní
 backtest. Během výpočtu spreadu se generují obchody které jsou pak dopočteny v rámci backtestu.
 
 Aby bylo možné se vyhnout neustálému přepočítávání spreadu, je postup dále rozdělen
 
 ```
	data(klient)--> 
	   upload(server) --> 
	      parametry(klient) --> 
	        gen_trades(server) -->
	          parametry(klient) --> 
	              backtest(server) --> výsledky(klient)
```

Během **gen_trades** se generuje graf obchodů, které se též ukládají do cache. Na tento graf se pak lze odvodlat 
při backtestu aniž by bylo nutné data stahovat a znova uploadovat
 
### Reference dat v cache

Všechna generovaná data jsou označena svým **id**, což je řetězec znaků. Po uložení dat do cache je vráceno 
v odpovědi `{id: "xxxx"}` - toto ID lze následně použít ve výpočtech jako parametr `{source: "xxxx"}`
 
### Platnost nahraných dat v cache

Cache má jen omezenou velikost a platnost. V případě, že je velikost cache je překročena, data jsou označena za 
neplatná a smazaná. V takovém případě reference na ji6 neplatná data způsobí, že volaná metoda skončí stavovým 
kódem **410 Gone**. Předpokládá se, že klient obnoví stav cache (uploaduje data do cache) aby mohl pokračovat 
ve výpočtu. Klient by vždy měl na chybu 410 reagovat obnovením cache (upload) a opakováním výpočtu

### Získání generovaných dat

Některé metody generují data do cache, ale nevrací vygenerovaný obsah. Aby mohl klient v případě smazání dat z cache
provést reupload, musí si po vygenerování dat tyto data z cache stáhnout. K tomu slouží funkce **get_file**, která
očekává jediný parametr a to **source**, odpovědí je pak JSON obsahující vygenerovaná data

**Poznámka:** smyslem separátní operace je zejména urychlit výpočty a mezivýsledky stahovat dodatečně nebo na pozadí.
Této vlastnosti využívá zejména webový klient aby mohl co nejrychleji reprezentovat výsledky testu a v době, 
kdy uživatel studuje výsledky může provést stažení mezivýsledků a nagenerovaných souborů pro případnou obnovu.

## funkce

### upload

Funkce očekává přímo obsah souboru jako JSON, vrací `{id:"xxxx"}` uploadovaných dat. Funkce neřeší typ ani účel dat,
pouze data uloží pod danné ID. Je na klientovi, aby znal typ dat a uměl je pak správně použít

### historical_chart

Nahraje do cache historická data. Parametry jsou

```
{"asset":"XXX","currency":"XXX", "smooth":nnn}
```
Parametr "smooth" je nepovinný a pokud je uveden, je zadán v minutách.

Výsledkem operace je `{id:"xxxx"}`

### trader_chart

Nahraje do cache data z vybraného tradera

```
{"trader","id"}
```

Výsledkem operace je `{id:"xxxx"}`

### trader_minute_chart

Nahraje do cache minutový graf z vybraného tradera. Minutový graf obsahuje data za 10 dní

```
{"trader","id"}
```

Výsledkem operace je `{id:"xxxx"}`

##random_chart

Vygeneruje náhodny graf

Očekává parametry `seed`, `volatility`, `noise`

Výsledkem operace je `{id:"xxxx"}`

##gen_trades

Generuje obchody podle nastavení spreadu.

```
"source","sma","stdev","force_spread","mult",
"raise","fall","cap","mode","sliding"."dyn_mult",
"reverse","invert","ifutures";

```

Výsledkem operace je `{id:"xxxx"}`

##run

Spustí backtest

```
"trader":<id tradera>
"source":<id chart>
"reverse":<otoc graf>
"invert":<inv graf>;
"config":<config tradera>
"init_pos":<initial pozice>
"balance":<currency balance>
"init_price":<pocatecni cena>
"neg_bal":<pokracuj pri zaporne balanci>
```
Výsledkem operace je seznam obchodů

