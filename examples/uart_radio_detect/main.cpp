#include <Arduino.h>

/**
 * ADVANCED UART Diagnostic Tool
 * This script checks for common wiring issues:
 * 1. Loopback (TX shorted to RX)
 * 2. Baud rate mismatch
 * 3. No connection
 */

#define RADIO_RX 5
#define RADIO_TX 4

// List of common baud rates to test
uint32_t bauds[] = {115200, 57600, 9600};
int currentBaudIdx = 0;

void runTest() {
    uint32_t baud = bauds[currentBaudIdx];
    Serial.printf("\n--- TESTING BAUD RATE: %d ---\n", baud);
    
    Serial2.begin(baud, SERIAL_8N1, RADIO_RX, RADIO_TX);
    delay(500);

    // 1. Loopback Check
    Serial.println("[1/3] Checking for Loopback (TX shorted to RX)...");
    while(Serial2.available()) Serial2.read(); // Clear
    
    String testPattern = "CHECK_123";
    Serial2.print(testPattern);
    delay(100);
    
    String response = "";
    while(Serial2.available()) response += (char)Serial2.read();
    
    if (response == testPattern) {
        Serial.println("  [!!] LOOPBACK DETECTED: Pins 16 & 17 are shorted or tied together.");
        Serial.println("       The radio is not being reached.");
    } else if (response.length() > 0) {
        Serial.printf("  [?] Received unexpected data: [%s]\n", response.c_str());
    } else {
        Serial.println("  [OK] No loopback detected.");
    }

    // 2. AT Command Check
    Serial.println("[2/3] Sending AT Command...");
    Serial2.println("AT");
    delay(500);
    
    bool gotResponse = false;
    Serial.print("  Response: ");
    while(Serial2.available()) {
        gotResponse = true;
        char c = Serial2.read();
        if (c >= 32 && c <= 126) Serial.print(c);
        else Serial.printf("[0x%02X]", c);
    }
    
    if (!gotResponse) {
        Serial.println("  [X] NO RESPONSE from radio.");
    } else {
        Serial.println("\n  [√] RESPONSE RECEIVED!");
    }
}

void setup() {
    Serial.begin(115200);
    while(!Serial);
    delay(1000);
    Serial.println("===============================");
    Serial.println("  UART RADIO TROUBLESHOOTER    ");
    Serial.println("===============================");
    
    runTest();
    
    Serial.println("\n--- DIAGNOSTIC COMPLETE ---");
    Serial.println("Type 'NEXT' to try a different baud rate.");
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "NEXT") {
            currentBaudIdx = (currentBaudIdx + 1) % (sizeof(bauds)/sizeof(bauds[0]));
            runTest();
        } else {
            Serial2.println(cmd);
        }
    }

    if (Serial2.available()) {
        while(Serial2.available()) {
            Serial.write(Serial2.read());
        }
    }
}
