# Bytelipi Otto

An Otto-style **humanoid robot** built on the **ESP32-C3 Super Mini**. It has an animated OLED face,
6 servos, a buzzer for sound effects, and its own Wi-Fi hotspot with a web dashboard that you can open on
any phone or laptop. No app and no internet are needed.

By **Bytelipi Innovation Labs**.

---

## Features

- **Walk, turn and dance** from the dashboard: Forward, Backward, Left, Right, two Moonwalks and a full Dance routine.
- **12 extra Otto moves:** Swing, Tiptoe swing, Up-down, Jitter, Crusaito (and back), Flapping (and back), Ascending turn, Wave L, Wave R.
- **21 animated faces** on a 128x128 OLED. Each move changes the face (for example Forward = happy, Left = thinking).
- **Arms rest down at the sides.** One button raises both arms.
- **Buzzer sounds for every button:** a different tune for each move, 15 hold-to-play sound effects, and a short sound for each of the 21 expressions.
- **Hold to move, let go to stop.** The robot also stops by itself if the phone loses the connection (about 1.2 seconds).
- **Speed slider**, **Servos to 90** button for assembly, sound on/off, and an auto-cycle for expressions.
- Movements use the same sine-wave method as the original Otto Humanoid library, so they look like a real Otto.

## Hardware

| Part | Notes |
|---|---|
| ESP32-C3 Super Mini | The brain. Runs the Wi-Fi hotspot and the web dashboard. |
| Waveshare 1.5 inch OLED Module | 128x128, SSD1327 controller, SPI mode. |
| 6x micro servos | SG90 works. **MG90S (metal gear) is recommended** because plastic gears wear out. |
| Buzzer module, 3 pins | Passive buzzer for different pitches. An active buzzer gives beats only. |
| Servo power supply | 5 to 6 V, at least 2 A (for example 2x 18650 cells with a 5 V regulator, or 4x AA). |
| Capacitor | 470 to 1000 µF, rated 10 V or higher (16 V is common). |
| Otto Humanoid body | 3D-printed Otto Humanoid parts (legs, feet, arms, body). |
| Jumper wires | For the connections below. |

## Wiring

### OLED (SPI)

| OLED pin | ESP32-C3 Super Mini |
|---|---|
| VCC | 3V3 |
| GND | GND |
| DIN | GPIO 6 |
| CLK | GPIO 4 |
| CS | GPIO 7 |
| DC | GPIO 3 |
| RST | GPIO 10 |

The module must be in 4-wire **SPI mode** (this is the default; check the resistor settings on the back).

### Servos (signal wires)

| Servo | ESP32-C3 Super Mini |
|---|---|
| Left leg (hip) | GPIO 0 |
| Right leg (hip) | GPIO 1 |
| Left foot | GPIO 2 |
| Right foot | GPIO 5 |
| Left arm | GPIO 20 |
| Right arm | GPIO 21 |

Servo order in the code (and in the original Otto library): left leg, right leg, left foot, right foot, left arm, right arm.

### Buzzer module

| Buzzer pin | ESP32-C3 Super Mini |
|---|---|
| VCC | 3V3 |
| GND | GND |
| I/O (signal) | GPIO 8 |

### Power (important)

```
Servo supply (+) ──┬── all 6 servo red wires
                   └── capacitor (+) long leg
Servo supply (-) ──┬── all 6 servo brown/black wires
                   ├── capacitor (-) striped leg
                   └── ESP32 GND   (common ground, required)
ESP32 ── powered from USB or its own 5V input (not from the servo red wires)
```

- **Never power the servos from the ESP32 3V3 or 5V pins.** Six servos draw too much current, and the Wi-Fi will drop or the board will reset.
- The servo ground and the ESP32 ground must be joined.
- Do not connect USB power and the 5V pin at the same time.

## Software setup

1. Install the **Arduino IDE 2.x**.
2. In Boards Manager, install **esp32 by Espressif**, version **3.0 or newer** (the code uses the 3.x PWM functions).
3. Install the **U8g2** library from the Library Manager.
4. Install the **AnimatedFaces** library from this repository: copy `libraries/AnimatedFaces` into your `Documents/Arduino/libraries` folder (or zip that folder and use Sketch → Include Library → Add .ZIP Library).
5. Open `Otto_Humanoid_WebAP/Otto_Humanoid_WebAP.ino`.
6. Select the board **ESP32C3 Dev Module** and set **USB CDC On Boot = Enabled**.
7. Upload. If the upload fails, hold the **BOOT** button while you plug in the USB cable.

