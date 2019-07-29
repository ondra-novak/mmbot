#
# example of configuration of real running instance 
#

[traders]
list=btcczk ltcczk grinbtc bchsvbtc usdeth_deribit usdbtc_deribit xrpczk

[common]
dynmult_raise=200
dynmult_fall=1
spread_calc_max_trades=50
spread_calc_min_trades=1
dry_run=0
acum_factor_buy=0.5
acum_factor_sell=0.5
detect_manual_trades=0

[coinmate]
@template common
start_time=1561100016000
broker=coinmate

[poloniex]
@template common
broker=poloniex

[binance]
@template common
broker=binance

[deribit]
@template common
broker=deribit
acum_factor_buy=0
acum_factor_sell=0
buy_step_mult=0.9
sell_step_mult=0.9
spread_calc_max_trades=50
spread_calc_min_trades=12
dynmult_raise=200
dynmult_fall=1
sliding_pos.change=5

[ltcczk]
@template coinmate
pair_symbol=LTC_CZK
external_assets=25
title=LTC/CZK
sliding_pos.change=5
sliding_pos.change.assets=15


[btcczk]
@template coinmate
pair_symbol=BTC_CZK
external_assets=0.8
title=BTC/CZK
sliding_pos.change=5
sliding_pos.assets=0.2

[xrpczk]
@template coinmate
pair_symbol=XRP_CZK
external_assets=10000
title=XRP/CZK
sliding_pos.change=10

[grinbtc]
@template poloniex
title=GRIN/BTC
pair_symbol=BTC_GRIN
external_assets=0

[bchsvbtc]
@template poloniex
title=BSV/BTC
pair_symbol=BTC_BCHSV
external_assets=0
acum_factor_buy=0
acum_factor_sell=0

[btcusdt]
@template binance
title=BTC/USDT
pair_symbol=BTCUSDT
external_assets=0.25

[usdbtc_deribit]
@template deribit
pair_symbol=BTC-PERPETUAL
external_assets=14000
title=USD/BTC

[usdeth_deribit]
@template deribit
pair_symbol=ETH-PERPETUAL
external_assets=10000
title=USD/ETH
