## Web Admin - instalace a přechod

### Co se mění přechodem na "webadmin" verzi

Od verze nazvané "webadmin" se mění způsob zadávání nastavení. Namísto ruční editace konfiguračních souborů je možné použít webové rozhraní, které je mnohem pohodlnější a nabízí rozšířené funkce realizované přímo v prohlížeči. K dispozici je i mobilní verze včetně možnosti instalace jako aplikace typu PWA podporované na telefonech Android (a omezeně i na iPhone prostřednictvím Safari)

S přidáním konfigurace prostřednictvím webového rozhraní ale přichází několik změn, které bylo nutné provést, aby webové prostředí fungovalo

- **traders.conf** je deprecated - robot umí traders.conf načíst, ale tato možnost je v nastavení vypnuta zakomentováním řádku s patřičným @include traders.conf. **Protože se neprovádí konverze nastavení, je případně nutné obsah traders.conf znovu zadat do webového rozhraní**

- **http_bind je povinný** - v původní verzi bylo použití `http_bind` pouze volitelný doplňek a propojení s webserverem se realizovalo prostřednictvím mapování složky v na url webserveru. Avšak webadmin je dostupný pouze přes `http_bind`, proto musí být povolen a správně nastaven. Ve výchozím nastavení robot otevírá port `localhost:11223`. Tento port lze namapovat na webserver přes nastavení reverzní proxy

- **konfigurace je jednodušší** - konfigurační soubory jsou teď mnohem kratší, spousta nastavení se děje přímo v prohlížeči. Po instalaci lze robota hned spustit a webové prostředí je bez dalšího nastavení ihned k dispozici

- **nastavení API klíčů se děje přes konfigurační soubory** - tato část se nezměnila. Ve webovém prostředí není možné nastavovat API klíče. K tomu je třeba upravit patřičnou sekci v konfiguračním souboru brokera. Je to záměr i kvůli bezpečnosti, aby neexistovala žádná možnost najít způsob jak přes webové rozhraní ukrást tyto klíče.

### Instalace 

Verzi `webadmin` lze instalovat pouze z větve `webadmin`. Doporučuje se instalovat do odděleného adresáře a nepřepisovat starou verzi. Po převedení nastavení traderů lze pak starého robota vypnout.

(před instalací je třeba zajistit si přítomnost těchto balíků v systému: 
`cmake make g++ git libcurl4-openssl-dev libssl-dev libcurlpp-dev`, Doporučuje se také pro robota vytvořit odděleného uživatele)

```
$ git clone -b webadmin https://github.com/ondra-novak/mmbot.git mmbot_webadmin
$ cd mmbot_webadmin
$ ./update
$ bin/mmbot start
```

### Otevření webového rozhraní

