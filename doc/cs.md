# MMBOT 2.0

## Změny 

V původním dokumentu zde byl popsán algoritmus robota a postup jak jej zprovoznit. Všechny 
tyto texty jsou ve verzi 2.0 zastaralé. Verze 2.0 se instaluje jinak, provozuje jinak a výpočty
jsou též jiné

* Na výběr je několik strategií (každá přináší jinou sadu výpočtů)
* Veškeré nastavení se provádí přes webové rozhraní. Konfigurační soubor obsahuje minimum nastavení




## Jak robot funguje

Obchodní robot MMBot 2.0 (dále jen Robot) provádí automatizované obchodování formou tzv.
market makingu. To je technika, kdy robot generuje zisk při obchodování ve spreadu, tedy v 
situacích, kdy se cena nehýbe moc, nebo vytváří vlnky, kdy se cena po růstech vrací do původního
cenového pásma, atd. V situacích, kdy cena dlouhodobě trenduje robot nefunguje efektivně a tím hůře,
čím silnější trend je. Ovšem statisticky drtivá většina trendů se láme v korekci, ve kterých často
robot nabere zpět to co trendem ztratil a ještě něco navíc. Navíc, trendová období netrvají dlouho, zpravidla jde o krátké pumpy a dumpu následované několika dny klidu nebo vlnkování.

## Kolik robot vydělává

Výše výdělku závisí na trhu i zvoleném nastavení. Neexistuje univerzální recept jak provozovat robota ziskově. Potenciál zisku pramení z market makingu v tom, že robot nenakupuje dráž než prodává (s výjimkou situací, kdy musí, protože se dostane do kraního stavu)

Z dlouhodobého provozu robota bylo dosaženo až 150% zisku na ETH/USD na Deribit za 4 měsíce provozu, dále cca 40% zisku na BTC/USD též na Deribitu. 

Kromě toho, jedna ze strategií umožňuje 100% úspěšnost ovšem při zisku kolem 10% ročně založeného na principu toho, že po každém obchodu na účet přibudou prostředky, které již k činnosti robota nejsou potřeba a lze je odebrat. Tato strategie generuje trvalý profit bez nebezpečí, že by se robot dostal do situace, že by mu došly prostředky a nemohl obchodovat. Lze tedy robota nechat obchodovat po mnoho let a vydělané prostředky z něho postupně stahovat. Strategie se jmenuje Half-Half.

Robota v tuto chvíli není možné nastavit aby provozoval skalpovací strategie a/nebo obchodoval podle indikátorů. Jediné indikátory, které se používají jsou `SMA` (Simple Moving Average) a `STDEV` (standard deviation - obecně `Bollinger Bands`) a to pro výpočet šířky spreadu, přičemž SMA část lze zvolit jinou délku, než STDEV část (Standardní indikátor BB vyžaduje výpočet průměru za stejné období jako odchylky)
 
 
## Instalace robota

* Robot se instaluje na počítač s operačním systémem Linux, odzkoušena je instalace na Ubuntu 18+. Nevylučuje to instalaci na jiný typ Linuxu, jen to není vyzkoušené
* Instalujte robota jako uživatel, **nikoliv jako root**. Provozujte robota též jako uživatel. Je vhodné pro robota vytvořit nového uživatele, aby byl oddělen od ostatních částí systému. Lze provozovat víc instancí robota, každého jako jiný uživatel
* Pro pohodlné ovládání budete potřebovat webserver, například **nginx**. Robota lze sice pustit a ovládat bez webserveru, ale neobsahuje žádné zabezpečení, takže tento způsob považuju za pouze dočasné řešení

### postup
 
1. použijte `git clone https://github.com/ondra-novak/mmbot.git` 
2. přejděte do adresáře `mmbot` 
3. spusťte `./build`

Dosta pravděpodobně instalace selže, protože na vašem systému nejsou nainstalované důležité baliky. Získejte následující balíky 

```
sudo apt-get install g++ git libcurl4-openssl-dev libssl-dev libcurlpp-dev
```

