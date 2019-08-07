# Konfigurační soubor

Nastavení robota je uloženo v konfiguračním souboru. Robot hledá konfigurační soubor v adresáři `../conf` pod názvem odvozeného z názvu pod kterým je spuštěn a přidáním `conf` jako přípony

```
bin/mmbot -> bin/../conf/mmbot.conf
```

Cestu na config lze změnit parametrem `-f`

Konfigurační soubor je jeden, ale může se odkazovat na další konfigurační soubory pomocí directivy `@include` (viz dále)

## Formát konfiguračního souboru

- **sekce**: `[nazev_sekce]`
- **klíč a hodnota** `klic=hodnota` - Řetezcové hodnoty se __nepíší__ do uvozovek. Text hodnoty je zakončen znakem pro nový řádek. Pokud je třeba hodnotu rozdělit an víc řádků, pak lze použít zpětné lomýtko před koncem řádku, čímž se označuje pokračování. Samotný znak zpětného lomídka a oddělovač řádky není součástí hodnoty


```
[nazev_sekce]
ciselna_hodnota=10
retezcova_hodnota=Ahoj světe
viceradkova_hodnota=toto je zacatek \
	tady to pokracuje \
	a tady je konec
```

### Relativní cesty

Pokud je hodnotou cesta na nějaký soubor, složky a podobně, zpravidla jako výchozí cesta bývá cesta ke konfiguračnímu souboru, kde se hodnota nachází. Lze používat **relativní** cesty

```
logfile=../log/logfile
```

Pokud se hodnota nachází v konfiguračním souboru `/usr/local/share/mmbot/conf/mmbot.conf`, pak log bude generován do souboru `/usr/local/share/mmbot/conf/../log/logfile`



## Direktivy

**@include <soubor>** - vloží do konfiguračního souboru jiný soubor, který se také zpracuje jako část konfiguračního souboru. Cesta může být uvedena relativně. 

**@template <sekce>** - Použije vyjmenovanou sekci jako šablonu hodnot, pro hodnoty, které nejsou uvedeny v aktuálni sekci.

```
@include vedlejsi_trhy.conf

[coinmate_template]
start_time=1557924975877
broker=coinmate
dynmult_raise=200
dynmult_fall=5

[btcczk]
@template coinmate_template
pair_symbol=BTC_CZK
external_assets=0.5
title=BTC/CZK
```

## Komentáře

Komentáře se píší na samostatný řadek za znak `#`. Komentář nelze zapsat za hodnutu (stane se součástí hodnoty)

```
# Toto je komentar

klic=hodnota # Toto neni komentar
```
 
## Popis položek konfiguračního souboru

### [service]

**inst_file** = cesta na soubor představující běžící instanci. Existence souboru slouží jako kontrola, že jiná instance již běží. Zároveň je to socket, který slouží ke komunikaci s běžící robotem

**name** = (volitelný) Název programu, objevuje se v různých výpisech v konzoli atd.

**user** = (volitelný) Změní efektivního uživatele po startu služby. Položka obsahuje jméno uživatele nebo uživatel:skupina. S tímto nastavením je třeba službu pouštět jako **root**. Po spuštění běží služba jako nastavený uživatel. 

**Poznámka** nedoporučuje se pouštět robota s právy **root** z bezpečnostních důvodů


### [log]

Nastavuje logování

**file** = jméno souboru, do kterého program loguje svou činnost

**level** = level: **debug**,**info**,**progress**,**note**,**warning**,**error**,**fatal**,


### [report]

Řídí funkci reportování výsledků robota. Zpravidla se výsledky robota prezentují prostřednictvím webové aplikace v adresáří `www`. Robot do tohoto adresáře pravidelně ukládá soubor s výsledky, které webová aplikace čte a prezentuje

**path** = cesta do složky, kam se report ukládá. Soubor se vždy jmenuje `report.json` 

**interval** = (volitelné) určuje, jak staré informace se reportují. Čas je v milisekundách a výchozí hodnota je `864000000`

**http_bind** = (volitelné) Pokud je položka uvedena, robot bude fungovat jako jednoduchý `http` pro prohlížení výsledků. To položky uveďte adresu a port - viz příklady

```
#
# pouze místní prohlížení
# adresa stránek http://localhost:12345/
#
http_bind=localhost:12345
```

```
#
# přístup na stránky ze sítě
# adresa stránek http://<IP-adresa-počítače>:22334
#
http_bind=*:22334
```

Před dvojtečku se píše buď `localhost` nebo `127.0.0.1`, pak stránky budou k dispozici jen na počítači, kde robot běží a nebude možné je prohlížet ze sítě. Nebo lze uvést jméno počítače (domény) na které robot běří, případně hvězdičku (`*`) pokud má být stránka dostupna ze všech sítí, do kterých je počítač připojen

