# Nastavení a provozování robota

## Jak robot funguje?

Robot vydělává na rozdílech mezi nákupní a prodejní cenou obchodovaného assetu (třeba bitcoin). Vždy tedy nakupuje za nižší cenu, než je cena posledního obchodu, a prodává za cenu vyšší. Rozdíly v ceně mohou být malé a tedy i malé zisky, robot však provádí mnoho obchodů a tak se malé zisky mohou nasčítat ve větší zisky

**Nejedná se o HFT** Robot neobchoduje na MARKET. Naopak pouze dodává likviditu na burzu pomocí vhodného umístění LIMITních příkazů do orderbooku. Výkon robota a počet obchodů tak závisí na ostatních účastnících trhu, kteří tam nakupují a prodávají. Vhodným protějškem jsou také arbitrážní roboti, kteří jsou schopni zobchodovat limitní příkazy při reakci na pohyb na jiné burze.

## Na jakém trhu robot funguje nejlépe?

Ideálním trhem je trh, který jde dlouhodobě do boku avšak objevují se na něm vlnky a výkyvy. Čí vyšší jsou výkyvy nahoru a dolu, tím většího zisku může robot dosáhnout. Naopak trendující trhy, u kterých je trend strmější než denní volatilita v součtu mohou rapidně srážet výkon a přinášet ztráty - avšak krátkodobé trendy, které končí změnou trendu mohou naopak být vítané, protože to co robot zobchoduje jedním směrem pak pokryje se ziskem druhým směrem

## Jaké mám očekávat zisky?

Robot není navržen tak, aby generoval vysoké zisky, protože primárně není úrčen pro obchod na páce. Avšak velmi záleží na nastavení. V základním nastavení lze očekávat slabé zisky kolem 10% ročně. V tomto nastavení však robot dokáže obchodovat na libovolně rozkolísaném trhu. Je však vhodné nastavit robota na větší objemy avšak dobře si spočítat, zda rozkolísání trhu nemůže robota poslat na jednu z kritických stran, kde přestává fungovat, tedy kdy robotovi dojdou buď assety nebo peníze. Pak je třeba situaci řešit buď doplněním assetů nebo peněz (nebo obchodováním na páku)

Více o nastavení robota dále

## Jaké je riziko?

Dokud robot obchoduje pouze s prostředky bez páky, je riziko malé. Sám algoritmus dlouhodobě negeneruje ztrátu. Ztráta může přijít pouze tím, že trh, na kterém robot obchoduje jde dlouhodobě dolu a přestává generovat vlnky, nebo na něm klesají objemy. Takový trh signalizuje, že o něj obchodníci ztrácí zájem. Robot se tak může dostat do situace, kdy zůstane back holderem, tedy bude držet assety, které nemají žádnou cenu. 

Neznamená to ale, že každý trh jdoucí dolu může být ztrátový. Pokud jde trh dolu pomaleji než zisky z vlnek, pak i takový trh může přinášet v celku zisk.

## Co potřebuju k provozu robota?

- Trvale běžící stroj, server nebo VPS
- Operační systém linux, ideálně Ubuntu 18, případně vrstevníci
- Připojení na internet
- účet na podporované burze
- vygenerované API klíče
- GCC a GPP 7.0
- Git
- různé další knihovny: libssl, libcurlpp, libcurl, atd

Více informací na install.md

## Soubory robota

```
 +-- bin
 |    + -- mmbot
 |    + -- brokers
 |            + -- coinmate
 |            + -- poloniex
 |            + -- <další brokeři>
 + -- conf
 |     + -- brokers
 |     |      + -- coinmate.conf
 |     |      + -- poloniex.conf
 |     + -- mmbot.conf
 |     + -- brokers.conf
 |     + -- traders.conf
 + -- log
 + -- data
 + -- www
       + -- index.html
       + -- style.css
       + -- code.js
       + -- manifest.json
```


## Konfigurace 

### Soubory ke konfiguraci

Podrobnosti v nastavení hledejte v config_cs.md

