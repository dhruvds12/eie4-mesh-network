To estimate battery life you need to:

1. **Characterize every power state** (from the Heltec/Laird ESP32-LoRa V3 board’s datasheets):

   * **LoRa radio (SX1276):**

     * Tx: 120 mA @ +20 dBm
     * Rx: 10.8 mA
     * Sleep: \~1 µA ([nicerf.com][1])
   * **ESP32 SoC (Wi-Fi & BLE):**

     * Wi-Fi Tx (802.11n @+14 dBm): \~180 mA
     * Wi-Fi Rx/listen: \~95–100 mA
     * BLE Tx (0 dBm): \~130 mA
     * BLE Rx/listen: \~95–100 mA&#x20;
   * **Low-power modes (ESP32):**

     * Modem-sleep (CPU active, radios off): 20–31 mA
     * Light-sleep: 0.8 mA
     * Deep-sleep (ULP on): 150 µA
     * RTC-only (ULP off): 10 µA&#x20;

2. **Decide your operating‐cycle** (e.g. 1 hour):

   * How many LoRa Tx per hour, and duration of each (t<sub>Ltx</sub>)?
   * How long BLE is active (scanning, advertising) per hour (t<sub>BLE</sub>)?
   * How long Wi-Fi is up (associations, data transfer) per hour (t<sub>WiFi</sub>)?
   * The remainder of the hour you’ll likely be in deep-sleep (t<sub>sleep</sub>).

3. **Compute the average current**
   For each state *i*, let I<sub>i</sub> be its current in mA, t<sub>i</sub> in seconds. Over one cycle period T (e.g. 3600 s):

   $$
   I_{\rm avg}
   = \frac{1}{T}\sum_i I_i \,t_i
   = \sum_i I_i \,\frac{t_i}{T}
   $$

   (Units: mA)

4. **Convert to battery life**
   If your battery has capacity *C* (mAh):

   $$
   \text{Life (h)} \;=\;\frac{C}{I_{\rm avg}}
   $$

5. **Example** (1 h cycle, 3000 mAh cell):

   | State                | I (mA) | t (s) | Contribution $I\times t/3600$ (mAh) |
   | -------------------- | -----: | ----: | ----------------------------------: |
   | LoRa Tx (60 × 0.5 s) |    120 |    30 |                                1.00 |
   | BLE scan (5 s)       |     95 |     5 |                                0.13 |
   | Wi-Fi Tx (60 s)      |    180 |    60 |                                3.00 |
   | Deep-sleep           |   0.15 |  3505 |                                0.15 |
   | **Total per hour**   |        |       |                        **4.28 mAh** |

   $$
   I_{\rm avg}=4.28\text{ mA}
   ,\quad
   \text{Life} = \frac{3000\text{ mAh}}{4.28\text{ mA}}\approx700\text{ h}\approx29\text{ days}.
   $$

6. **Refinements & real-world factors**

   * **Regulator efficiency** (DC-DC vs LDO)
   * **Battery derating** at high/low temperatures
   * **Voltage drop** below regulator cutoff
   * **Dynamic power** in the ESP32 (CPU frequency, peripherals)

By plugging in *your* numbers for how often and how long you transmit or listen on LoRa, BLE and Wi-Fi, you’ll get a realistic battery-life estimate for your Heltec mesh node.

[1]: https://www.nicerf.com/lora-module/long-range-lora-module-lora1276-c1.html?utm_source=chatgpt.com "LoRa1276-C1 : SX1276 868MHz 100mW CE-RED Certified LoRa ..."