Za dvojtečku se píše číslo volného portu v rozsahu 1024-65535. Čísla obsazených portů poskytne `netstat -tan | grep LISTEN`, všechny ostatní porty jsou volné

**Bezpečnostní upozornění:** Nedoporučuje se místní server vystavovat veřejnému internetu. Není stavěn na zátěž kterou může z internetu obdržet a není připraven na případný pokusy o hack serveru a průnik hackerů do vašeho počítače. Pokud si chcete prohlížet výsledky vzdáleně ze svého domácího prohlížeče nebo z mobilního telefonu, doporučuje se nainstalovat webserver, například `Nginx` nebo `Apache`. Pak už stačí pouze namapovat adresář `www` na veřejnou URL (případně zabezpečit přístup heslem - viz návod k webserveru) a lokální server robota vypnout odstraněním řádky `http_bind`

 **http_auth** = (volitelné) Aktivuje základní zabezpečení stránky heslem. Funguje pouze pokud je **http_bind** nastaveno a přístup probíhá zkrze nastavenou adresu. Hodnotou tohoto klíče jsou mezerou oddělené tokeny, které jsou vygenerované z kombinace jméno a heslo následujícím způsobem (v shellu). Dva příklady:
 
```
$ echo -n franta:abc123 | base64
ZnJhbnRhOmFiYzEyMw==
$ echo -n karel:tajneHesl0 | base64
a2FyZWw6dGFqbmVIZXNsMA==
```

Klíč **http_auth** pak bude nastaven následovně

```
http_auth=ZnJhbnRhOmFiYzEyMw== a2FyZWw6dGFqbmVIZXNsMA==
```

Oba uživatelé (franta a karel) se budou schopni přihlásit svým jménem a heslem



### [brokers]

Obsahuje definice brokerů. Každý broker má definovaný identifikátor, který se pak použije u každého obchodovaného páru. Robot je tak schopen obchodovat na více burzách současně.

**<jmeno>** = V sekci [brokers] jsou všechny klíče považovány za deklaraci nového brokera. Název klíče je interpretován jako jméno brokera, hodnotou je příkazový řádek, který způsobí spuštění brokera a jeho napojení na robota. Příkazový řádek obsahuje cestu a jméno processu, který brokera implementuje a parametry, což je většinou cesta na jeho konfigurační soubor

**Potřebné soukromé a veřejné klíče na burzu jsou uloženy v konfiguračním souboru brokera**

```
[brokers]
coinmate=../bin/brokers/coinmate brokers/coinmate.conf
poloniex=../bin/brokers/poloniex brokers/poloniex.conf
```

Sekce **[brokers]** bývá často umístěna v samostatném souboru **brokers.conf** a tento soubor je do hlavního konfiguračního souboru vložen přes **@include brokers.conf**

### [traders]

Obsahuje seznam obchodovananých párů. Jméno sekce "obchodníci" je odvozena o toho, že v rámci robota působí pro každý pár virtuální digitální obchodník, který samostatně a nezávisle na ostatních obchoduje na daném páru.

Sekce [traders] je rozdělena do dvou souborů. V hlavní konfiguračním souboru lze najít položku:

```
[traders]
storage_path=../data
```

Definuje cestu, kam si jednotliví obchodníci ukládají pracovní a stavová data. Čím déle robot pracuje, tím víc dat se ukládá ke každému páru a díky tomu robot může přesněji spočítat různé hodnoty pro optimální trading. Pokud jsou data vymazána, jednotliví obchodníci si je musí nasbírat znovu a během této doby nemusí být dostatečně efektivní

### [traders] v traders.conf

Soubor **traders.conf** je vložen do hlavního konfiguračního suboru přes **@include traders.conf**. Oddělení do separátního souboru je provedeno proto, aby při změně nastavení párů nedošlo vlivem překlepu ke změně nastavení jiných částí robota například omylem.

**list** = obsahuje seznam identifikátorů obchodníků. Identifikátory jsou oddělené mezerou

**Poznámka** - identifikátory si volí uživatel tak aby z identifikátoru poznal, o jakého obchodníka jde. Při volbě identifikátoru dodržte následující pravidla

- Pouze malá a velká písmena a číslice a nebo znak podtržítka případně pomlčky
- identifikátor nesmí obsahovat mezeru, lomítka, tečky atd
- protože identifikátor je zároveň použit jako název souboru, omezení je také dáno operačním systémem kde je robot provozován

```
[traders]
list=ltcczk btcczk ethczk xrpczk bchczk dashczk grinusdc
```

### Konfigurace obchodníků