- **mmbot.conf** - obsahuje základni nastavení aplikace, které z pravidla není třeba měnit. 
- **brokers.conf** - obsahuje nastavení broketů. Nastavení není třeba měnit, není třeba zavést dalšíhi brokera. Je vhodné znát jména broketů, které jsou v tomto configu deklarovány
- **traders.conf** - do tohoto konfiguračního souboru se vkládají definice jednotlivých obchodovaných párů a jejich nastavení
- **brokes/*.conf** - pro každého brokera je zde config, ve kterém je třeba doplnit API klíče na burzy. Stačí upravit jen ty, které se používají

### Příprava na zavedení obchodovaného páru

1. Pro každý pár je vhodné vyhradit 50% vkladu a za tento vklad nakoupit obchodované assety. Naptíklad, pokud obchoduju BTC/USD se vkladem 30000 USD při ceně 1 BTC za 10000USD, první krokem je nákup 1.5 BTC za 15000 USD. Tímto zůstane na účtu 15000 USD v BTC a 15000 USD v penězích.
2. Zjistěte označení assetu (třeba `BTC`), currency (třeba EUR) a páru (třeba `BTCEUR`). Tyto informace často nejsou k dispozici na UI burze, ale najdete je často v popisu API. Například na bitfinexu se pár označuje s písmenem t (`tBTCEUR`), na coinmate byt to bylo `BTC_EUR`


### Nastavení nového páru

1. v configu **traders.conf** smažte ukázkový obsah a na začátek napište

```
[traders]
list=<moje označení>
```

Namísto <moje označení> si vložte vaši značku měnového páru. Například `btceur`

```
[traders]
list=btceur
```


2. doplnte sekci s vašim označení

```
[traders]
list=btceur

[btceur]
broker=
pair_symbol=
```

* **broker** - napište jméno brokera. Seznam brokerů najdete v **brookers**
* **pair_symbol** - vložte značku obchodovaného páru (např. `BTC_EUR`)

**za poslední položkou musí být prázný řádek**

## API klíče

1. Získetjte API klíče na burze
2. Otevřte konfigurační soubor brokera
3. Vyplňte prázdné položky hodnotami získané z burzy

## Více párů

Pokud chcete obchodovat více párů, pak postupujte stejně jako u jednoho páru. Do položky `list` lze vložit více značek oddělené mezerou.

Dobře si propočítejte finanční rezervu


## Záběh robota

Před spuštěním robota s novým párem je dobré aby robot po nějakou dobu pouze sbíral data. Do konfiguračního souboru k danému páru lze napsat následující řádek

```
dry_run=1
```

tento řádek způsobí, že robot nebude posílat pokyny na burzu. Je vhodne pro nově přidaný pár po nějakou dobu (minimálně hodinu) nechat robota puštěného v tomto režimu. Robot během této doby sbírá data z trhu a ukládá je do souboru v adresáři `data`. Tyto data posléze používá k výpočtu nutných hodnot

**Poznámka**: V tomto režimu pokyny končí v emulátoru. Robot navenek vykazuje činnost jako by reálně obchodoval. Zapsané obchody se na burze fyzicky nerealizují. Pokud je robot přepnut do ostrého režimu, všechny testovací obchody jsou vymazány a je provedena synchornizace obchodů s burzou. 


**Poznámka:** tento krok lze vynechat, avšak je potřeba počítat s tím, že během prvních pár hodin nemusí robot obchodovat optimálně. Neměl by však generovat ztrátu. Může se stát, že robot nebude obchodovat vůbec (například bude držet vysoký spread), nebo naopak bude generovat velmi mnoho titěrných obchodů. I tak by každý tento obchod měl pokrývat poplatky na burze, čili samotné obchody by neměly generovat ztrátu

Stejná situace nastane, pokud promažete adresář `data`

## Spuštění robota

### Spuštění robota na zkoušku

Robota lze spustit tak, aby vypisoval co dělá, ale neprováděl žádnou obchodní činnost

V adresáři kde je robot nainstalován:

```
$ bin/mmbot -vdt run
```

- `-vdt` je kombinace tří přepínačů
    - `v` způsobí, že činnost se bude vypisovat do konzole
    - `d` způsobí, že robot bude vypisovat veškerou činnost (debug)
    - `t` způsobí, že žádný pokyn nedorazí na burzu (globalní dry_run)
    
Činnost robota lze ukončit stiskem Ctrl+C


### Ostré spuštění robota jako služba

```
$ bin/mmbot start
```

V režimu služby se nic na konzoli nezobrazí. Pokud chcete zkontrolovat, že robot běží, napište do konzoli

```
$ bin/mmbot status
```

### Ukončení robota spuštěněho jako služba

```
$ bin/mmbot stop
```

**Poznámka:** Zastavení robota nezpůsobí smazání umístěných pokynů na burzách. Pokud je robot znovy spuštěn, pokračuje v činnosti tam kde přestal.

### Restart robota

```
$ bin/mmbot restart
```


Restart robota je třeba provést **při každé změně konfigurace**. Bez restartu se změny konfigurace neuplatní.

## Správné nastavení robota

[Podrobné nastavení](config_cs.md)

Robot provádí nákup a prodej v relativně malém pásu kolem aktuální ceny. Vydělává tak tím, že poskytuje likviditu obchodníkům, kteří potřebují rychle nakoupit nebo prodat. Dlouhodobě se totiž ukazuje, že cena se často točí kolem nějaké hodnoty, dokud nenajde rovnováhu, Jakmile je rovnáha nalezena, může se záhy cena posunou jinam a zase hledá rovnováhu. Cílem obchodování malých rozdílu přitom je, aby ztráta pramenící z velké změny ceny byla vyrovnána obchodováním při pohybu do boku.

Nastavení se liší podle sledovaného cíle. Robot může sloužit jako doplněk k HODLu, kdy není dopředu známo, odkud kam se cena může pohybovat, a nebo může být agresivnější a sázet na to, že cena se nijak zásadně měnit nebude a pokud ano, tak málo, a zisky se nadělají při pohybu do boku.

V základním nastavení robot vždy obchoduje procentní rozdíl spreadu vůči aktuálnímu zůstatku (s připočtením `external_assets`)

Velikost pokynu se počítá na základě ceny posledního obchodu `Tp` a cenu pokynu `Op` a aktuální balance assetů `A`

```
          ________
S = A × (√ Tp / Op  - 1)

 
```

Pokud se Bitcoin obchoduje na ceně 200000 Kč a spočítaný spread je 1700 Kč, pak prodejní příkaz bude na ceně `201700 Kč` o velikosti

```
Ss = -0,004223 × A
```

Nákupní příkaz bude na ceně `198300 Kč`o velikosti

```
Sb = 0,0042773 × A
```

Pokud dosadíte za **A = 1 BTC**, pak nakupovat budeme `0.0043 BTC`, což představuje objem `852,69Kč`, prodávat se bude `0.0042 BTC` o objemu `847,14Kč`. Cenový rozdíl zde dělá `3,55Kč`. Pokud by cena bitcoinu poklesla o polovinu (na 100000kč), musel bych předtím udělat zhruba **28tis obchodů** abych ztrátu dohnal.

V tomto nastavení se nezdá, že by robot byl schopen vytvořit výrazný profit, který by ochránil investici před pohyby. Na druhou stranu, toto nastavení lze provozovat do nekonečna, protože vzoreček nikdy nevyjde pro prodej větší než A a díky polovině dokonce nikdy nebude větší než polovina A. Stejně tak lze dokázat, že nákupy nebudou dohromady nikdy stát víc, než objem odpovadjící počátečnímu A při počáteční ceně...

Takže postačí mít na účtu například 0.1 BTC a 20000 Kč při ceně 200000Kč za BTC a obchodovat je možné do nekonečna.

### Zvětšení objemu obchodů

Robot může obchodovat větší částky. Toho lze docílít tak, že robota přesvědčíme, že bokem mimo burzu disponujeme dalšímy penězi a assety. K tomu slouží v konfiguračním souboru položka `external_assets`. Do této položky napíšeme číslo o které navýšíme ono `A` ve vzorečku. Tím lze dosáhnout většího objemu. Má to však nevýhodu.

Pokud navýšíme počet assety držené mimo burzu, robot nyní může všechny assety vyprodat, nebo mu mohou dojít peníze na nákup. Protože vzoreček počítá s penězi mimo burzu. 

Přesto lze obchodovat i bez toho aby peníze fyzicky existovaly, pokud se cena bude držet v nějakém bezpečném pásmu

### Odhad bezpečného pásma

Robot disponuje funkcí odhadu bezpečného cenového pásma. 

Pokud máme robota delší čas v provozu například v režimu **na zkoušku**, lze z příkazové řádky napsat následující příkaz

```
$ bin/mmbot calc_range
```

Robot vypíše pro každý měnový pár informaci o maximální a minimální ceně na základě stavu balance na burze

```
Trader LTC/CZK:
	Assets:			28.7953 LTC
	Assets value:		73668.4 CZK
	Available assets:	8.79527 LTC
	Available money:	77392.6 CZK
	Min price:		0.030036 CZK
	Max price:		4829.33 CZK
```

Příklad ukazuje:
- **Assets** - robot vidí následující množství coinů (LTC)
- **Assets value** - spočítáná hodnota assetů vůči aktuální ceně
- **Available assets** - skutečné množství assetů k dispozici na burze (tady je vidět, že `external_assets` je nastaven na 20)
- **Available money** - množství peněz na burze k nákupu
- **Min price** - minimální možná cena. Je vidět, že na burze je dostatek prostředku na pád Litecoinu až do oblasti halířů
- **Max price** - maximální možná cena. Je vidět, že při ceně 4829.33 Kč za 1 LTC robotovi dojdou LTC a nebude mít co prodávat

 Je třeba počítat s tím, že se jedna o odhad a to ještě za předpokladu, že by cena šla pouze jedním směrem bez korekcí a tak pomalu, aby třeba na situaci nezareagoval dynamický multiplikátor, který toto pásmo může ještě rozšířit díky efektivnějším nákupům. Určitou představu o možnostech spekulace výpočet je schopen poskytnout.
 
### Nastavení optimálního parametru external_assets

Doporučuje se zvyšovat objemy pouze pomocí tohoto nastavení. Ideální postup je nastavit nějaké číslo a nechat si to spočítat. Nastavit další číslo a opět si to nechat spočítat a takto postupovat až je nastavení vyhovující.

Volání calc_range se doporučuje pouštět až po nějaké době, kdy robot běží na zkoušku a sbírá data. Nejdříve tak po hodině ale čím déle tím lépe. Jde o to, aby robot měl dost údajů k výpočtu spreadu.

Opakované spouštění funkce **calc_range** může vracet různě odlišné výsledky v závislosti na aktuální spreadu. Je vždy dobré porovnat výsledky v různých časech a případně hodnotu doupravit

### Další možnosti nastavení objemu

**buy_mult**, **sell_mult** - přímo násobí vypočtenou hodnou multiplikátorem. 

**buy_step_mult**, **buy_step_mult** - násobí spočtený spread. Čím širší spread, tím větší objem (ale tím nižší šance na zásah)

**dynmult_raise**, **dynmult_fall** - ve výchozím stavu je tato funkce zapnutá. Umožňuje reagovat na rychlý pohyb ceny (pumpu nebo dumpu) tím, že zvyšuje spread při každém zásahu o nastavenou hodnotu **dynmult_raise** a případně snižuje **dynmult_fall** v době klidu. Vyšší spread znamená vyšší objem a i vyšší zisk z jedné otočky. Pokud cena prudce akceleruje, robot umísťuje pokyny dál od ceny a tím zvyšuje spread a tedy objem.

# Výsledky a monitoring

## Kde sledovat výsledky?

Grafický přehled je k dispozici prostřednictvím webové stránky, kterou je možné prohlížet pomocí prohlížeče, například `chrome`. Stránku je nutné zpřístupnit pomocí webserveru, například pomocí `Nginx` nebo `Apache`. Nouzově pro osobní použití lze aktivovat robotův malý ad-hoc http server pomocí volby [http_bind](config_cs.md#report). Tato možnost se ale nedoporučuje pro prohlížení výsledků přes internet nebo jakoukoliv nezabezpečenou síti.

Webový server stačí nasměrovat do složky `www`, kde se nachází celá prezentace a kam robot ukládá soubory s reportem.

## Sekce a grafy

### Summary

Představuje přehled všech obchodovaných párů a základních měn. Každý řádek obsahuje tyto údaje

```
NÁZEV PÁRU <nákupní příkaz> <poslední cena> <prodejní příkaz> pos <aktuální pozice>

24h: <výsledky za 24 hodin>
```

**Nákupní příkaz** se zobrazuje zelenou barvou a obsahuje nákupní cenu a pod tím menším písmem množství. **Prodejní příkaz** je červenou barvou a je zde uvedena prodejní cena a množství. Poslední cena (modře) by se měla pohybovat mezi těmito cenami. Pokud se cena posune přes prodejní nebo nákupní příkaz, dojde k obchodu.

Seznam posledních obchodů se zobrazuje pod přehledem. Seznam se aktualizuje automaticky každou minutu

#### Výsledky za 24h

- **Relativní změna ceny**
- **Počet obchodů** (t)
- **Celkový objem** (vol)
- **Zmena pozice** (pos) - kladná čísla nákupy, záporná prodeje
- **Průměrná nákupka** (avg) 
- **Zisk nebo ztráta** (p/l)
- **Přírustek normovaného zisku** (norm) viz níže
  - 

### Grafy

Grafy lze zobrazovat buď jeden graf za každý pár, nebo pro určitý pár všechny grafy

- **P/L from positions** - Zobrazuje zisk nebo ztrátu z držením pozice. Tento ukazatel je vhodný, pokud robot záporné pozice opravdu shortuje (nedoporučuje se), protože při záporné pozici započítává zisk, když cena klesá. Pokud jde o exchange s rozdělením 50:50, tak pří záporné pozici a růstu ceny přesto účet jako celek získává profit, ale o onu uváděnou ztrátu menší, než by získával kdyby neobchodoval. Adekvátně při poklesu cen může kladná pozice mírnit celkovou ztrátu a tím se zobrazovat jak zisk   

- **Normalized profit** - Každý obchod je exekuován s tím, že vždy vzniká malý profit při použití jiné perspektivy pohledu na celého robota. Vic informací dále. Graf lépe vystihuje profit robota při dlouhodobějším obchodování

- **Trades** - grafovaný průběh obchodů a jejich objemů. Červené tečky jsou prodeje, zelené nákupy

- **Position** - graf vývoje pozice

- **Price** - záznam vývoje ceny v místech, kde se obchodovalo. Lze na tom vidět, kde robot nakupoval a kde prodával (není vidět množství, to je vidět na **Trades**

- **Total P/L** a **Total Normalized** - sloučený vývoj zisků a ztrát přes všechn páry vztažené na jejich základní měnu. Pokud je víc základních měn, je zde více grafů, pro každou měnu.


## Normalizovaný zisk a normalizované akumulované assety

K pochopení normalizovaného zisku je třeba se vrátit k výše uvedenému vzorci. 

```
           _________
S = A × ( √ Tp / Op  - 1)
```

Tento vzorce lze upravit tak, aby vracel počet assetů na zadané ceně
 
```
         ______
B = A × √ P / N
```

Kde `A` je počet asetů na ceně `P` a `N` je nová cena na které vychází počet assetů `B`. Zajímavé na vzorci je také to, že je reflexivní a tranzitivní. Tedy, že též platí:


```
         _______
A = B × √ N / P 
```
(důkaz, dosaďte A horního vzorce a dostane B=B)

Jinými slovy, když nastavím, `B` a `N` jako nový výchozí bod, mohu se zadáním `P` vrátit k původnímu `A`

Tranzitivita se projevuje tak, že si mohu vybrat libovolnou cenu X. Z této ceny a z vypočtených assetů Y přejít na cenu N a získat původní B

```
         _______
Y = A × √ P / X
         _______
B = Y × √ X / N 
```
(důkaz opět dosazením, X se vykrátí , zůstane P/N)

Tato vlastost v praxi znamená, že si stačí pamatovat výchozí nastavení `A` a `P`, a mohu po zadání libovolné ceny `N` spočítat, kolik mám mít assetů na účtu. Robot jednoduše k zadané ceně spočítá požadované množství assetů a vydá pokyn na burzu, aby se tak stalo. 

Pro pochopení důsledku tohoto vzorce je třeba si ještě určit, jak bude vypadat vývoj `currency` na účtu, tedy to, čím se platí. Pro robota se doporučuje, aby na účtu bylo připraveno vždy stejné množství currency jako odpovídá hodnotě assetů. Hodnota držených assetů `C` na ceně `N` je:

```
                 _______            _______
C = B × N = A × √ P / N  × N = A × √ P × N 
```

Stejnou hodnotu musím držet v `currency`

Například, pokud Bitcoin stojí 200000 Kč a mám na účtu právě 1 BTC, pak mám na účtu také 200000Kč v penězích a dohromady má moje portfolio 400000kč. Pokud cena BTC klesne na 180000Kč, pak budu mít na účtu

```
         _____________
B = 1 × √200000/180000 = 1,0541 BTC
         _____________
C = 1 × √200000×180000 = 189736,66 Kč
```

Pokud ale budu provádět nákup na ceně 180000kč, pak mi na účtu zůstane

```
C' = 200000 - (0.0541 × 180000) = 190262 Kč
```

Rozdíl mezi `C` a `C'` se nazývá **normalizovaný zisk**

```
Cn = C' - C = 525,34 Kč
```

Tyto peníze už mohu z účtu odebrat, protože  nikdy nebudou potřeba. Lze totiž využít reflexivity vzorce a prokázat, že **k dalším nákupům** za nižší ceny bude třeba **méně peněz** a při návratu na původní cenu nám tyto peníze k získání přesné poloviny nebudou chybět

```
               _____________
B2 = 1.0541 × √180000/200000 = 1 BTC
               _____________
C2 = 1.0541 × √180000×200000 ~= 200000 Kč

C2' = 189736,66 - (-0.0541 × 200000) = 200556,66 Kč 
```

A opět, rozdíl mezi `C2` a `C2'` se nazývá **normalizovaný zisk**

```
C2n = C2' - C2 = 556,66 Kč
```

Idea normalizovaného zisku je tedy založena na tom, že mám na burze vyhrazené peníze přesně v poměru 50:50 a pouze přelévám prostředky z jedné strany na druhou podle vývoje aktuální ceny. Vůbec přitom není třeba hlídat aktuální hodnotu celého portfolia, protože ta z dlouhodobého hlediska není důležitá. Důležitější je umět správně změřit **normalizovaný zisk**, ten má totiž schopnost generovat **trvalý příjem** bez ohledu na to, co se děje na burze. Důležité samozřejmě je, aby se cena na burze hýbala, a aby se obchodovalo. Nezáleží kam, ale kolik. Z každého obchodu je pak **normalizovaný** zisk kladný.

Robot umožňuje zapnout funkci, kdy část normalizovaného zisku se akumuluje do assetů. V takovém případě může i tento zisk být ovlivněn cenou vlastního assetu. Toto nastavení je třeba si dobře rozmyslet a určit si správně cíle. U potenciálně rostoucího assetu se vyplatí například polovinu zisku akumulovat. U potenciálně klesajícího assetu spíš neakumulovat.

Množství akumulovaných assetů navíc se pak říká **normalizované akumulované assety**. Robot je ovšem začlení do celkové balance `A` a tak se jejich akumulace projeví v drobném zesílení budoucích zisků.

