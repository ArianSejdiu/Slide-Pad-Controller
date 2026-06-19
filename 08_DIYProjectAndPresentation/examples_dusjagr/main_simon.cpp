#include <Arduino.h>

// Pin configurations
const int buttonPins[] = {12, 13, 11, 10}; // Connected to GND via buttons (needs pullup)
const int ledPins[] = {6, 5, 22, 21};       // LEDs corresponding to buttons
const int numElements = 4;
const int groundPin = 9;                   // GND helper pin
const int audioPin = A0;                   // Analog output A0 for audio/sound

// Game state definition
enum GameState {
  STATE_START,
  STATE_PLAY_SEQUENCE,
  STATE_GET_INPUT,
  STATE_CORRECT_INPUT,
  STATE_GAME_OVER
};

GameState currentState = STATE_START;

// Game logic variables
int sequence[100];
int sequenceLength = 0;
int playerIndex = 0;

// Simon sound frequencies
const int buttonTones[] = {310, 252, 209, 415}; // Frequencies for LED index 0, 1, 2, 3
const int gameOverTone = 42;                    // Low buzz frequency for game over

// Timing constants
unsigned long stateTimer = 0;
unsigned long stepTimer = 0;
int currentStepIndex = 0;
bool stepActive = false;

// Debouncing variables
bool buttonState[numElements] = {HIGH, HIGH, HIGH, HIGH}; // HIGH = unpressed, LOW = pressed
bool lastButtonState[numElements] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceTime[numElements] = {0, 0, 0, 0};
const unsigned long debounceDelay = 40; // 40ms stability required

// Update debounced button states
void updateButtons(unsigned long currentMillis) {
  for (int i = 0; i < numElements; i++) {
    bool reading = digitalRead(buttonPins[i]);
    
    // If the input changed (noise or actual press)
    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = currentMillis;
    }
    
    // Check if the pin state has remained stable longer than debounce window
    if ((currentMillis - lastDebounceTime[i]) > debounceDelay) {
      buttonState[i] = reading;
    }
    
    lastButtonState[i] = reading;
  }
}

// Helper to play tone and turn on LED
void activateLedAndTone(int index) {
  if (index >= 0 && index < numElements) {
    analogWrite(ledPins[index], 255);
    tone(audioPin, buttonTones[index]);
  }
}

// Helper to turn off LED and stop tone
void deactivateLedAndTone(int index) {
  if (index >= 0 && index < numElements) {
    analogWrite(ledPins[index], 0);
  }
  noTone(audioPin);
}

// Helper to turn off all LEDs
void allLedsOff() {
  for (int i = 0; i < numElements; i++) {
    analogWrite(ledPins[i], 0);
  }
  noTone(audioPin);
}

void setup() {
  Serial.begin(115200);

  // Set Ground pin helper to LOW
  pinMode(groundPin, OUTPUT);
  digitalWrite(groundPin, LOW);

  // Initialize button and LED pins
  for (int i = 0; i < numElements; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    pinMode(ledPins[i], OUTPUT);
    analogWrite(ledPins[i], 0);
  }

  // Audio pin setup
  pinMode(audioPin, OUTPUT);

  Serial.println("--- SIMON SAYS GAME START ---");
}