Každý obchodník musí mít v konfiguračním souboru sekci se stejným názvem jako je identifikátor obchodníka

```
[ltcczk]
...
...


[btcczk]
...
...

```
Následuje podrobný popis konfigurace jednoho obchodníka

#### Povinné


**broker** = identifikátor brokera (ze sekce [brokers])


**pair_symbol** = specifikuje symbol obchodního páru. Správný symbol je nutné získat na cílové burze nebo v popisu jejich API. Příklady pro různé burzy

```
Pár:      ETH/BTC
Coinmate: ETH_BTC
Bitfinex: tETHBTC
Poloniex: BTC_ETH
```

#### Volitelné

(dle významnosti)
**title** = (volitelné) definuje nadpis v reportu

**dry_run** = (volitelné)Zapíná (**1**) režím emulace. V tomto režimu robot neposílá pokyny na burze a provádí párování v interním emulátoru. Lze použít na testování nastavení, nebo na sběr nutných dat pro výpočet spreadu. Při vypnutí režimu emulace robot smaže všechny provedené obchody a stáhne skutečný stav z burzy (ale nesmaže nasbíraná data pro výpočty). Výchozí hodnota je **0**

**external_assets** = (volitelné) specifikuje, kolik assetů leží mimo burzu. Robot toto číslo připočítává ke zjištěné balanci a používá ve výpočtu. Uvedené assety přitom nemusí fyzicky existovat, lze číslem zvýšit objem obchodů za cenu zvýšeného rizika, že při dlouhodobém pohybu jedním směrem bez korekce dojde k vyčerpání všech prostředků na burze. Pokud externí assety existují, lze je na burzu doplnit a obchodovat dál. 

Informaci o tom, kdy lze očekávat vyčerpání assetů nebo currency poskytne příkaz **calc_ranges**


**dynmult_raise** = (volitelné) definuje zvýšení dynamického multiplikátoru. Dynamický multiplikátor má stejný význam jako **buy_step_mult**/**sell_step_mult**, pouze se po čas obchodování dynamicky mění. Pokaždé, když je realizován obchod dojde k jeho zvýšení. Když se neobchoduje, časem se snižuje až na 1. Existují dva nezávislé multiplikátory, jeden pro nákup a druhý pro prodej. Tato hodnota definuje o kolik procent se zvedne dynamický multiplikátor při realizaci obchodu. Výchozí hodnota je 200 (Tedy 200 procent). Každý další obchod může multiplikátor zvýšit ještě víc podle jeho aktuální hodnoty 

**dynmult_fall** = (volitelné) definuje snížení dynamického multiplikátoru. Dynamický multiplikátor má stejný význam jako **buy_step_mult**/**sell_step_mult**, pouze se po čas obchodování dynamicky mění. Pokaždé, když je realizován obchod dojde k jeho zvýšení. Když se neobchoduje, časem se snižuje až na 1. Existují dva nezávislé multiplikátory, jeden pro nákup a druhý pro prodej. Tato hodnota definuje o kolik procent klesne dynamicky multiplikátor, když se v daném kole neobchoduje. Výchozí hodnota je 2.5%. V praxi to znamená, že multiplikátor zvýšený o 200% v době obchodu klesne po cca 27 minutách na hodnotu 1.5x a na normální hodnotu se dostane po cca 44 minutách.

**acum_factor_buy** - akumulační faktor při nákupu. Určuje, zda má robot spíš akumulovat assety nebo peníze. Pokud je zadáno 0, pak veškerý profit je připsán do peněz. Pokud je zadáno 1, pak profit je použit na nákup většího množství assetů. To může ve výsledku zvýšit budoucí profity, ale zároveň způsobí, že celkový normalizovaný profit bude závislý na aktuální ceně assetů. Výchozí hodnota je 0.5, tedy 50% normalizovaného profitu jde do assetů 

**acum_factor_sell** - akumulační faktor při prodeji. Určuje, zda má robot spíš akumulovat assety nebo peníze. Pokud je zadáno 0, pak veškerý profit je připsán do peněz. Pokud je zadáno 1, pak je profit ponechán v assetech (je prodáno o trochu méně). To může ve výsledku zvýšit budoucí profity, ale zároveň způsobí, že celkový normalizovaný profit bude závislý na aktuální ceně assetů. Výchozí hodnota je 0.5, tedy 50% normalizovaného profitu jde do assetů 

