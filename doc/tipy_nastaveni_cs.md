# Kuchařka - rady na nastavení

Robota lze provozovat s různými cíli. Vyberte požadovaný cíl a získejte tipy, jaké nastavení použít

## Cíl 1: Zisk bez rizika

Určité množství prostředků chci nechat vydělávat a neřešit technické analýzy ani spekulace. Zároveň vím, že vyhrazené peníze nebudu po určitou delší doby (5 a více let) potřebovat. Ideálně trvale takové peníze vyhradit právě k tomuto účelu.  

Je nutné si uvědomit, že peníze vyhrazené na tento cíl nesmím potřebovat k jinému účelu. Jsou to peníze, které mi mají vydělat, tedy to co mě opravdu nezajímá, je aktuální zisk nebo ztráta z pozice. Naopak, zajímá mne, kolik prostředků z obchodování již mohu průběžně stahovat z účtu na exchange.

Nastavení

```
[name]
pair_symbol=BTCUSDT
```

To je celé. Zároveň je třeba obchodní účet rozdělit půl na půl - crypto v hodnotě zbývajícího fiatu. Pokud obchodujeme XXX ku BTC, pak pochopitelně držíme XXX v hodnotě zbývajících BTC.

**Tip** - není třeba fyzicky držet currenct (fiat nebo BTC v případě páru proti BTC) na exchange. Mohou být v Trezoru nebo jiné peněžence a na exchange držet jen minimum. Nicméně pokud robot narazí na nedostatek prostředků, je nutné prostředky následně doplnit. Vždy se snažte vždy dodržet poměr 50:50

**Kolik jsem vydělal?** - Webové prostředí se snaží kreslit graf normalizovaného zisku. Bez grafu pouhým pohledem na exchange si lze udělat představu jednoduše: **Pokud mám v currency víc než hodnotu cryptomincí, tak cokoliv nad rámce této hodnoty je zisk** - příklad: Pokud držím 0.5 BTC a 6000 USD a cena 1 BTC je 11500 USD, pak 250 USD (6000-0.5*11500) je zisk, který mohu z exchange vybrat. Robot totiž na další obchodování bude potřebovat pouze 5750 USD, což je přesně tolik, kolik je 0.5 BTC při aktuální ceně. 

**Kam nekoukat?** - obvyklou chybou při sledování tohoto cíle je sečíst hodnotu cryptomincí a fiatu a zjistit, že to dělá méně než počáteční investice. Chybou je to i v případě, kdy by to bylo více. Stejně tak jako při obyčejném HODLu cena může hodně kolísat, nebo jít dlouhodobě (v trendu jedním směrem). Vzhledem k tomu, že na začátku jsem si deklaroval, že investici budu trvale otáčet v exchange a nemám v plánu ji z tohoto "kolotoče" celou vybírat, tak informace o tom, jestli je aktuálně menší nebo větší než na počátku, není relevantní. To co je relevantní jsou peníze navíc (nad rámec půl na půl)

**Na co si dát pozor?** - často chci obchodování pouze vyzkoušet s malým množstvím a "časem se ukáže". Avšak ve výsledku to znamená, že robot je nucen vytvářet velice malé pokyny, menší, než je dovolené minimum na exchange. V takovém případě běží robot velice neefektivně, nebo neběží vůbec (chybové hlášení z exchange o příliš malých pokynech). Je prostě nutné obchodovat s větším objemem. Protože ale nechci držet veškeré crypto na exchange, mohu použít řešení nastíněné v **Cíl 2**

## Cíl 2: Navýšení objemů - obchodování s minimem

I přes velké množství crypto na exchange robot vytváří pokyny s malým množstvím. Je to dáno tím, že ve vzorci figuruje geometrický průměr aktuální ceny a počáteční ceny vůči počáteční ceně. Tedy nejen že jde o rozdíl cen, ale ještě k tomu rozdíl průměrné ceny vůči počáteční. A to je často velice malé číslo. Existuje jediný způsob jak to zvýšit a to zvýšit množství assetů (cryptomincí), které se účastní výpočtu.

Ale není nutné je všechny držet na exchange

```
[name]
pair_symbol=BTCUSDT
external_assets=1.2
```

