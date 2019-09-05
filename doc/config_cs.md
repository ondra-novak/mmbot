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

**level** = level: **debug**,**info**,**progress**,**note**,**warning**,**error**,**fatal**, Každá úroveň zahrnuje výpisy z vyšší úrovně

 - **debug** - všechny výpisy včetně ladících
 - **info** - vše kromě ladících výpisů
 - **progress** - představuje výpisy informující o průběhu operace
 - **note** - výpisy obsahující zásadní informaci
 - **warning** - výpisy obsahující varování, které za určitých situacích mohou být považovány za chybu
 - **error** - výpisy představující chyby, které znamenají, že se nějaká operace neprovedla, ale jinak nemají vliv na běh robota
 - **fatal** - zásadní chyba, která představuje ukončení činnosti robota 

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

**spread_calc_interval** = (volitelné) Interval v minutách jak často se přepočítává spread. Výchozí hodnota je 10


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

**neutral_pos** = (volitelné) specifikuje neutrální pozici. Lze ji zadat jako čislo, pak 
určuje, kolik fyzicky držených assetů na burze má robot považovat za neutrální pozici (ted bez zápočtu `external_assets`). Alternativně lze zadat před číslo klíčové slovo
  * **assets** /číslo/ - stejné jako samotné číslo
  * **currency** /číslo/ - neutrální pozice je odvozena od zůstatku currency na účtu
  * **center** /číslo/ - hodnota určuje číslo jako poměr mezi assets a currency. Hodnota 1 představuje 50:50. Hodnota 2 pak 33:66, atd.

Nastavení neutrální pozice umožňuje řídit obchodování k maximalizaci zisku z pozice. Bez určení neutrální pozice se maximalizuje normalizovaný profit. Samotná specifikace neutrální pozice způsobí, že graf normalizovaného zisku je přepočítán na potenciální zisk z držené pozice. Některé další volby vyžadují nastavenou neutrální pozici, jinak nefungují. Nastavení **neutral_pos=0** je vhodné pro pákové burzy, kde pozice se přímo mapuje na pozici na burze.
  
**max_pos** = (volitelné) zakazuje robotovi vzít pozici větší, než zadané číslo (včetně pozice záporné, tedy shortu). 

**dynmult_raise** = (volitelné) definuje zvýšení dynamického multiplikátoru. Dynamický multiplikátor má stejný význam jako **buy_step_mult**/**sell_step_mult**, pouze se po čas obchodování dynamicky mění. Pokaždé, když je realizován obchod dojde k jeho zvýšení. Když se neobchoduje, časem se snižuje až na 1. Existují dva nezávislé multiplikátory, jeden pro nákup a druhý pro prodej. Tato hodnota definuje o kolik procent se zvedne dynamický multiplikátor při realizaci obchodu. Výchozí hodnota je 200 (Tedy 200 procent). Každý další obchod může multiplikátor zvýšit ještě víc podle jeho aktuální hodnoty 

**dynmult_fall** = (volitelné) definuje snížení dynamického multiplikátoru. Dynamický multiplikátor má stejný význam jako **buy_step_mult**/**sell_step_mult**, pouze se po čas obchodování dynamicky mění. Pokaždé, když je realizován obchod dojde k jeho zvýšení. Když se neobchoduje, časem se snižuje až na 1. Existují dva nezávislé multiplikátory, jeden pro nákup a druhý pro prodej. Tato hodnota definuje o kolik procent klesne dynamicky multiplikátor, když se v daném kole neobchoduje. Výchozí hodnota je 2.5%. V praxi to znamená, že multiplikátor zvýšený o 200% v době obchodu klesne po cca 27 minutách na hodnotu 1.5x a na normální hodnotu se dostane po cca 44 minutách.

