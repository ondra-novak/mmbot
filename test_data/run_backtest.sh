URL=http://localhost:11223
SETTOKEN="Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJzZXNzaW9uIiwiZXhwIjoxNjg0ODU1MDQ5LCJpYXQiOjE2NTAyOTUwNDksImlzcyI6Im1tYm90Iiwic3ViIjoiYWRtaW4ifQ.KzGkcNAQfy43-jTCBUgWJTnYt5yeXjAraZp6ofOu5UU"

curl -s -i -H "$SETTOKEN" -X POST -d @btcusd_data.json $URL/api/backtest/data
curl -s -i -H "$SETTOKEN" -H "Accept: text/event-stream" -X POST -d @trader.json $URL/api/backtest

