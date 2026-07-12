#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The parameter -1 means the display does not have a physical reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  // Initialize with the I2C addr 0x3C (commonly used for 128x64 OLEDs)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();      // Clear the hardware buffer
  display.setTextSize(2);      // Set text scale (1-8)
  display.setTextColor(SSD1306_WHITE); // Set text color
  display.setCursor(10, 20);   // Set cursor position (x, y)
  display.println(F("Hello!")); // Write text to buffer
  
  display.display();           // Push the buffer to the actual screen
}

void loop() {
  // Put your main code here, to run repeatedly:
}