Hodnota `1.2` je číslo, který jednoduše říká, kolik assetů držím mimo burzu. Příklad: V Trezoru mám 1.2 BTC a na exchange mám 0.2 BTC. Robot by počítal s 0.2 BTC. Ale protože jsem ochoten v případě potřeby dodat zbývající 1.2 BTC do obchodu, nastavím external_assets na 1.2. Tím robot počítá s 1.4 BTC pro výpočet pokynů. Dokud jsem schopen dodat celou částku, může robot obchodovat na jakékoliv ceně.

Stejnou strategii lze použít na currency (fiat). Pokud 1 BTC stojí 11500 USD, pak na exchange mám 2300 USD a zbývajících 13800 USD mám třeba v bance. Jakmile cena klesne tak, že robot je nucen dokoupit a spotřebuje oněch 2300 USD na exchange, mohu pokynem z banky poslat další fiat na nákup

**Poznámka** - zatímco množství assetů držených v peněženkách musím deklarovat v nastavení, množství currency drženého v bance nemusím, protože ve výpočtu strategie nikde nefiguruje

Robot nemá možnost zkontrolovat, že uvedené extra assety a currency skutečně existují. Jak toho využít budu diskutovat v **Cíl 4**

## Cíl 3: reinvestice 

Nastavení z **Cíl 1** nechává vydělané prostředky bez užitku na exchange. Našim cílem ale může být jakýkoliv výdělek zcela nebo zpoloviny uložit do assetů

Toto ovlivňuje parametry `acum_factor` 
 
 
```
[name]
pair_symbol=BTCUSDT
acum_factor=0.5
```

Hodnota je v rozsahu 0 až 1, je to desetinné číslo, takže můžete zadat třeba 0.5. Výchozí je 0

 - **0** - currency získané navíc z obchodování zůstávají na exchange, dokud nejsou vybrány
 - **1** - currency získané navíc z obchodování jsou použity k nákupu assetů. Robot přitom neprovádí extra nákup, pouze jen upravuje pokyny tak, aby z obchodu vznikalo víc assetů než currency. Získané assety se přímo reinvestují. V tomto režimu ale může dojít k nerovnováze, kdy na exchange budou cryptomince za větší hodnotu než zbývající fiat. Takové mince navíc můžeme exchange odebrat.
 - **0.5** - currency získané navíc z obchodování jsou použity z poloviny na nákup assetů. Robot přitom neprovádí extra nákup, pouze jen upravuje pokyny tak, aby z obchodu získal vždy půl na půl assety a currency. Ve výsledku je tedy stav na exchange vždy půl na půl. Tento režim je zrádný tím, že přímo na exchange není vidět, kolik se obchodováním vydělalo, protože vždy bude poměr půl na půl. Po čase na stejné ceně jako na začátku však zjistím, že mám víc assetů a currency než vůči počátku. Ale musí to být na stejné ceně. Webové prostředí se snaží průběžně vizualizovat tuto hodnotu jako **normalized profit** a **normalized accumulated assets**
 
## Cíl 4: navýšení objemů bez prostředků - obchodovatelný rozsah

Protože robot nemá jak zkontrolovat, že `external_assets` skutečně existují, dává to možnost zadat tam libovolné číslo a tím zvýšit objemy a potenciální zisk. Ale nese to sebou jisté riziko. Tímto krokem totiž přestane platit výpočet, kdy by robot mohl obchodovat v cenovém rozsahu od nula po nekonečno, tedy kdykoliv bez ohledu na cenu

Základní výpočet je totiž nastaven tak, že s nižší cenou robot víc nakupuje, ale nikdy tak aby nakoupil za veškeré peníze. S rostoucí cenou robot prodává, ale nikdy tolik, aby mu nezbylo něco na prodej.

Jakmile ale `external_assets` naplním číslem, představující assety které nemám, může se stát, že cena vystoupá tak vysoko, že robot vyprodá veškeré assety na exchange a bude očekává, že chybějící assety doplním. Pokud je ale nemám, tak jsem skončil. Stejná situace nastane při poklesu ceny, pokud stejným způsobem nedodám currency.

A protože se této vlastnosti zneužívá i v dalších cílech (**Cíl 5** a **Cíl 6**), robot umí odhadnout tzv obchodovatelný rozsah. To je cenový rozsah, kdy, podle balance z exchange, lze bezpečně obchodovat, aniz by došly prostředky na jedné i na druhé straně. V tomto cíli se snažím `external_assets` nastavit tak, aby obchodovatelný rozsah dostatečně pokrýval historický vývoj cen obchodovaného assetu. Což může být u crypta docela problém. Například Bitcoin mohu chtít obchodovat v rozsahu 3000 USD - 30000 USD. 