**dynmult_mode** = nabývá hodnot `independent`, `together`, `alternate`, `half_alternate`, výchozí hodnota je  `independent`. Řídí chování multiplikátorů dynmult.
 - `independent` - dynamické multiplikátory jsou nezávisle na sobě zvyšovány a snižovány na základě toho, jaký pokyn se exekuoval
 - `together` - dynamické multiplikátory se zvyšují společně pokud byl exekuován libovolný pokyn a snižují se, v období klidu. Vhodné pro zachycení největší amplitudy rozkmitu ceny na vysoce volatilním trhu
 - `alternate` - exekuce pokynu jedním směrem zvýší příslušný multiplikátor, avšak multiplikátor pro opačný směr nastaví zpět na 1. Vhodné pro zachycení rychlých pollbacků při pumpách a dumpách.
 - `half_alternate`  - exekuce pokynu jedním směrem zvýší příslušný multiplikátor, avšak multiplikátor pro opačný směr se sníží na polovic. Vhodné pro zachycení rychlých pollbacků při pumpách a rychlých trendech avšak není tak dravý jako `alternate`
 

**acum_factor_buy** - akumulační faktor při nákupu. Určuje, zda má robot spíš akumulovat assety nebo peníze. Pokud je zadáno 0, pak veškerý profit je připsán do peněz. Pokud je zadáno 1, pak profit je použit na nákup většího množství assetů. To může ve výsledku zvýšit budoucí profity, ale zároveň způsobí, že celkový normalizovaný profit bude závislý na aktuální ceně assetů. Výchozí hodnota je 0.5, tedy 50% normalizovaného profitu jde do assetů 

**acum_factor_sell** - akumulační faktor při prodeji. Určuje, zda má robot spíš akumulovat assety nebo peníze. Pokud je zadáno 0, pak veškerý profit je připsán do peněz. Pokud je zadáno 1, pak je profit ponechán v assetech (je prodáno o trochu méně). To může ve výsledku zvýšit budoucí profity, ale zároveň způsobí, že celkový normalizovaný profit bude závislý na aktuální ceně assetů. Výchozí hodnota je 0.5, tedy 50% normalizovaného profitu jde do assetů 

**internal_balance** - způsobí, že robot nebude sledovat balanci na burze, ale bude ji počítat z načtených obchodů. Tím přestane být výpočet ovlivňován změnami balance na burze, což může být přínosné zejména pro pár BTC/USD, pokud se zároveň obchoduje XXX/BTC (kdy BTC funguje jako currency). Tuto funkci zapněte později, až když nejaké trady byly na páru zaznamenány a uloženy, robot pak použije poslední uložený stav za výchozí. Je-li tato funkce zapnuta pro pár od začátku, pak je výchozí balancí 0 a výpočet funguje správně pouze pokud je zároveň nastavena položka `external_assets`

**sliding_pos.hours** - Specifikuje s jakou rychlostí se posouvá cena neutrální pozice. Tato volba musí být v kombinaci s `neutral_pos`. Hodnota je hodinách. Je v celku obtížné ji dobře nastavit. Příliš malé hodnoty mohou ve výsledku generovat ztrátu. Příliš velké hodnoty zase nezajistí posun tak rychle, aby se obchodování nedostalo mimo rozsah. Výpočet nejvíc sedí na EMA v hodinovém grafu. Optimální hodnota cca 240 (deset dní)

**sliding_pos.weaken** - Specifikuje zeslabování obchodní síly se zvyšování pozice. Hodnota definuje maximální pozici (při téhle pozici bude obchodování víceméně minimální) Hodnota se zadává jako procento z `external_assets`+`neutral_pos`. Pomocí backtestů byla zjištěna optimální hodnota 10-11. Vyšší hodnoty dovolí vyšší pozici. Volbu je dobré kombinovat se sliding_pos.hours. Pokud se obchodování přesune rychle na jinou cenu, vlivem snižování obchodní síly nebude nabraná taková ztráta. Časem dojde ke srovnání neutrální ceny a aktuální ceny. Nicméně zeslabení obchodní síly snižuje potenciální zisk 

