# ESPHome Smartmeter Reader for Wiener Netze

This is a custom component for ESPHome to integrate a smartmeter provided by Wiener Netze into Home Assistant via the infrared "Kundenschnittstelle". It gives power and energy readings, both positive and negative for active as well as reactive power.

## Device Support

**Supported and tested**
- Siemens IM150
- Siemens IM151
- Siemens IM350
- Siemens IM351
- Landis + Gyr E450
- Iskraemeco AM550-TD0

**Possibly supported but untested**
- Landis + Gyr E570
- Iskraemeco AM550-ED0

If you have one of the untested devices (or any other devices supplied by Wiener Netze) and are willing to help me test this component, please open an [issue](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/new).

## Installation and usage

Add the following code to your yaml file.
Dont forget to add your encryption key from the smartmeter website otherwise it will not work.

```yaml
external_components:
  - source: github://bernikr/esphome-wienernetze-smartmeter@v1.1.1

logger:
  baud_rate: 0

uart:
  tx_pin: 1
  rx_pin: 3
  baud_rate: 9600

wienernetze:
  key: <enter your key here>

sensor:
  - platform: wienernetze
    active_energy_pos:
      name: Energy
    active_power_pos:
      name: Power
    active_energy_neg:
      name: Negative Energy
    active_power_neg:
      name: Negative Power
    reactive_energy_pos:
      name: Reactive Energy
    reactive_power_pos:
      name: Reactive Power
    reactive_energy_neg:
      name: Reactive Negative Energy
    reactive_power_neg:
      name: Reactive Negative Power

text_sensor:
  - platform: wienernetze
    active_energy_pos:
      name: Energy Raw
    active_energy_neg:
      name: Negative Energy Raw
    reactive_energy_pos:
      name: Reactive Energy Raw
    reactive_energy_neg:
      name: Reactive Negative Energy Raw
```

For every sensor normal esphome sensor configs can be used to set name, id, icon, etc. or add filters and so on.

**Warning**  
All energy sensors roll over every 1000 kWh and start again from 0 due to precision issues of ESPHome.
(Sensors are always 32bit floats. If the meter is too high the sensor cant update every Wh anymore.)  
This is not a problem when using the `active_energy_pos` sensor for the energy dashboard in Home Assistant as it is set to `total_increasing` and therefore Home Assistant knows that a drop from 1000 to 0 is a reset of the counter and not a negataive consumption of 1000kWh.

As an alternative you can use the text_sensors which always result in the full counter of the meter (in Wh not kWh). To use them as a numeric sensor in Home Assistant you need to create a Template Sensor that takes the sensor and converts it into a numeric sensor:

```jinja
{{ states("sensor.energy_raw") | float / 1000 }}
```

## Tested Hardware
This component shoud work on all ESP8266 and ESP32 microcontrollers with an IR read-head attached to them. I used a ready made read-write head with an ESP01s built in that I got for 30€ on [ebay](https://www.ebay.at/sch/i.html?_nkw=lesekopf+tasmota). If they are advertised to be used with Tasmota, they should work.

## Thanks
The following resources were much help in the development of this component:
- https://github.com/aldadic/esp32-smartmeter-reader
- https://github.com/aburgr/smartmeter-reader
- https://github.com/DomiStyle/esphome-dlms-meter
- https://www.lteforum.at/mobilfunk/wiener-netze-smart-meter-auslesen.16222/
- https://gist.github.com/pocki80/941fa090a8d6269a9b3b68c195f8750f

Thanks to the following people for helping test this component on different smartmeters:
- IM151: @cyranosaurus
- IM350: @coolduke666
- IM351: @elpollodiablo
- E450: @s00500
- AM550-TD0: @paul-leitner