Ve webovém prostředí najdu v detailu dvě hodnoty `Highest tradable price` a `Lowest tradable price`. Tyto hodnoty jsou odhadem, v realitě je třeba vzít cca o 5%-10% užší rozsah, v závislosti na aktuálním spreadu. 

## Cíl 5: Zisk z pozice a posouvání obchodovatelného rozsahu

V tomto cíli se soustředím na zisk z pozice (P/L from position). Není pro mne důležité normalizovaný zisk ani neprovádím akumulaci. Využívám aktuální zůstatek assetů na exchange k tomu, abych přímo vydělával currency. Robot musí minimalizovat ztráty z propadu a maximalizovat zisky z růstu.
 
```
[name]
pair_symbol=BTCUSDT
external_asset=5
neutral_pos=center 1
accept_loss=1
sliding_pos.hours=240
sliding_pos.weaken=11
buy_mult=0.8
sell_mult=0.8
force_margin=1
report_position_offset=-0.2
```

Tento cíl už je složitější na nastavení a vyžaduje delší testování. Množství nastavení je už více, ale zkusím je projít.

Jak už bylo popsáno v `Cíl 4` uvedením vyššího `external_assets` znamená vymezení jen úzkého rozsahu cen, ve kterém lze obchodovat. Například 8000-12000 USD, v závislosti na stavu účtu na exchange tedy poměr assets a currency. Pokud cena klesne pod 8000 nebo nad 12000, robot se zastaví a nemůže dál obchodovat, protože na účtu exchange dojdou assety nebo currency. Obchodování bude možné pokud se cena vrátí zpět do rozsahu. 

Tento problém lze řešit posunem rozsahu. Jsou v zásadě dvě možnost, jedna krajní a druhá průběžná

### Neutrální pozice 

Vždy je třeba specifikovat tzv neutrální pozici. To je stav na obchodním účtu, který považuji za neutrální. Pokud mám na obchodním účtu víc assetů, robot to považuje za `long` pozici. Pokud mám víc currency, robot to považuje za `short` pozici. V **Cíl 6** se dostanu k pákovému obchodování, nyní postačí tato definice.

Neutrální pozici mohu definovat jako
 - **množství assetů** - `netural_pos=0.8` - nastaví neutrální pozici na 0.8 BTC a bude se tedy snažit obchodovat kolem této hodnoty. 
 - **množství currency** - `neutral_pos=currency 8000` - nastaví neutrální pozici na 8000 USD. (interně robot přepočítává dolary na bitcoiny dle aktuálního kurzu)
 - **střed** - `neutral_pos=center 1` - V tomto případě robot bude udržovat na exchange stejný poměr bitcoinů a dolarů. Pozor, neplést si se stejnou polovinou v **Cíl 1**. Zde robot pracuje přímo s tím co je na obchodním účtu aniž by započítával `external_assets`. Objemy pokynů se přitom stále řídí součtem `external_assets` a zůstatku. Číslo za `center` uvádí, kolik párů , jež obchoduji, sdílí stejnou currency. Například pokud obchoduji LTCUSDT a BTCUSDT, je možné uvést v obou případech `center 2`. Robot bude udržovat rezervu zůstatku pro oba páry současně.
 
### Hodnota pozice

Jak robot nakupuje a prodává, aktuální P/L je dán tím, jak se daří nakupovat dole a prodávat nahoře. Pokud cena jde proti (dolu v longu a nahoru v shortu), aktuální P/L se snižuje. Neznamená to ale hned ztrátu. Sama pozice má svou vnitřní hodnotu, která je dána potenciálem zhodnocení pokud dojde k otočce. 

Hodnota pozice se kreslí do grafu P/L jako modrá čára na pozadí. 

Pokud se neaktivují žádné funkce posouvající rozsahem, můžeme sledovat, že hodnota pozice roste neustále a pokud trh krásně vlnkuje, tedy neustále se cena vrací k počátku, pak hodnota pozice se vždy přenese do realizovaného P/L.

