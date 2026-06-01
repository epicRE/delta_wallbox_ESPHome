# Delta AC MAX Basic Wallbox communication via ESPHome

## ! WARNING !
**This is an experimental system, use at your own risk. I can't be held liable for any and all issues you might face afterward using this code. This includes but not limited to damages to EVSE/EV/buildings etc.**

## Introduction
This repo details what is needed for you to operate a Delta AC MAX Basic with the ESPHome component implementation (and ESPHome intergration).

Delta AC MAX Basic is a simple EV wallbox that is provided by Delta based in Taiwan - [https://landing.deltaww.com/en-US/products/EV-Charging/AC-MAX](https://landing.deltaww.com/en-US/products/EV-Charging/AC-MAX) 

## How to test 
The following assumes the ESP32 device is connected to this Linux system and it is idetified as ttyUSB0 (use `dmesg` to see what device it was assigned).

In a new folder do the following (you will need a bit of space for all the packages needed by esphome. (You will also need Python 3.11 and above)

```bash
python -m pip install --upgrade pip
python3 -m venv .venv
source .venv/bin/activate
git clone https://github.com/esphome/esphome
cd esphome/components
git clone https://github.com/epicRE/delta_wallbox_ESPHome delta_wallbox
cd esphome
python -m pip install -e ./esphome-dev
esphome config delta-wallbox.yaml
esphome compile delta-wallbox.yaml
esphome run delta-wallbox.yaml --device /dev/ttyUSB0
```
Get the IP address and now add the device to your Home Assisstant ESPHome intergration. 

## Details about Charger Raw State (numerical values) (not confirmed, only observed) 

| Charger Raw State (dec) |Charger Raw State (hex) | Description     |
| ------------- | ------------- | ------------- |
| 161 |`0xA1` | Unplugged/No Session |
| 177 |`0xB1` | Can start charging |
| 178 |`0xB2` | Can stop charging  |
| 193 |`0xC1` | Can start charging |
| 194 |`0xC2` | Charging |
| 15  |`0xF`  | Can start charging |