Webové rozhraní je přístupné v prohlížeči na adrese [http://localhost:11223/](http://localhost:11223/) . Na hlavní stránce je (zpravidla z počátku prázdná) report stránka a administraci najdete pod ikonkou ozubeného kola (vpravo nahoře)

Pokud instalujete robota vzdáleně přes `ssh`, pak se doporučuje aktivovat ssh tunel pomocí přepínače `-L11223:localhost:11223`. Tím lze prostředí pomocí stejného odkazu otevřít lokálně a bude fungovat tak dlouho, dokud je `ssh` aktivní

Než však můžete přidávat páry z různých směnáren, je třeba do konfiguračních souborů nastavit API klíče. Webové rozhraní sice umožní vytvořit nastavení pro směnárnu, ke které nemáte API klíč, ale bude na tuto skutečnost upozorňovat pomocí červeného vykřičníku a některé funkce nebudou pracovat správně

Konfigurační soubory klíčů najdete v `conf/brokers/`

Po nastavení klíčů je třeba robota restartovat `bin/mmbot restart`

### Propojení s webserverem

Ačkoliv by bylo možné `http_bind` nasměrovat do internetu, z bezpečnostních důvodů se to nedoporučuje. Navíc `http_bind` nepodporuje **https**! Nastavení **https** je dobré zprovoznit na webserveru přes službu **let's encrypt**

Webserver lze použít **nginx** nebo **apache**. 

Nastavení pro **nginx** - následující část vložíte do patřičné sekce `server { }`

```
location / {
	 proxy_pass http://localhost:11223/;
}
```

Lze rozhraní namapovat i na cestu úpravou `location /cesta {`


Nastavení pro **apache**

```
<Location "/">
    ProxyPass "http://localhost:11223/"
</Location>
```

Více informací hledejte v návodu daného webserveru

### Zabezpečení

Webové prostředí (včetně report stránky) je otevřeno všem, dokud není definován první uživatel s právy administrátora. Bez nastavení administrátora není možné žádné nastavení uložit, proto to udělejte jako první.

Zároveň lze zvolit, jestli report stránka bude veřejná, nebo chráněná heslem pomocí volby u uživatele `<guest>` - ten se sice nedá smazat, ale lze jej vypnout nastavením `no access`

#### když zapomenu heslo?

Existuje způsob, jak se dostat do nastavení i v případě ztráty hesla. Ten způsob vede přes `web_admin.conf` v sekci `[web_admin]`. V této sekci lze nastavit pevný login administrátora v položce `auth`. Způsob nastavení je uveden přímo v configu v komentářové části. Je potřeba tento klíč povolit smazáním # a správně nastavit. Po restartu robota lze nově zvolený login použít pro přihlášení a změnit nastavení zabezpečení

### Jak pracovat s nastavením

- **veškeré volby je třeba uložit** - Dokud není nastavení uloženo, změny v  nastavení se nijak neprojeví. Týká se to i přidávání a mazání traderů. I v případě, že smažete tradera, přesto pořád obchoduje, dokud nastavení není uloženo. Teprve po uložení je smazán a všechny jeho pokyny jsou zrušeny.

- **Některá akční tlačítka mají okamžitou platnost** - jde o funkce **reset** a **repair** a o funkci zrušení všech pokynů. 

- **Dočasné zastavení obchodování** - ať už jedotlivě, nebo pomocí funkce **Global Stop** - všechna dočasná zastavení se obnoví **uložením** nového nastavení (stačí jen klepnout na **Save**)

- **Backtest vyžaduje data** - Funkce Backtestu je k dispozici vždy až po určité době, kdy robot běží. Lze backtestovat i na datech získané po čas běhu v režimu "dry run", ale pozor, jakmile "dry run" vypnete, všechny obchody vytvořené v tomto režimu se smažou 


### Jak importovat stará nastavení?

Obecně nelze importovat volby z `traders.conf`. Je třeba je prostě přepsat. Pokud jde o stavové soubory (obsahují statistiky a záznamy obchodů a data pro výpočet spreadu), ty lze naimportovat jednoduše. Nejprve překopírujte `data` ze staré verze do nové

V okamžiku zakládání tradera, kterého přenášíte z `traders.conf` jednoduše vyplňte `trader's UID` stejné jako máte v konfiguračním souboru. Po uložení by měl robot být schopen tyto data načíst a pokračovat tam kde přestal. Pokud se stane, že se nebudou zaznamenávat nově provedené obchody, tak funkce `repair`  by to měla opravit. Ale nepoužívejte to dokud to není potřeba.

### Pomalá odezva

Webové prostředí není optimalizováno na rychlou odezvu. Proto pomalá odezva prostředí je běžnou věcí a není třeba se tím trápit. Co je příčinou pomalé odezvy?

 1. Webové prostředí běží pouze v jednom vláknu. Nepředpokládá se, že by prostředí používaly stovky uživatelů. a zpravidla se předpokládá, že celý robot pracuje na malé a  výkonově slabé VPS. Proto se větší část CPU nechává pro výpočty robota a webové prostředí má nižší prioritu
 
 2. V některých situacích webové rozhraní přímo zasahuje do výpočtů robota, i v takovém případě mají výpočty přednost a webové rozhraní musí počkat, až se vše bez narušení spočítá
 
 3. Brokeři umí jednu operaci naráz. Pokud tedy brokera zrovna používá robot k ovládání trhu, musí webové prostředí počkat, až se broker uvolní. Někdy také data ze směnárny mohou mít zpoždění, protože brokeři je cacheují aby se redukoval počet requestů za sekundu. U některých směnáren bývá kriticky nízký limit.
 
 4. Websocket není a nebude podporován.
 
  