**accept_loss** - pokud je nenulová, definuje počet hodin od posledního trade, po kterou musí mít robot zablokované vydání pokynu v jednom směru, aby se aktivovala funkce `accept_loss`. Zároveň musí být splněno, že dynamické multiplikátory jsou rovné 1. Pokud je tedy toto splněno, robot posune equlibrium na cenu zablokovaného pokynu a tím akceptuje ztrátu vzniklou tím, že se pokyn nebude realizovat. Pokyn může být zablokován v důsledku `sliding_pos.max_pos`, ale v důsledku toho, že nejsou prostředky na burze. Je třeba si ovšem dát pozor, aby pokyn nebyl zablokován například v důsledku dlouhotrvající maintenance na burze (robot momentálně nerozpozná důvod zablokování). Proto je dobré nastavit hodnotu na řádově několik hodin, například 12 (nejdelší maintenance míval Bitfinex = 7 hodin)

**enabled** - ve výchozím stavu je nastaveno na `true` a umožňuje běh robota na zadaném páru. Pokud je nastaveno na `false`, robot zruší všechny své pokyny a přestane obchodovat. Pokyny jsou zablokované (ale nereaguje ani funkce `accept_loss`). V tomto stavu také neprobíhá výpočet spreadu. Nicméně probíhá sběr dat pro výpočet spreadu.   

**detect_manual_trades** - `(experimentální)` Robot se pokouší označit načtené obchody které neprovedl on jako manuální obchody (provedené uživatelem ručně na burze). Tyto obchody se pak nezapočítávají do statistik a přehledů. Pokud je zároveň zapnuta `internal_balance`, pak se tyto trady nezapočítávají ani do balance. Funkce je v režimu experimentální, protože zaznamenává nemalé procento false positive (robot někdy nepozná svůj obchod)

**start_time** = (volitelné) specifikuje počáteční čas obchodování od začátku epochy v milisekundách (linux timestamp * 1000). Výchozí hodnota je 0. Nastavení na jinou hodnotu způsobí, že robot bude ignorovat starší obchody než je zadaný čas a nebude je zobrazovat v reportech ani z nich počítat statistiky a dalších informace

**spread_calc_hours** = (volitelné) Specifikuje, jak dlouhý úsek se bude používat na výpočet spreadu. Výchozí hodnota je **120**, tedy pět dní. Pokud nastavíte větší číslo, je třeba počítat s tím, že robot musí dodatečná data časem nasbírat a bude mu to trvat přesně tolik hodin, o kolik větší číslo napíšete.

**spread_calc_min_trades** = (volitelné) Minimální počet obchodů na den při backtestu - Při výpočtu spreadu se hodnotí, kolik denně obchodů robot provedl. Pokud provedl méně, než je zadaná hodnota, příslušný test se vyřadí (obdrží nízké skoré) a použije se jiný výsledek. Výchozí hodnota je **8**

**spread_calc_max_trades** = (volitelné) Maximální počet obchodů na den při backtestu - Při výpočtu spreadu se hodnotí, kolik denně obchodů robot provedl. Pokud provedl více, než je zadaná hodnota, příslušný test se vyřadí (obdrží nízké skoré) a použije se jiný výsledek. Výchozí hodnota je **24**

**force_spread** = (volitelné) Vynutí si konkrétní spread. Hodnota je zadána jako logaritmus o základu e. Hodnota 0.01 odpovídá 1%. Výpočet log(x/100+1), kde x je spread v procentech. Pokud je vynucen spread, pak se výpočet spreadu nespouští

**min_size** = (volitelné) specifikuje minimální velikost pokynu. Vhodné, pokud burza tvrdošině blokuje příliš malé pokyny.


#### starší a obsolete

**buy_mult** = násobí velikost nákupního pokynu číslem. Výsledný výpočet nemění spočtenou rovnováhu. Pokud je číslo menší než 1, pak při exekuci tohoto příkazu dojde ke zvýšení velikosti pokynu na nižšá ceně. Pokud je číslo větší než 1, pak naopak příští pokyn může být menší na vyšší ceně. Pri velkých hodnotách může dojít k totálnímu chaosu ve výpočtu, proto se nedoporučuje volbu používat, případně jen hodně blízko k jedničce 

