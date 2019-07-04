# Obchodování na Deribitu a Bitmexu

## Výstraha

Robot primárně není stavěn pro obchodování na páku. Ale obchodovat je možné s tím, že všechny principy strategie fungují a robot je schopen nákupy níže a prodeji výše vydělávat. 

Díky páce lze navýšit pozici a vydělat víc, než na exchange. Ale má to rizika

## Použijte external_assets

Na marginových burzách neexistují assety ve fyzické podobě, ale pouze pozice. Ta je zpočátku nulová. Vzorec, který robot používá však s nulou nepracuje. Je třeba nastavit nějaký výchozí bod - nějaké výchozí assety, ze kterých se bude počítat. 

Nastavením této položky též definujeme páku se kterou bude robot pracovat. Množství assetů volíme podle zůstatku kolaterálu. Čím větší poměr assetů vůči kolaterálu, tím větší pákou bude robot obchodovat

Příklad: Při ceně 11200 USD za BTC se zůstatkem 1000 USD můžeme obchodovat 1 BTC. Výsledná páka (vůči křivce strategie) bude 11.2x. Pozor nevyjadřuje to aktuální páku na burze. Toto číslo vyjadřuje kolikrát je třeba výsledky robota vynásobit, aby se došlo ke skutečným hodnotám

## Otočená cena na Deribitu (a Bitmexu)

Na rozdíl od jiných pákových burz, na futures Deribit a Bitmex se kontrakty uvádí v USD. Přitom pro pár BTC/USD by měl být kontrakt veden v BTC. Kvůli tomuto otočení musí robot obchodovat tento pár obráceně, jakoby šlo o pár USD/BTC. To vede k tomu, že v prostředí se zobrazuje cena 1 USD v BTC. Tedy při ceně 11200 USD za 1 BTC budete robot zobrazovat cenu 0.000089, nebo 89.28µ. Stejně tak je otočen význam nákupu a prodeje. Nákupem dolarů se vlastně na burze otevírá short (prodej). Prodejem dolarů se vlastně otevír long (nákup). Zisky a ztráty jsou pak uváděny v BTC, tedy přesně tak, jak vycházejí ve výsledcích na oněch burzách 

## Záporná fee na Deribitu

Robot záporná fee neuvažuje a nepočítá s nimi. Jako fee pracuje s nulou, tedy bez fee. Záporná fee jsou benefitem na váš účet ale ve statistikách robota nefigurují

## Rozsah, kde lze obchodovat

Cenový rozsah v okamžiku spuštění robota je obtížné spočítat a výpočty calc_range a highest a lowest tradable price neukazují správné hodnoty.

Pokud jde o odhad rozsahu, jedná se o kvadratickou rovnici s dvěma kořeny

``` 
                  _____
      A×P×(A - 2×√A×D×P + D×P)
x1 = ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
         A² - 2×A×D×P + (D×P)²
         
                    _____
        A×P×(A + 2×√A×D×P + D×P)
x2 = ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        A² - 2×A×D×P + (D×P)²

A = external_assets
P = startovní cena
D = kolaterál (po odečtení initial marginu)
```

Příklad

```
A = 10000 USD
P = 12000 USD
D = 0.1 BTC

x1 = 6287.4 USD
x2 = 25301 USD
```

Z příkladu vyplývá, že obchodovatelný rozsah je mezi 6287.4 a 25301 USD

## Posouvání open price

Pokud cena roste ve vlnkách, je možné, že open price se též posouvá. Je dobré nastavit robota aby chytal nejmenší vlnovky a tím při jakékoliv příležitosti uvolníl prostor pro další posun ve směru pohybu. Tyto obchody se budou zaznamenávat jak ztrátové, protože Deribit počíta zisk a ztrátu podle toho, jestli byla pozice redukováná před open price (ve ztrátě) nebo za ní (v zisku). Na druhou stranu, každá redukce pozice způsobí, že nové otevření pozice posune open_price ve směru hlavního pohybu. Při větším pullbacku pak bude zaznamenán významný zisk

## Trvale otevřená pozice

Robotovi nevadí. Pokud však zaznamenáváte ztráty díky vysokému fundingu, je možné, jakmile se na chvíli pozice octne v zisku, ji zavřít celou. Tohle vás připraví o větší potenciální zisk. Dobře si spočítejte, jestli se ztráty z fundingu nedají prostě přežít

## Nastavení robota

Díky negativním fee není třeba nutit robota do velkého spreadu. Parametr `spread_calc_min_trades` nastavte klidně na vyšší hodnotu, třeba 15 za den (min 15x buy+sell denně)

Nastavte vyšší `dynmult_raise` například na 300 az 400 a lehce vyšší ``dynmult_fall`` například na 2.5. Na futures bývají prudší pohyby následované malými vlnkami. Při prudkých pohybech se násobí spread a po delší době neaktivity se vrátí do úzkého proužku chytat vlnky

 
