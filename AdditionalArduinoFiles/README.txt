The Due implementation of the default arduino disables the watchdog feature at
startup. Due to ARM limitation, it is not possible to enable a watchdog once it
is disabled. For that reason, the watchdog feature does not work with default
Arduino installation. 

Also some other boards like the Alligator board need their own variants, which can be
copied and installed the same way.

To solve the problem copy the modified version as follows:

The content of boards.txt needs to be appended to <ArduinoInstallPath>/hardware/arduino/sam or for version of Arduino IDE later than 1.5.x, $(HOME)/.arduino15/packages/hardware/arduino/sam/1.6.6

Copy directories arduino_due_repetier, alligator_r1 and alligator_r2 to
<ArduinoInstallPath>/hardware/arduino/sam/variants or for version of Arduino IDE later than 1.5.x, $(HOME)/.arduino15/packages/hardware/arduino/sam/1.6.6/variants

After a restart you have a new board in your list. Use it only if you
compile with watchdog feature enabled (which is the preferred way for increased safety)

------------ License -----------

The files contained here are part of the Arduino package and their
license terms stay valid regardless of the license of the core firmware.  