**internal_balance** - způsobí, že robot nebude sledovat balanci na burze, ale bude ji počítat z načtených obchodů. Tím přestane být výpočet ovlivňován změnami balance na burze, což může být přínosné zejména pro pár BTC/USD, pokud se zároveň obchoduje XXX/BTC (kdy BTC funguje jako currency). Tuto funkci zapněte později, až když nejaké trady byly na páru zaznamenány a uloženy, robot pak použije poslední uložený stav za výchozí. Je-li tato funkce zapnuta pro pár od začátku, pak je výchozí balancí 0 a výpočet funguje správně pouze pokud je zároveň nastavena položka `external_assets`

**sliding_pos.change**, **sliding_pos.assets** - dvojice nastavení přepne robota do režimu 
kdy se snaží postupně v čase nakupovat a prodávat assety tak, aby docílil zadaného počtu 
assetů drženého na burze. **sliding_pos.change** definuje změnu ceny neutrální pozice (cena
která odpovídá množství držených assets) oproti ceně posledního obchodu v procentech z rozdílu. Například pokud neutrální pozice vychází na cenu 1000 USD a poslední obchod je za
2000 USD, a toto nastavení je 5, pak se neutrální pozice přepočítá na cenu 1005. Dalšími
obchody lze eventuálně cenu neutrální pozice "dotáhnout" k aktuální ceně. Doporučená hodnota je v rozsahu `0-10`. Přepočet probíhá vždy při provedení obchodu. Proměnná **sliding_pos.assets** definuje, kolik assetů na burze je považováno za neutrální pozici. Výchozí hodnota je `0`. K této hodnotě je během výpočtu přičtena ještě hodnota `external_assets`, proto tato hodnota představuje množství assetů na burze bez započtení externích assetů.  Výchozí hodnota je přizpůsobena pro pákové burzy typu Deribit, kde neutrální pozící je zpravidla myšlena
 nulová pozice. 
 
 **POZNÁMKA:** - Pokud je proměnná **sliding_pos.change** nenulová, robot počítá některé statistiky jinak. Například se nezobrazují statistiky o množství akumulovaných assetů a všechny zisky ať již z akumulace nebo z normalizovaného profitu jsou sloučeny. V tomto režimu také přesnější informace poskytne statistika `P/L from position`.  

**detect_manual_trades** - `(experimentální)` Robot se pokouší označit načtené obchody které neprovedl on jako manuální obchody (provedené uživatelem ručně na burze). Tyto obchody se pak nezapočítávají do statistik a přehledů. Pokud je zároveň zapnuta `internal_balance`, pak se tyto trady nezapočítávají ani do balance. Funkce je v režimu experimentální, protože zaznamenává nemalé procento false positive (robot někdy nepozná svůj obchod)

**start_time** = (volitelné) specifikuje počáteční čas obchodování od začátku epochy v milisekundách (linux timestamp * 1000). Výchozí hodnota je 0. Nastavení na jinou hodnotu způsobí, že robot bude ignorovat starší obchody než je zadaný čas a nebude je zobrazovat v reportech ani z nich počítat statistiky a dalších informace

**spread_calc_hours** = (volitelné) Specifikuje, jak dlouhý úsek se bude používat na výpočet spreadu. Výchozí hodnota je **120**, tedy pět dní. Pokud nastavíte větší číslo, je třeba počítat s tím, že robot musí dodatečná data časem nasbírat a bude mu to trvat přesně tolik hodin, o kolik větší číslo napíšete.

**spread_calc_min_trades** = (volitelné) Minimální počet obchodů na den při backtestu - Při výpočtu spreadu se hodnotí, kolik denně obchodů robot provedl. Pokud provedl méně, než je zadaná hodnota, příslušný test se vyřadí (obdrží nízké skoré) a použije se jiný výsledek. Výchozí hodnota je **8**

**spread_calc_max_trades** = (volitelné) Maximální počet obchodů na den při backtestu - Při výpočtu spreadu se hodnotí, kolik denně obchodů robot provedl. Pokud provedl více, než je zadaná hodnota, příslušný test se vyřadí (obdrží nízké skoré) a použije se jiný výsledek. Výchozí hodnota je **24**




#### starší a obsolete

**buy_mult** = _není dále podporován_
**sell_mult** = _není dále podporován_

**buy_step_mult** = (volitelné) Nákupní multiplikátor kroku. Upravuje krok spreadu tím, že ho vynásobí zadaným číslem. Nákupní příkaz tak bude umístěn dále (>1) nebo blíže (<1) středu. Adekvátně bude upraveno i množství dle výpočtu: Výchozí hodnota je **1**

**sell_step_mult** = (volitelné) Prodejní multiplikátor kroku. Upravuje krok spreadu tím, že ho vynásobí zadaným číslem. Prodejní příkaz tak bude umístěn dále (>1) nebo blíže (<1) středu. Adekvátně bude upraveno i množství dle výpočtu: Výchozí hodnota je **1**