**sell_mult** = násobí velikost prodejního pokynu číslem. Výsledný výpočet nemění spočtenou rovnováhu. Pokud je číslo menší než 1, pak při exekuci tohoto příkazu dojde ke zvýšení velikosti pokynu na vyšší ceně. Pokud je číslo větší než 1, pak naopak příští pokyn může být menší na nižší ceně. Pri velkých hodnotách může dojít k totálnímu chaosu ve výpočtu, proto se nedoporučuje volbu používat, případně jen hodně blízko k jedničce

**buy_step_mult** = (volitelné) Nákupní multiplikátor kroku. Upravuje krok spreadu tím, že ho vynásobí zadaným číslem. Nákupní příkaz tak bude umístěn dále (>1) nebo blíže (<1) středu. Adekvátně bude upraveno i množství dle výpočtu: Výchozí hodnota je **1**

**sell_step_mult** = (volitelné) Prodejní multiplikátor kroku. Upravuje krok spreadu tím, že ho vynásobí zadaným číslem. Prodejní příkaz tak bude umístěn dále (>1) nebo blíže (<1) středu. Adekvátně bude upraveno i množství dle výpočtu: Výchozí hodnota je **1**


#### volby pro backtest

Pro backtest lze použít všechny volby z konfigurace obchodníka a k tomu tyto navíc. 

Volby pro backtest se většinou zadávájí na příkazovém řádku, avšak mají stejný formát jako volby configu. Mohou být uvedeny i v nastavení páru, ale mimo backtest se ignorují

**spread_calc_interval** = specifikuje interval jak často se přepočítává spread během backtestu. Hodnota je v minutách (bere se čas grafu). Vychozí hodnota je 0 a tím se přepočet spreadu vypíná. Jako výchozí spread se použije spread na zdrojovém páru v době spuštění backtestu. Spread je možné změnit pomocí `force_spread` a nebo zapnutím této volby. Ale pozor. výpočet spreadu je velice pomalá operace, proto se doporučuje nastavovat interval kolem 1000 a víc, nebo si nechat spread první průchodem spočítat a pro další průchody použít `force_spread`


##### Generování grafu

* Není-li určeno jinak, tak se backtest provádí nad daty určenými pro výpočet spreadu. Tam je zpravidla k dispozici cca 5 dní minutových dat (přesněji `spread_calc_hours` ovšem nad původním párem) 

**mirror** = prochází vygenerovaný graf tam a zpět. Díky tomu se eliminuje trend. Vychozí je zapnuto

**repeat** = vytvoří graf tím že zopakuje již vytvoření graf víckrát. Pokud není jinak uvedeno, zopakuje se graf vytvořený  zdat určených pro výpočet spreadu

**trend** = přidává trend. Pozor hodnota určuje posun v procentech za 1 minutu. Bývá to hodně malé číslo: např 0.0001. Kládná čísla představují rostoucí trend, záporná klesající. Je-li aktivní `mirror`, pak je výsledkem stříška nebo V. U inverzních futures je význam znaménka otočen

**random** = generuje n8hodný graf. Parametr určuje počet minut.

**seed** = (povinné pokud s `random`) libovolné číslo fungující jako seed. Změnou čísla se vygeneruje jiný graf

**stddev** = několik hodnot oddělených lomítkem:  Například 0.04/0.02/0.01 - počet hodnot není omezen, ale většinou bývají 1-4. První hodnota uvádí směrodatnou odchylku náhodného trendu, který se mění každou minutu. Druhá hodnota určuje tento trend pro 2 minuty, třetí hodnota pro 4 minuty atd. Výsledná změna ceny je dána jako kombinace všech náhodných změn. Jiný příklad: ////0.01 - definuje že pouze každých 16 minut se změní směr grafu.

**merge** = on/off sloučí data určená pro výpočet spreadu s náhodným grafem. Výsledkem je graf, který může mít vlostnosti které se blíží skutečnému trhu. Výchozí je off

**dump_chart** = cesta. Specifikuje cestu na soubor. kam se uloží vygenerovaný graf pro kontrolu. Cesta musí být absolutní. Pokud použijete příponu `.csv`, lze soubor otevřít v Excelu/LibreOffice