Jakmile instalace projde, najdete aplikaci ve složce bin/mmbot

## První spuštění

Ve výchozím stavu není webové rozhraní povoleno pro externí port. Pokud se připojujete na vzdálený server, kam robota instalujete, přidejte si do připojení tunel (ssh tunel). Příkazová řádka ssh může vypadat takto

```
$ ssh -L10000:localhost:10000 <user@addr.net>
```

Poté přejděte do složky robota (do té výchozí, nikoliv do bin) a napíšte

```
$ bin/mmbot -p 10000 start
```

Pokud nenastala chyba, příkaz nic nevypíše. Pokud napíše, že port 10000 je obsazen, zkuste celý postup opakovat s jiným číslem (např o 1 vyšším).
Po spuštění robota bude k dispozici na vašem počítači webová stránka http://localhost:10000/ .Přes tuto stránku se dostanete do administrace kliknutím na ozubené kolečko

## Počáteční nastavení

1. Administrace je přístupná do okamžiku, než si založíte login. Není možné nic jiného nastavit,
 dokud není založen aspoň jeden administrátor. Nastavení účtu proveďte v sekci `Access control`
 
2. Ve stejné sekci (`Access control`) nastavte i API klíče k jednotlivým směnárnám - samozřejme
jen těm, které budete používat. Nastavení klíčů se uplatní po uložení přes tlačítko Save

**Poznámka**: Nastavení kllčů lze upravovat jen do prvního znovunačtení stránky. Pak už je
nelze zpět vyvolat a jediná možná akce je smazání klíču. Teprve po smazání klíčů lze nastavit nové

## Přidání obchodovaných párů

Tlačítkem + lze přidat obchodované páry. Obchodování se spustí po uložení, pokud pár má zaškrtnutý "Enable" a vyškrtnutý "Dry run". Tyto volba jsou v tomto stavu od začátku jako forma pojistky.

## Zastavení robota

```
$ bin/mmbot stop
```

## Restart robota po nastavení

```
$ bin/mmbot restart
```

Pokud je robot spuštěn bez parametru `-p`, není otevřen port pro přístup do nastavení a statistik.
Přístup je ale možný trvale přes webserver a unixový socket, který se doporučuje spíš než otevřený port


## Propojení s webserverem

Při běhu robota je k dispozici unixový socket, který naleznete v adresáři `run`: 

```
run/mmbot.socket
```
(složka je relativně k instalační složce)

### Připojení na nginx

* Prostudujte si nastavení `proxy_pass`
* Nastavení lze provést přidáním do configu nginxu do sekce `server { }`

```
server {
   ...
   ...
   
   location / {
   		proxy_pass http://unix:/<cesta na mmbot>/run/mmbot.socket:/;
   }
}
```
Kde `<cesta na mmbot>` doplňe absolutní cestu na složku s mmbotem. V zásadě je to o tom, že unix: napíšete absolutní cestu na onen `mmbot.socket`

Po restartu nginxu by měl být robot dostupný na zvolené adrese/doméně/cestě


### Zařídit si https

1. Potřebujete doménu
2. do sekce `server` vložte `server_name <domena>;`
3. ujistěte se, že robot je dostupný přes http protokol na zadné doméně.
4. nainstaluje `python-certbot-nginx`  a spusťte `certbox --nginx` jako root a postupujte podle instrukc9

## Spouštění robota při restartu počítače

K tomuto doporučuji jednoduchý trik: V rámci uživatele, který spouští robota zadejte

```
$ crontab -e
```

V následujícím editor přidejte na konec

```
@reboot /cela/cesta/na/bin/mmbot start
```

(samozřejmě se správnou cestou)


Soubor uložte a měl by se aktivovat automatický strart

 
 
## Strategie

### Linear

