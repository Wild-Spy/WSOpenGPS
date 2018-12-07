 
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
Sleep: 1mA !! :(
So there's 0.5mA going into the sck pin of the serial flash.  Try setting the SCK pin to an input when we're asleep?  Or setting it to low voltage when asleep?