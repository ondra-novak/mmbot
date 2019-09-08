# Kuchařka - rady na nastavení

Robota lze provozovat s různými cíly. Vyberte požadovaný cíl a získejte tipy, jaké nastavení použít

## Cíl 1: Zisk bez rizika

Určité množství prostředků chci nechat vydělávat a neřešit technické analýzy ani spekulace. Zároveň vím, že vyhrazené peníze nebudu po určitou delší doby (5 a více let) potřebovat. Ideálně trvale takové peníze vyhradit právě k tomuto účelu.  

Je nutné si uvědomit, že peníze vyhrazené na tento cíl nesmím potřebovat k jinému účelu. Jsou to peníze, které mi mají vydělat, tedy to co mě opravdu nezajímá, je aktuální zisk nebo ztráta z pozice. Naopak, zajímá mne, kolik prostředků z obchodování již mohu průběžně stahovat z účtu na exchange.

Nastavení

```
[name]
pair_symbol=XXXYYY
```

To je celé. Zároveň je třeba obchodní účet rozdělit půl na půl - crypto v hodnotě zbývajícího fiatu. Pokud obchodujeme XXX ku BTC, pak pochopitelně držíme XXX v hodnotě zbývajících BTC.

**Tip** - není třeba fyzicky držet currenct (fiat nebo BTC v případě páru proti BTC) na exchange. Mohou být v Trezoru nebo jiné peněžence a na exchange držet jen minimum. Nicméně pokud robot narazí na nedostatek prostředků, je nutné prostředky následně doplnit. Vždy se snažte vždy dodržet poměr 50:50

**Kolik jsem vydělal?** - Webové prostředí se snaží kreslit graf normalizovaného zisku. Bez grafu pouhým pohledem na exchange si lze udělat představu jednoduše: **Pokud mám v currency víc než hodnotu cryptomincí, tak cokoliv nad rámce této hodnoty je zisk** - příklad: Pokud držím 0.5 BTC a 6000 USD a cena 1 BTC je 11500 USD, pak 250 USD (6000-0.5*11500) je zisk, který mohu z exchange vybrat. Robot totiž na další obchodování bude potřebovat pouze 5750 USD, což je přesně tolik, kolik je 0.5 BTC při aktuální ceně. 

**Kam nekoukat?** - obvyklou chybou při sledování tohoto cíle je sečíst hodnotu cryptomincí a fiatu a zjistit, že to dělá méně než počáteční investice. Chybou je to i v případě, kdy by to bylo více. Stejně tak jako při obyčejném HODLu cena může hodně kolísat, nebo jít dlouhodobě (v trendu jedním směrem). Vzhledem k tomu, že na začátku jsem si deklaroval, že investici budu trvale otáčet v exchange a nemám v plánu ji z tohoto "kolotoče" celou vybírat, tak informace o tom, jesli je aktuálně menší nebo větší než na počátku, není relevantní. To co je relevantní jsou peníze navíc (nad rámec půl na půl)

**Na co si dát pozor?** - často chci obchodování pouze vyzkoušet s malým množstvím a "časem se ukáže". Avšak ve výsledku to znamená, že robot je nucen vytvářet velice malé pokyny, menší, než je dovolené minimum na exchange. V takovém případě běží robot velice neefektivně, nebo neběží vůbec (chybové hlášení z exchange o příliš malých pokynech). Je prostě nutné obchodovat s větším objemem. Protože ale nechci držet veškeré crypto na exchange, mohu použít řešení nastíněné v **Cíl 2**

## Cíl 2: Navýšení objemů - obchodování s minimem

I přes velké množství crypto na exchange robot vytváří pokyny s malým množstvím. Je to dáno tím, že ve vzorci figuruje geometrický průměr aktuální ceny a počáteční ceny vůči počáteční ceně. Tedy nejen že jde o rozdíl cen, ale ještě k tomu rozdíl průměrné ceny vůči počáteční. A to je často velice malé číslo. Existuje jediný způsob jak to zvýšit a to zvýšit množství assetů (cryptomincí), které se účastní výpočtu.

Ale není nutné je všechny držet na exchange

```
[name]
pair_symbol=XXXYYY
external_assets=AAA
```

Hodnota AAA je číslo, který jednoduše říká, kolik assetů držím mimo burzu. Příklad: V Trezoru mám 1 BTC a na exchange mám 0.1 BTC. Robot by počítal s 0.1BTC. Ale protože jsem ochoten v případě potřeby dodat zbývající 1 BTC do obchodu, nastavím external_assets na 1. Tím robot počítá s 1.1 BTC pro výpočet pokynů. Dokud jsem schopen dodat celou částku, může robot obchodovat na jakékoliv ceně.

