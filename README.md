# OncoSense-MIT-Lincoln-Laboratory
Arduino IDE Code for OncoSense Functionalities

AdaSpecCode was for testing on skin lesions, bumps, scars, and further anomalies alongside moisture. It is developed however still not well-tested. 


MainSkin is the code used in the video, the main code used to detect skin moisture and classifying it as dry, normal, or well-hydrated.

The code sets a threshold for the amount of light reflectance that is required to classify the skin as being moisturized/hydrated and dry or even normal. It uses the Near-Infrared LED as the IR2_LED_PIN and the spectrometer Adafruit_AS7341 to pick up on light reflected back from the skin. The microcontroller at use is QTPy SAMD21.