Klesání hodnoty pozice značí, že dochází k realizaci ztráty. Naopak pokud hodnota pozice roste tak i navzdory klesajícímu aktuálnímu P/L nejde o ztrátu.
  

**Upozornění:** - **nikdy nepoužívám stoploss**, nikdy zbytečně neukončuji spekulaci přímou ztrátou. Vždy se vyplatí nechat robota situaci nějak vyřešit pomocí jeho nástrojů. Ztráty - sice jsou, ale mnohem nižší, než ztráty ze stoplossu. Provedení stoplossu a vyrovnání pozice do svého neutrálního stavu okamžitě sníží hodnotu pozice na nulu a přiznají aktuální P/L jako konečný.

### accept_loss

Volba `accept_loss` je funkce, která odblokuje robota, který nemůže obchodovat v důsledku toho, že na obchodním účtu došly peníze nebo cryptomince. Funkce obsahuje číslo udávající počet hodin, po kterých se funkce aktivuje za předpokladu, že obchodování je zablokováno a cena se pohybuje o  dvojnásobek spreadu oproti poslednímu trade ve směru ven z rozsahu. V takovém případě robot upraví equilibrium tak, aby obchodování bylo opět možné. Dojde k posunu rozsahu směrem k aktuální ceně. Tento posun má pochopitelně **vliv na hodnotu pozice**. Po aktivaci této funkce se pravděpodobně hodnota pozice sníží. Funkce `accept_loss` se používá jako forma stoplossu.
 
### sliding_pos

Volby **sliding_pos.hours** a **sliding_pos.weaken** určují, jak se má robot snažit posouvat střed obchodovatelného rozsahu tak, aby nemuselo dojít k aktivaci `accept_loss`. Číslo `sliding_pos.hours` udává čas v hodinách, za jak dloho dojde k srovnání neutrální pozice s aktuální cenou. Číslo 240 je 10 dní. Vyšší čísla znamenají vyšší zisky ale vyšší riziko, že posun bude pomalejší a dojde posunu ceny až na okraj rozsahu. Nižší čísla sice flexibilněji posouvají neutrální pozici, ale znamenají mnohem menší zisk. Na krátkém úseku lze nastavení testovat v kalkulačce na webovém rozhraní, tedy po tom, co robot nějakou dobu obchoduje. Doporučuji tedy začínat mnohem širším rozsahem

Číslo `sliding_pos.weaken` udává procento snížení objemů se zvyšující se pozicí. Jakmile totiž se pozice pohybuje na vyšších číslech, je výhodnější snížit objemy a tím dočasně rozšířit rozsah ovšem s efektem dočasného snížení zisku z každého obchodu. Předpokládá se, že po čase se buď cena vrátí k původním hodnotám a nebo dojde k vyrovnání v důsledku `sliding_pos.hours`. Opět je třeba tyto čísla odladit backtestem v kalkulačce. Hodnoty uvedení v příkladu jsou výsledky mého interního testování na BTCUSD a ETHUSD

### multiplikátory

Při testech byl zjištěn pozitivní vliv multiplikátorů `buy_mult` a `sell_mult`. Výchozí hodnotou je 1, tedy multiplikace se neuplatňuje. Jiné hodnoty ale způsobí, že velikost pokynu se vynásobí multiplikátorem, a výsledkem je buď nižší nebo vyšší objem v závislosti na tom, zda je multiplikátor nižší nebo vyšší než 1. Obecně mnohem lepší výsledky dosahují čísla nižší než 1. Je to dáno tím, že o co menší objem se zobchoduje teď o to větší objem se zobchoduje v následujícím pokynu. Pokud je to pokyn stejným směrem, výhodněji to posune otvírací cenu. Poku je to pokyn opačným směrem, zvýší se tím potenciální zisk. Čísla je třeba opět vhodně backtestovat.

### další volby

- `force_margin` - si pouze vynutí zacházení jako by šlo o pákovou burzu. Má to vliv zejména na způsob reportování výsledků, kdy se upřednostňují grafy pro měření zisku z pozice
- `repot_position_offset` - webový report odvozuje aktuální pozici od provedených obchodů. To může způsobit, že reportovaná pozice je jiná, než pozice skutečná. Pokud zjistím tento rozdíl, uvedu ho do této volby. Pak by reportovaná pozice měla odpovídat te skutečně. Není to prováděno automaticky z různých důvodu, zejména proto, že neutrální pozice nemusí být konstantní a tak by výsledek nebyl vypovídající.


