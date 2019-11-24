## Návrh protokolu pro externí strategii

**Následující dokument nepopisuje existující funkci, jde pouze o návrh**

Protokol umožňuje implementovat další strategie pomocí externího procesu. Externí process komunikuje s robotem pomocí roury napojené na **stdin**,**stdout** a **stderr**. Robot zasílá externímu procesu příkazy po **stdin** a externí process odpovídá na **stdout**. Chyby a ladící výpisy může reportovat na **stderr**

Protokol je synchronní a poloduplexní. Buď vysílá robot příkaz, nebo externí process odesílá odpověď, nikdy ne současně.

### základní formát protokolu

Komunikuje se formátem JSON. Jedna datová věta na jednu řádku (není povoleno dělit JSON na víc řádek).

Pro účely srozumitelnosti je JSON v následujícím popisu formátován do snadno čitelné formy 

**Příkaz**

```
["<id-příkazu>",{... parametry....}]
```

**Odpověď na přikaz**


```
[true,<návratová hodnota>]
```

**Chyba nebo výjimka**

```
[false, "Popis chyby"]
```

### Objekt stavu strategie

Objekt strategie je koncipovaný jako imutabilní. Každý příkaz dostává configurační objekt a stavový objekt. Pokud se očekává, že příkaz mění stav strategie, je možné tento nový stav vrátit jako výsledek příkazu.

Externí process si nesmí žádný stav ukládat mimo stavový objekt. Pouze stavový objekt toiž určuje, jak volání po sobě následují. Je totiž pravděpodobné, že robot si uchovává kopie stavů tak aby mohl přepočítat strategii různých záchytných bodů. Může tak požádat o přepočítání z libovolného staršího stavu. (například při kreslení grafů) 

**Počáteční stav** stavového objektu je prázdný objekt. Je na strategii, aby si zajistila inicializaci ve vhodnou chvíli

**Perzistence** - stav se ukládá perzistentně. Process externí strategie může být mezitím několikrát ukončen, ale stav zůstane zachován.


### Příkazy

#### onIdle

Příkaz je volán kdykoliv robot čeká na exekuci vypsaných pokynů, ale k žádné exekuci zatím nedošlo. Toto se provádí zhruba 1x za minutu.

**Parametry**

```
{
	"config": Config,
	"state": State,
	"minfo": MarketInfo,
	"ticker": Ticker,
	"assets": number,
	"currency": number,
}
```

Kromě konfigu, stavu a informaci o trhu zasílá i aktuální ticker a zůstatek na účtu v assetech a currency

**Návratová hodnota**

```
State
```

Tento příkaz očekává aktualizaci stavu strategie. Pokud se aktualizace neprovedla, stačí vrátit obsah `state`.



#### onTrade

Příkaz je volán vždy, když je detekován obchod. Může být volán několikrát za sebou, aniž by došlo k volání ostatních příkazů. Předpokládá se, že strategie aktualizuje svůj stav a spočítá normalizovaný profit


```
{
	"config": Config,
	"state": State,
	"minfo": MarketInfo,
	"price": number,
	"size": number,
	"assets": number,
	"currency": number,
}
```

V položce **price** je cena exekuovaného pokynu. V položce **size** je objem, přičemž záporné číslo je prodej a kladné je nákup. Položky **assets** a **currency** obsahují aktuální zůstatky

**Návratová hodnota**

```
{
	"state":State,
	"norm_profit":double,
	"norm_assets": double
}
```
**norm_profit** je profit strategie včetně nerealizovaného zisku 
**norm_assets** obsahuje profit v assetech. Tyto dvě čísla vytváří celkový profit z daného obchodu. Tedy má strategie akumulovat assety (použít zisk k nákupu assetů), pak hlásí **norm_assets**, ale hondota **norm_profit** je nulová.


#### getBuyOrder, getSellOrder

Požádá strategii o zaslání dat pro vypsání pokynu (nákupní **getBuyOrder**, a prodejni **getSellOrder**). 

```
{
	"config": Config,
	"state": State,
	"minfo": MarketInfo,
	"new_price": number,
	"assets": number,
	"currency": number,
	"try": number,
}
```

Robot zašle navrhovanou cenu **new_price**. Vypsaný příkaz může být na jiné ceně, nicméně nelze vypsat nákupní příkaz nad aktuální cenou a prodejní příkaz pod aktuální cenou. Aktuální cena je buď cena posledního trade, nebo last cena tickeru.

Položka "try" obsahuje číslo pokusu volání. Volání může být opakováno s jinou cenou max_price pokud požadovaný pokyn nejde vypsat.

**Návratová hodnota**

```
{
	"price": number,
	"expected": number,
	"required": number	
}
```

Pokud je **price** nulová, nebo mimo povolený rozsah, použije robot **new_price**

Položka **expected** obsahuje očekávaný zůstatek na účtu v assetech před exekucí pokynu. Z různých důvodů může být jiný. Startegie by ale sem měla vložit číslo, které svou činnosti spočítala po předchozí exekuci. Rozdíl může nastat například při částečné exekuci

Položka **required** obsahuje požadovaný zůstatek na účtu v assetech po exekuci pokynu. Robot spočítá velikost pokynu podle aktuálního zůstatku s přihlédnutím na to, že velké rozdíly od **excepted** je schopen nějakým způsobem snížit.


Pokud je **size** nulová, pokyn se nevypíše

Pokud je **size** záporná, může se vypsat **dust_order**, ovšem kladný

Hodnota **size** se zaokrouhluje do X.9 dolu a nad X.9 nahoru. Pokud je po zaokrouhlení nula, může se volání opakovat s vyšší navrhovanou cenou. Aby se předšlo přetahování o cenu, může strategie zabránit špatnému zaokrouhlení tím, že dodrží nastavení kroku a minimálních objemů, které má k dispozici v **minfo**

**Pozn**: Prodejní pokyn se zasílá s kladným **size**.

#### calcSafeRange
 
Spočítá bezpečný rozsah

```
{
	"config": Config,
	"state": State,
	"minfo": MarketInfo,
	"assets": number,
	"currency": number,
}
```

**Návratová hodnota**

```
{
	"min": number,
	"max": number,	
}
```

#### getEquilibrium

Vrací číslo, které se zobrazí jako Equilibrium ve statistikách

```
{
	"config": Config,
	"state": State,
}
```


**Návratová hodnota**

```
number
```



### Struktura MarketInfo

```
{
	"asset_symbol":String,
	"currency_symbol":String,
	"asset_step":number,
	"currency_step":number,
	"min_size":number,
	"min_volume":number,
	"leverage":number,
	"invert_price":boolean,
	"inverted_symbol":String,
	"simulator":boolean,
}
```

### Struktura Ticker

```
{
	"ask":number,
	"bid":number,
	"last":number,
	"time":number
}
```

### Struktura State a Config

Struktury jsou zpravidla objekty volně definované pro účely strategie. 
Config vstupuje z nastavení, State si vprůběhu činnosti strategie udržuje sama 

