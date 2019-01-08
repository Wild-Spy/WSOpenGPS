 
### Setup
Add the following to Additional Boards Manager URLs
```
https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
```

In the board manager search for `Adafruit SAMD Boards` and install it.

Copy libraries into your library directory.

Set board to Adafruit Feather M0.

Current measurements:
Running: 22mA avg
Sleep: 620uA

- So there's 0.5mA going into the sck pin of the serial flash.  Try setting the SCK pin to an input when we're asleep?  Or setting it to low voltage when asleep?
- 8/1/2019 - Still 600uA, disabled the SPI and we still get lots of draw.  What could it be from?  I tried completely disconnecting the flash and also the GPS in sleep mode and we still have 600uA so it must be the RF95 or something on the feather..