## Cíl 6: Obchodování na inverzích futures

Příkladem inverzní futures je Deribit.

```
[name]
pair_symbol=BTC-PERPETUAL
external_asset=20000
neutral_pos=0
max_pos=4000
accept_loss=1
sliding_pos.hours=240
sliding_pos.weaken=11
buy_mult=0.8
sell_mult=0.8
report_position_offset=1250
```

Toto je speciální případ nastavení. Je třeba si uvědomit, že inverzní futures obchodují kontrakty, které mají velikost většinou v dolarech. Jako currecny tak tady vystupuje Bitcoin a kontrak v dolarech je asset.

Z toho důvodu jako `external_assets` zadávám hodnotu v dolarech. Představuje něco jako maximální možnou pozici, kterou bude robot obchodovat. Pokud vložím na Deribit 2 BTC při ceně 10000 USD, pak maximální pozici, kterou robot může vzít je 20000 USD, čímž je vklad plně pokryt. Navíc robot tuto pozici nevezme naráz, ale postupně, jak bude cena růst (ve skutečnosti to bude prezentováno jako 20000 short). V takovém případě nehrozí likvidace ze shortu.

Na druhou stranu, pokud začne cena klesat, klesat začne i vklad a likvidace z longu hrozí vždy. V krajním případě (BTC=0) budu tedy Deribitu dlužit 20000 USD.  

Hodnotu `external_assets` volím z ohledem na můj kolaterál. Ta taky vymezuje obchodovatelný rozsah, který uvidím na webovém rozhraní. Z počátku doporučuji volit hodnotu 10x hodnota kolaterálu v dolarech. Při kolaterálu 1 BTC za 10000 USD tedy zadávám 100000 USD. To vytvoří dostatečně široký rozsah  6000-25000 USD.

Pokud se začne obchodovat mimo rozsah, hrozí likvidace. Na rozdíl od exchange mě Deribit nechá obchodovat až do likvidace, nedojde tedy k zastavení obchodování jako u exchange (dlouhou dobu před tím ale už bude větší pokyny Deribit odmítat). Proto je vhodné velikost pozice limitivat pomocí `max_pos`. Tam následně může zapůsobit `accept_loss`

Volby `neutral_pos` uvádím 0, protože chci, aby to odpovídalo pozici na burze. Ale dává smysl uvést i `center 1`, V takovém případě robot bude neutrální pozici umísťovat na pozici 1/2 hodnoty kolaterálu - do shortu 

Ostatní volby jsou stejné jako v **Cíl 5**

### další volby

- `report_position_offset` - Pozor u inverzních futures se rozdíl uvádí obráceně (protože interně je význam short a long prohozen). Proto je třeba rozdíl zadávat s opačným znaménkem.



# Řešení problémů

1. **Pokyny na nekonečných cenách a nekonečných objemech** - zkontrolujte hodnotu spreadu a multiplikátorů. 
2. **Spread je příliš úzký** - zpočátku obchodování nemusí mít robot dost údajů na výpočet spreadu. Můžete spread nastavit na fixní hodnotu `force_spread=0.01`. Hodnota je logaritmická. Nižší čísla tvoří nižší spread. Vyšší čísla naopak vyšší spread. Po čase doporučuji funkci vypnout. **Pokud je spread dále spočítán jako příliš úzký**, zkuste poštelovat volbu **spread_calc_min_trades** - zadejte nižší číslo (např 4, 3, 2, 1).   
3. **Multiplikátory jsou moc vysoké** - Vysoké multiplikátory mohou být důsledkem úzkého spreadu. Pokud tomu není tak, pak zkontrolujte **dynmult_raise** a **dynmult_fall**. Pokud si nevíte rady, zkuste před volbu dát # a tím je deaktivovat
4. Obecně platí, že kdykoliv začne robot dělat něco nevhodného, hledejte chybu v nastavení. Ideální je vypnutí všech volitelných voleb uvedením # před volbu
5. Interní stav robota lze vynulovat pomocí `bin/mmbot repair <trader>`
6. Statistiky lze smazat pomocí `bin/mmbot reset <trader>`
7. Vypsat config pro daného tradera `bin/mmbot show_config <trader>`
7. Vypsat výchozí hodnoty configu `bin/mmbot show_config default`