void loop() {
  unsigned long currentMillis = millis();

  // Continually update debounced state of all buttons
  updateButtons(currentMillis);

  switch (currentState) {
    case STATE_START: {
      // Rotating idle animation
      static int lastLed = -1;
      if (currentMillis - stateTimer >= 150) {
        stateTimer = currentMillis;
        if (lastLed >= 0) {
          analogWrite(ledPins[lastLed], 0);
        }
        lastLed = (lastLed + 1) % numElements;
        analogWrite(ledPins[lastLed], 128); // Mid brightness for idle
      }

      // Check if any button is pressed to start the game
      for (int i = 0; i < numElements; i++) {
        if (buttonState[i] == LOW) {
          // Initialize random seed based on micros() at start button press
          randomSeed(micros());
          
          allLedsOff();
          Serial.println("Game started! Ready...");
          
          // Flash all LEDs and play a start chirp
          for (int j = 0; j < numElements; j++) {
            analogWrite(ledPins[j], 255);
          }
          tone(audioPin, 600, 100);
          delay(100);
          tone(audioPin, 800, 150);
          delay(150);
          allLedsOff();
          delay(600);

          sequenceLength = 0;
          
          // Add first step
          sequence[sequenceLength++] = random(0, numElements);
          
          currentState = STATE_PLAY_SEQUENCE;
          currentStepIndex = 0;
          stepActive = false;
          stepTimer = currentMillis;
          break;
        }
      }
      break;
    }

    case STATE_PLAY_SEQUENCE: {
      int playbackSpeed = max(150, 400 - (sequenceLength * 15)); // Get faster as level increases
      int offDuration = playbackSpeed / 2;

      if (!stepActive) {
        // Wait between steps
        if (currentMillis - stepTimer >= offDuration) {
          int ledIndex = sequence[currentStepIndex];
          activateLedAndTone(ledIndex);
          
          stepActive = true;
          stepTimer = currentMillis;
          
          Serial.print("Step ");
          Serial.print(currentStepIndex + 1);
          Serial.print("/");
          Serial.print(sequenceLength);
          Serial.print(": LED ");
          Serial.println(ledIndex);
        }
      } else {
        // Wait for current step duration to finish
        if (currentMillis - stepTimer >= playbackSpeed) {
          int ledIndex = sequence[currentStepIndex];
          deactivateLedAndTone(ledIndex);
          
          stepActive = false;
          stepTimer = currentMillis;
          currentStepIndex++;

          if (currentStepIndex >= sequenceLength) {
            // Finished playing the sequence. Transition to player input state.
            playerIndex = 0;
            currentState = STATE_GET_INPUT;
            Serial.println("Your turn!");
          }
        }
      }
      break;
    }

    case STATE_GET_INPUT: {
      // Loop through all buttons to detect player input
      static int pressedButton = -1;
      
      if (pressedButton == -1) {
        // Look for a new stable press
        for (int i = 0; i < numElements; i++) {
          if (buttonState[i] == LOW) {
            pressedButton = i;
            activateLedAndTone(pressedButton);
            Serial.print("Pressed button: ");
            Serial.println(pressedButton);
            break;
          }
        }
      } else {
        // Wait for the button to be stably released
        if (buttonState[pressedButton] == HIGH) {
          deactivateLedAndTone(pressedButton);
          
          // Verify player input
          if (pressedButton == sequence[playerIndex]) {
            playerIndex++;
            pressedButton = -1;

            if (playerIndex >= sequenceLength) {
              // Entire sequence replicated correctly!
              currentState = STATE_CORRECT_INPUT;
              stateTimer = currentMillis;
            }
          } else {
            // Mistake! Game over.
            pressedButton = -1;
            currentState = STATE_GAME_OVER;
            stateTimer = currentMillis;
          }
        }
      }
      break;
    }

    case STATE_CORRECT_INPUT: {
      // Short delay before adding new step and playing sequence again
      if (currentMillis - stateTimer >= 600) {
        Serial.println("Correct! Advancing level...");
        
        // Success chirp
        tone(audioPin, 1000, 80);
        delay(80);
        tone(audioPin, 1200, 120);
        delay(120);
        
        // Add new step
        if (sequenceLength < 100) {
          sequence[sequenceLength++] = random(0, numElements);
        }
        
        currentState = STATE_PLAY_SEQUENCE;
        currentStepIndex = 0;
        stepActive = false;
        stepTimer = currentMillis;
      }
      break;
    }

    case STATE_GAME_OVER: {
      Serial.println("Game Over!");
      
      // Play low buzzer sound on A0
      tone(audioPin, gameOverTone, 1000);

      // Flash all LEDs in unison
      for (int f = 0; f < 5; f++) {
        for (int i = 0; i < numElements; i++) {
          analogWrite(ledPins[i], 255);
        }
        delay(100);
        for (int i = 0; i < numElements; i++) {
          analogWrite(ledPins[i], 0);
        }
        delay(100);
      }
      
      noTone(audioPin);
      delay(500);

      currentState = STATE_START;
      stateTimer = currentMillis;
      break;
    }
  }
}