You will see a harmless message "Multiple libraries were found for WiFi.h" if you have an extra WiFi library installed. The IDE uses the correct ESP32 one.

## First-time setup (before the robot walks)

1. Upload the sketch and connect to the Wi-Fi (see below).
2. Open the dashboard and tap **Servos to 90**. All servos move to their centre position.
3. **Fit the servo horns and attach the legs, feet and arms while the servos are at 90°.**
4. Hold the robot in your hand (feet off the ground) and try each move.
5. Adjust the settings at the top of the sketch:
   - `TRIM`: adds a few degrees to a servo so it is centred.
   - `INVERT`: set a servo to `true` if it moves the wrong way.
   - The two arm servos are reversed by default (`INVERT` is `true` for the last two entries). If the arms go up when they should go down, change both back to `false`.
6. Upload again, then test on the ground.

## Using the robot

1. Power the robot.
2. On start-up the OLED shows the Wi-Fi name, password and address and **keeps them on the screen until a phone or laptop joins the hotspot**. Then it shows "Connected!" with the address for 2 seconds (with a short chime), and then the neutral face.
3. On your phone join the Wi-Fi:
   - **Name:** `ByteLipi_Otto_OLED`
   - **Password:** `bytelipi`
4. Open **http://192.168.4.1** in the browser (use `http://`, not `https://`). If your phone says "no internet", choose to stay connected and turn mobile data off.

### Dashboard controls

| Control | What it does |
|---|---|
| Forward / Backward / Left / Right | Walk or turn. Hold to move, release to stop. |
| STOP | Stops everything. |
| Moonwalk ◀ ▶ | Otto moonwalk to the left or right (hold). |
| DANCE | Runs the dance routine with music until you press STOP. |
| Arms: down / up | Raises or lowers both arms. |
| Sound: on / off | Mutes the buzzer. |
| More moves (hold) | Swing, Tiptoe swing, Up-down, Jitter, Crusaito, Crusaito back, Flapping, Flapping back, Ascending turn, Wave L, Wave R. |
| Sound effects (hold) | Cry, Laugh, Victory, Alert, Success, Hello, Wow, Yawn, Love, Angry, Robot talk, Laser, Power up, Power down, Doorbell. |
| Speed slider | 1 (slow) to 5 (fast). Level 3 is the original Otto timing. |
| Servos to 90 | Holds every servo at exactly 90° for assembly. |
| Expressions | Tap a face to show it on the OLED (with its own short sound). |
| Auto cycle | Changes the expression every 4 seconds. |

### Faces and sounds

- Each move button changes the face while it runs and returns to **neutral** when you let go.
- A tune plays while you hold a move or sound-effect button and stops when you let go. A very short tap gives a very short sound.
- The 21 expressions are: neutral, happy, laughing, funny, sad, angry, crying, loving, embarrassed, surprised, shocked, thinking, winking, cool, relaxed, delicious, kissy, confident, sleepy, silly, confused.

## Configuration (top of `Otto_Humanoid_WebAP.ino`)

| Setting | Meaning |
|---|---|
| `AP_SSID`, `AP_PASS` | Wi-Fi name and password (password must be 8+ characters). |
| `WAIT_FOR_CONNECTION` | `true` keeps the Wi-Fi info on the screen until a device connects. `false` shows it for `INFO_SCREEN_MS` only. |
| `INFO_SCREEN_MS` | Info screen time at start, used only when `WAIT_FOR_CONNECTION` is `false`. |
| `CONNECTED_SCREEN_MS` | How long the "Connected!" screen shows after a device joins. |
| `FAILSAFE_MS` | The robot stops if the page sends no command for this long. |
| `SERVO_PIN` | GPIO pin of each servo. |
| `TRIM`, `INVERT` | Servo calibration and direction. Both arms are reversed by default. |
| `SPEED_SCALE` | Period multiplier for the speed slider levels. |
| `ARM_DOWN`, `ARM_UP`, `ARM_SPEED` | Arm resting angles and how fast they move. |
| `PIN_BUZZER` | Buzzer signal pin. |
| `BUZZER_ACTIVE` | `true` for an active buzzer (rhythm only), `false` for a passive buzzer. |
| `BUZZER_ACTIVE_LOW` | Set `true` only if the buzzer sounds while idle. |
| `TUNE_TIME_SCALE` | Length of all sounds. `0.5` is shorter, `1.0` is longer. |

