URL=http://localhost:11223
SETTOKEN="Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJzZXNzaW9uIiwiZXhwIjoxNjg2NzY2NDY2LCJpYXQiOjE2NTIyMDY0NjYsImlzcyI6Im1tYm90Iiwic3ViIjoiYWRtaW4ifQ.M7l-mU_7bEVep8Q3IOh0Mvzk5eUeP5aoiXheF7o8R7I"

curl -s -i -H "$SETTOKEN" -X POST -d @btcusd_data.json $URL/api/backtest/data
curl -s -i -H "$SETTOKEN" -X POST -d @trader.json $URL/api/backtest

