# Offline sweep

20 seeds per cell, 50 Hz radar, one-way delay/jitter per profile, deadline 30 ms, R=3, RTO 20 ms. Estimator = Python port of the board EKF.

| link profile | loss | policy | delivered | age p50 / p99 / max (ms) | mean pos. error (m) | final err (m) |
|---|---|---|---|---|---|---|
| uniform loss, 5+-2 ms | 0.00 | raw | 100.0% | 5.5 / 7.0 / 7.0 | 0.223 | 0.124 |
| uniform loss, 5+-2 ms | 0.00 | retx | 100.0% | 5.5 / 7.0 / 7.0 | 0.223 | 0.124 |
| uniform loss, 5+-2 ms | 0.00 | deadline | 100.0% | 5.5 / 7.0 / 7.0 | 0.223 | 0.124 |
| uniform loss, 5+-2 ms | 0.05 | raw | 95.1% | 5.2 / 7.0 / 7.0 | 0.238 | 0.134 |
| uniform loss, 5+-2 ms | 0.05 | retx | 100.0% | 6.1 / 44.0 / 78.0 | 0.223 | 0.124 |
| uniform loss, 5+-2 ms | 0.05 | deadline | 95.1% | 5.2 / 7.0 / 7.0 | 0.238 | 0.134 |
| uniform loss, 5+-2 ms | 0.10 | raw | 90.1% | 5.2 / 7.0 / 7.0 | 0.253 | 0.144 |
| uniform loss, 5+-2 ms | 0.10 | retx | 100.0% | 9.6 / 79.4 / 189.0 | 0.223 | 0.124 |
| uniform loss, 5+-2 ms | 0.10 | deadline | 90.1% | 5.2 / 7.0 / 7.0 | 0.253 | 0.144 |
| uniform loss, 5+-2 ms | 0.20 | raw | 79.4% | 5.5 / 7.0 / 7.0 | 0.292 | 0.177 |
| uniform loss, 5+-2 ms | 0.20 | retx | 97.3% | 242.3 / 468.8 / 862.0 | 0.226 | 0.125 |
| uniform loss, 5+-2 ms | 0.20 | deadline | 79.4% | 5.5 / 7.0 / 7.0 | 0.292 | 0.177 |
| uniform loss, 5+-2 ms | 0.30 | raw | 68.5% | 5.5 / 7.0 / 7.0 | 0.358 | 0.216 |
| uniform loss, 5+-2 ms | 0.30 | retx | 72.9% | 772.5 / 1443.3 / 1912.0 | 0.260 | 0.134 |
| uniform loss, 5+-2 ms | 0.30 | deadline | 68.5% | 5.5 / 7.0 / 7.0 | 0.358 | 0.216 |
| burst loss (mean 5), 5+-2 ms | 0.10 | raw | 91.2% | 5.5 / 7.0 / 7.0 | 0.253 | 0.133 |
| burst loss (mean 5), 5+-2 ms | 0.10 | retx | 98.0% | 10.3 / 223.2 / 421.0 | 0.228 | 0.136 |
| burst loss (mean 5), 5+-2 ms | 0.10 | deadline | 91.2% | 5.5 / 7.0 / 7.0 | 0.253 | 0.133 |
| burst loss (mean 5), 5+-2 ms | 0.20 | raw | 79.7% | 5.5 / 7.0 / 7.0 | 0.313 | 0.159 |
| burst loss (mean 5), 5+-2 ms | 0.20 | retx | 91.7% | 286.6 / 617.0 / 1249.0 | 0.244 | 0.138 |
| burst loss (mean 5), 5+-2 ms | 0.20 | deadline | 79.7% | 5.5 / 7.0 / 7.0 | 0.313 | 0.159 |
| high jitter, 20+-20 ms | 0.00 | raw | 100.0% | 20.4 / 40.0 / 40.0 | 0.238 | 0.121 |
| high jitter, 20+-20 ms | 0.00 | retx | 82.8% | 1109.2 / 2100.1 / 2237.0 | 0.217 | 0.173 |
| high jitter, 20+-20 ms | 0.00 | deadline | 72.3% | 15.1 / 29.9 / 30.0 | 0.332 | 0.192 |
| high jitter, 20+-20 ms | 0.05 | raw | 95.1% | 19.6 / 39.9 / 40.0 | 0.263 | 0.133 |
| high jitter, 20+-20 ms | 0.05 | retx | 76.2% | 1135.3 / 2195.9 / 2296.0 | 0.228 | 0.338 |
| high jitter, 20+-20 ms | 0.05 | deadline | 68.9% | 13.9 / 29.8 / 30.0 | 0.360 | 0.206 |
| high jitter, 20+-20 ms | 0.20 | raw | 79.4% | 20.4 / 39.9 / 40.0 | 0.292 | 0.175 |
| high jitter, 20+-20 ms | 0.20 | retx | 60.0% | 1305.5 / 2505.7 / 2687.0 | 0.223 | 0.113 |
| high jitter, 20+-20 ms | 0.20 | deadline | 57.0% | 14.7 / 29.8 / 30.0 | 0.442 | 0.289 |