## HTTP API

The dashboard uses simple GET requests, so you can also control the robot from your own app or script.

| Request | Description |
|---|---|
| `/` | The dashboard page. |
| `/state` | JSON: `{"e":"happy","auto":false,"arms":false,"snd":true}` |
| `/set?e=<expression>` | Show an expression. |
| `/auto?on=1` or `0` | Auto-cycle expressions. |
| `/move?d=<move>` | Start a move. Repeat the request at least every 1 second to keep moving. |
| `/move?d=stop` | Stop (add `&q=1` for no sound). |
| `/dance` | Start the dance routine. |
| `/arms?up=1` or `0` | Raise or lower the arms. |
| `/fx?n=<effect>` | Play a sound effect. Repeat to keep it playing, and use `n=stop` to stop. |
| `/sound?on=1` or `0` | Sound on or off. |
| `/speed?v=1..5` | Set the speed. |
| `/center` | Hold all servos at 90°. |

Moves: `forward`, `backward`, `left`, `right`, `moonl`, `moonr`, `swing`, `updown`, `jitter`, `crusaito`, `crusaitob`, `flap`, `flapb`, `tiptoe`, `ascend`, `wavel`, `waver`.
Effects: `cry`, `laugh`, `victory`, `alert`, `success`, `hello`, `wow`, `yawn`, `love`, `angry`, `robot`, `laser`, `powerup`, `powerdown`, `doorbell`.

## Troubleshooting

| Problem | What to try |
|---|---|
| Wi-Fi name does not appear | Open the Serial Monitor at 115200 baud and check for "AP started". Move the phone close to the board. Turn the phone Wi-Fi off and on. Try another device. |
| Page does not open | Use `http://192.168.4.1`, not `https://`. Turn mobile data off. |
| Wi-Fi drops or the board resets when servos move | Servo power problem. Use a separate 5 to 6 V supply of 2 A or more, a common ground, and the capacitor. |
| A leg or arm moves the wrong way | Set that servo to `true` in `INVERT`. |
| Arms go up instead of down | Flip `INVERT` for both arm servos (the last two values). If only one arm is wrong, flip only that one. |
| A servo is not centred | Change its value in `TRIM` and upload again. |
| Robot does not start with the left foot servo connected | GPIO 2 is a boot pin on the ESP32-C3. Move that servo to another free pin and change `SERVO_PIN`. |
| OLED is blank | Check the wiring, make sure the module is in SPI mode, and check the 3.3 V supply. |
| Buzzer sounds all the time | Set `BUZZER_ACTIVE_LOW` to `true`. |
| Every sound is the same pitch | You probably have an active buzzer. Set `BUZZER_ACTIVE` to `true`, or use a passive buzzer. |
| Very low sounds (such as Angry) are quiet | Small buzzers play low notes quietly. Raise the frequencies in `N_ANGRY`. |
| Compile error about `Act` | Use the latest version of the sketch. |

## Limits and ideas

- All 12 usable GPIO pins are in use, so adding sensors needs a different pin plan (for example a PCA9685 servo driver board on I2C).
- The buzzer uses a software timer, because all 6 PWM channels of the ESP32-C3 are used by the servos. The sound can be slightly rough while the Wi-Fi is busy.
- The walking values come from the Otto Humanoid library. Your build may need tuning of `TRIM`, `INVERT` and the speed.
- Ideas: calibration sliders with settings saved in flash, over-the-air (OTA) updates, a captive portal so the dashboard opens automatically, an idle and sleep mode, an ultrasonic sensor for obstacle avoidance, and battery monitoring.

## Credits

- **Otto DIY** and the Otto robot community, and the Otto Humanoid library whose movement parameters are used here.
- **Vishal Soni**, Arduino_Otto_Robot project: [video](https://youtu.be/xBAoARIPTug) and [Instructables guide](https://www.instructables.com/Otto-Robot-Arduino-Robot-Simple-Arduino-Robot/).
- **esp32-oled-animated-faces** (the 21 animated faces), a derivative of the [Otto Emoji GIF Component](https://github.com/txp666/otto-emoji-gif-component) by 小鹏 (txp666), MIT license.
- **U8g2** by olikraus, used for the OLED display.
- Waveshare, for the 1.5 inch OLED module.

## License

Add your chosen license here (see the `LICENSE` file). Third-party parts keep their own licenses, so keep the license files that come with the AnimatedFaces library.