Nejjednodušší strategie, která nejlépe funguje tam, kde očekáváme vyšší zisky,vyšší riziko a 
případně s marginem, futures, tedy tam, kde lze mít long nebo short pozici. Lze
ji provozovat i na klasických exchange kde není short s tím, že se short se vytvoří posunutím
neutrální pozice - tedy že určité množství assetů na účtu se prohlásí za nulový stav a short se pak realizuje odprojeme těchto assetů nad toto množství

Startegie odvozuje velikost pokynu přímo úměrné vzdálenosti od posledního obchodu (případně od equilibria)

Lze nastavit
 * **Velikost přírustku pozice při změně ceny o 1 %** - Toto určuje rizikovost strategie. Čím vyšší číslo, tím větší objemy, ale tím rychleji může dojít k liquidation.
 * **Neutrální pozici** - specifikuje jaké množství assetů je neutrální pozice. Tam kde lze shortovat nechte nulu
 * **Maximální pozice** - Definuje při jaké pozici doje přepnutí strategie do režimu redukce. I v tomti režimu může docházet k navyšování pozice, ale ne tak razantně. Robot redukuje pozici kdykoliv
 se objeví opačný pohyb a dojde k exekuci příslušného pokynu. Jakmile je pozice redukována pod
 tuto hranici, přepne se strategie zpět do normálního režimu. Jakékoliv razantní redukování pozice může znamenat ztrátu. Avšak pořád ta ztráta je menší, než stoploss, který se obecně nedoporučuje používat
 * **Akumulace** - Má význam pro směnárny, kde nakoupení assety jsou ve vašem vlastnictví. Pokud je
 akumulace nastavena na 1, pak zisk z obchodování je použit k navýšení assetů. Na margin a futures směnárnách by to vedlo k postupnému zvyšování longu.
 
### Keep value

Tato strategie si klade za cíl udržet hodnotu assetů na počáteční hodnotě ať se cena pohne
jakýmkoliv směrem. Pokud hodnota klesne, dokoupí, pokud hodnota stoupne, prodá. Vyrovnání
vždy dochází ve skocích definované spreadem.

* **external assets** - lze nastavit kladné číslo určující kolik assetů držíte mimo burzu. Případně záporné číslo představující, kolik assetů se nepočítá do hodnoty

* **accumulation** - zisk z obchodování se použije k nákupu assetů. Tyto navíc assety se neúčastní systému udržení hodnoty

### Half half

Tato strategie se snaží, aby poměr mezi hodnotou assetů a penězi na účtu ve směnárně byl v rovnováze. Kdykoliv je rovnováha narušena, robot rovnováhu nastolí obchodem. Tato strategie je určena pro klasické směnárny. Její výhodou je, že je spočítána přesně tak, aby robotovi nikdy nedošly ani assety ani peníze a to na jakékoliv ceně. Nevýhodou je, že zisky z tohoto obchodování jsou relativně nízké a vyžaduje volatilní trhy

 
* **external assets** - lze nastavit kladné číslo určující kolik assetů držíte mimo burzu. 

* **accumulation** - zisk z obchodování se použije k nákupu assetů. Tyto navíc assety se následně započítávají do rovnováhy.


## Různé druhý trhů a jak si s nimi robot poradí

Nastavení strategie silně záleží i na typu trhu, na kterém se obchoduje. A ačkoliv způsob obchodování je zpravidla vždy stejný, přesto se liší v ruzných nuancích, které ale ovlivňují nasazení určité strategie

### Klasická exchange

Klasická exchange je typ trhu, na kterém se směnuje jeden druh assetu za jiný, nebo peníze za asset. Směnou vždy o jeden asset přicházíme a jiný získáváme. Hodnota držených assetů se směnou nemění, ale mění se hodnota pouze se změnou ceny jednoho assetu vůči jinému. 

Robot pomocí nakupování za nižší cenu nebo prodejem za vyšší cenu se pokouší maximualizovat hodnotu v obou držených assetech. Toto obchodování má dvě krajní meze, totiž okamžik, kdy dojde jeden nebo druhý asset (nebo asset a peníze). Tato situace je nemilá, ale nikoliv problematická. Držený asset stále má hodnotu a i když v ten okamžik jeho hodnota není výhodná, situace se může změnit.

