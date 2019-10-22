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
 
1. použijte `git clone https://github.com/ondra-novak/mmbot.git` (v současném stavu přidejte `-b 2.0`, ve větvi 2.0)
2. přejděte do adresáře `mmbot` 
3. spusťte `./update`

Dosta pravděpodobně instalace selže, protože na vašem systému nejsou nainstalované důležité baliky. Získejte následující balíky 

```
cmake make g++ git libcurl4-openssl-dev libssl-dev libcurlpp-dev
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


 



 