Stejnou strategii lze použít na currency (fiat). Pokud 1 BTC stojí 11500 USD, pak na exchange mám 1150 USD a zbývajících 10350 USD mám třeba v bance. Jakmmile cena klesne tak, že robot je nucen dokoupit a spotřebuje oněch 1150 USD na exchange, mohu pokynem z banky poslat další fiat na nákup

**Poznámka** - zatímco množství assetů držených v peněženkách musím deklarovat v nastavení, množství currency drženého v bance nemmusím, protože ve výpočtu strategie nikde nefiguruje

Robot nemá možnost zkontrolovat, že uvedené extra assety a currency skutečně existují. Jak toho využít budu diskutovat v **Cíl 4**

## Cíl 3: reinvestice 

Nastavení z **Cíl 1** nechává vydělané prostředky bez užitku na exchange. Našim cílem ale může být jakýkoliv výdělek z cela nebo z poloviny uložit do assetů

Toto ovlivňuje parametry `acum_factor` 
 
 
```
[name]
pair_symbol=XXXYYY
acum_factor=F
```

Hodnota F je v rozsahu 0 až 1, je to desetinné číslo, takže můžete zadat třeba 0.5. Výchozí je 0

 - **0** - currency získané navíc z obchodování zůstávají na exchange, dokud nejsou vybrány
 - **1** - currency získané navíc z obchodování jsou použity k nákupu assetů. Robot přitom neprovádí extra nákup, pouze jen upravuje pokyny tak, aby z obchodu vznikalo víc assetů než currency. Získané assety se přímo reinvestují. V tomto režimu ale může dojít k nerovnováze, kdy na exchange budou cryptomince za větší hodnotu než zbývající fiat. Takové mince navíc můžeme exchange odebrat.
 - **0.5** - currency získané navíc z obchodování jsou použity z poloviny na nákup assetů. Robot přitom neprovádí extra nákup, pouze jen upravuje pokyny tak, aby z obchodu získal vždy půl na půl assety a currency. Ve výsledku je tedy stav na exchange vždy půl na půl. Tento režim je zrádný tím, že přímo na exchange není vidět, kolik se obchodováním vydělalo, protože vždy bude poměr půl na půl. Po čase na stejné ceně jako na začátku však zjistím, že mám víc assetů a currency než vůči počátku. Ale musí to být na stejné ceně. Webové prostředí se snaží průběžně vizualizovat tuto hodnotu jako **normalized profit** a **normalized accumulated assets**
 
## Cíl 4: navýšení objemů bez prostředků - obchodovatelný rozsah

Protože robot nemá jak zkontrolovat, že `external_assets` skutečně existují, dává to možnost zadat tam libovolné číslo a tím zvýšit objemy a potenciální zisk. Ale nese to sebou jiné riziko. Tímto krokem totiž přestane platit výpočet, kdy by robot mohl obchodovat v cenovém rozsahu od nula po nekonečno, tedy kdykoliv bez ohledu na cenu

Základní výpočet je totiž nastaven tak, že s nižší cenou robot víc nakupuje, ale nikdy tak aby nakoupil za veškeré peníze. S rostoucí cenou robot prodává, ale nikdy tolik, aby mu nezbylo něco na prodej.

Jakmile ale `external_assets` naplním číslem, představující assety které nemáme, může se stát, že cena vystoupá tak vysoko, že robot vyprodá veškeré assety na exchange a bude očekává, že chybějící assety doplním. Pokud je ale nemám, tak jsem skončil. Stejná situace nastane při poklesu ceny, pokud stejným způsobem nedodám currency.

A protože se této vlastnosti zneužívá i v dalších cílech (**Cíl 5** a **Cíl 6**), robot umí odhadnout tzv obchodovatelný rozsah. To je cenový rozsah, kdy, podle balance z exchange, lze bezpečně obchodovat, aniz by došly prostředky na jedné i na druhé straně. V tomto cíli se snažím `external_assets` nastavit tak, aby obchodovatelný rozsah dostatečně pokrýval historický vývoj cen obchodovaného assetu. Což může být u crypta docela problém. Například Bitcoin mohu chtít obchodovat v rozsau 3000 USD - 30000 USD. 

Ve webovém prostředí najdu v detailu dvě hodnoty `Highest tradable price` a `Lowest tradable price`. Tyto hodnoty jsou odhadem, v realitě je třeba vzít cca o 5%-10% užší rozsah, v závislosti na aktálním spreadu. 

## Cíl 5: Zisk z pozice a posouvání obchodovatelného rozsahu

 