Robota lze z tohoto stavu odblokovat pomocí aktivní funkce `Accept loss`, která po nastavené době přijme malou ztrátu, posune equilibrium a pokusí se restartovat obchodování prodáním nějakého assetu pod cenou. Tam se může stát, že robot po nějakou dobu bude nakupovat dráž, než prodával, ale je to jisté řešení, jak pokračovat v obchodování a ztrátu dohnat

Doporučené strategie **Half-Half** nebo **Keep-Value**. Strategie **Linear** funguje taky, ale díky tomu, že dochází ke směně, je obchodovatelný rozsah velice úzky, a není možné shortovat. Short lze nahradit posunutím neutrální pozice třeba do středu hodnoty na účtu, kdy určitá část assetů je prohlášena za nulu a short je pak stav, kdy na účtu je méně assetů, než neutrální nula. Statistiky pak počítají takový stav jako short, tedy je to rozdíl mezi aktuálním ziskem a hypotetickým ziskem získaným tím, když by se assety neobchodovaly, ale pouze držely

### Margin exchange a bežné futures

Robota lze provozovat na marginových burzách a na běžných futures. V takovém trhu lze provádět i short obchody. Běžné futures se vyznačují tím, že držená pozice nemění své množství v závislosti na ceně a že tudíž velikost pozice odpovídá držení daného assetu. Například LONG 10 BTC na margin exchange BTC/USD je ekvivalentní jako nákup 10 BTC na klasické exchange a zisk z takové pozice vychází stejně.

Výhodou marginové exchange a futures je v tom, že směna neznamená ztrátu hotovosti a opačná směna pouze změni stav hotovosti podle relizované ztráty nebo zisku. Samotná výše směny je víceméně bez nákladů, pouze část hotovosti je blokována v marginu.

Robot dobře funguje v marginových burzách s vysokou pákou. Je třeba si uvědomit, že vysoká páka nezvyšuje obchodovatelný rozsah. Ten je dán celkovou možnou ztrátkou, kterou je možné držet na účtu před likvidací. Nižší margin jen omezuje možnost navyšovat hodnotu na krajích rozsahu, protože exchange už nedovolí vypsat další pokyn (v takovém případě může pomoci funkce `Accept loss`]. Na vysokopákových exchanges a futures je vlastní margin zanedbatelný. Velká pozice totiž vždy má mnohem větší nerealizovaný zisk, než je vlastní margin pozice a to často až 10x více.

Příklady běžných futures: Bitmex ETH/XBT, LTC/XBT (cokoliv/XBT}

Pro obchodování na Margin exchange a futures se doporučuje strategie **Linear**


### Inverzni futures

Na trzích s kryptem jsou oblíbené tzv. inverzní futures. To jsou futures, na kterých se obchoduje asset vůči americkému dolaru, nebo jiné měně, ale kolaterál je veden v assetu a zisky a ztráty jsou též připisovány v assetech (např BTC). Velikost vzaté pozice se mění s tím, jak se mění vlastní cena assetu atd.

Invezní futures jsou definovány jako futures převrácených hodnot. Robot s nimi nakládá tak, že se kótuje inverzní hodnota assetu, tedy že kontrakt je veden ve měně, a cena je vedena jako hodnota kontrakru v assetech (čili převráceně, než je kótována na exchange).  Toto převrácení ale zajišťuje už samotný broker process a robot od tohoto procesu dostane informaci, že výsledky má před prezentací opět převrátit zpět do normálního tvaru. 

V inverzních futures je tedy long držen jako short, nákup je proveden jako prodej a cena je zobrazena jako 1/x. Na místech, kde se zobrazují méně důležité informace, v základní podobě tak lze u inverzní futures zahlédnout neupravenou hodnotu. Long pozice může být záporná, cena může být setinách až tisícínách.

Příklady inverzní futures: Deribit BTC/USD, ETH/USD. Bitmex XBT/USD. Na Deribitu je třeba depositovat BTC pro BTC/USD a ETH pro ETH/USD.

Pro obchodování Inverzních futures se doporučuje strategie **Linear**. Využít lze i strategii **Keep Value** pro hedge, nebo spekulace u assetů, u kterých je vysoká pravděpodobnost, že nebudou výrazně růst. Při obchodování **Keep Value** začínáme otevřením **short** pozice. Robot se pak snaží udržet hodnotu otevřené short pozice. Protože hodnota té pozice je dána cenou assetu, tak poklesem ceny robot short pozici zavírá, ale za normálních okolnosti vychází plné zavření short pozice na ceně 0. Velikost short pozice volíme s ohledem na cenu likvidace na vyšších cenách.

### Quanto futures

Quanto futures je vynález BitMEXu a je to způsob jak obchodovat ETH/USD bez nutnosti mít účet v USD nebo ETH (pro případ inverzních futures). V době psaní článku je USD odvozeno od BTC s kurzem 1mil USD. Pohyb ceny o 1 USD tak znamená pohyb o 0.000001 BTC.

Robot obchoduje ETH/USD jako ETH/XBT s tím, že cena je kótována v BTC. Díky tomu velikost pozice musí přepočtena na mikrobitcoiny, tedy long 123 ETH se vypisuje jako 0.000123ETH - protože pokud BTC stojí 1mil USD, je toto ekvivalentní pozice


### Forex, CFD

Obchodování na forexu a CFD je novinkou a aktuálně jej zajišťuje broker process "simplefx". Na rozdíl od exchange, CFD broker nenabízí klasické LIMITní příkazy, takže je nelze vypsat přímo na platformě. Také postupné otevírání pozice vede na několik pozic
samostatně evidovaných na platformě. Velkou otázkou také hraje spread.

Robot ve spolupráci s činnosti broker processu zvládá obchodovat a eliminovat některé nevýhody plynoucí z odlišnosti CFD od exchange.

* Broker process emuluje LIMITní příkazy a eviduje je u sebe. Nastavení limitních příkazů se tedy nepřepisuje na platformu brokera - CFD Broker nemá šanci zjistit, na jaké ceně máte otevřené LIMITní příkazy

* Broker process sleduje živý stream kótací a pokud se objeví cena odpovídající některému limitnímu příkazu, okamžite exekuuje pokyn na platformě jako MARKET order. Narozdíl od exchange se u CFD nezmění cena exekuce podle velikosti.

* Pokud dochází k exekuci opačného směru, než je držená pozice (redukce), probíhá k uzavírání existujících pozic v pořadí FIFO. Toto je funkce vlastního CFD Brokera na API, robot nemůže zvolit jiné pořadí

* Protože exekuce probíhá v reakci na kótaci, může dojit vlivem pomalé odezvi sítě k posunu ceny (slippage) a k exekuci na jiné ceně, než byl LIMITní příkaz vypsán. Naštěstí ten rozdíl nebývá velký

* Broker process se snaží maximálně eliminovat efekt spreadu. Provádí tak reálnou simulaci orderbooku, ve kterém jsou dva obchodníci a oba kótují obě strany orderbooku. Dokud neexistuje průnik mezi BID a ASK, k obchodu nedojde. Jakmile se objeví průnik, exekuuje se obchod zpravidla poblíž ceny, na které byl příkaz vypsán

* LAST CENA se pro účely zobrazení počítá jako střední cena v simulovaném orderbooku, přičemž opět do toho vstupuje jak kótace brokera, tak kótace robota. Ve výsledku tedy LAST cena leží vždy mezi nákupním a prodejním příkazem a pokud by došlo k vyjetí z rozsahu, doje k exekuci. Protože ale kótace robota mají vliv na hodnotu LAST ceny, nemusí graf LAST ceny přesně odpovídat grafu malovného na platformě, protože platforma často zobrazuje jen nákupní cenu, případně jen prodejní, nebo středovou, aniž by se do toho promítala kótace robota.

Doporučená strategie na CFD je **Linear**

  
 
