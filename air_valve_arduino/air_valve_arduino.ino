// Переключение воздушного клапана модели септика
// с помощью сервопривода по состоянию поплавка.
// Чтобы произошло переключение состояния клапана,
// состояние поплавка должно быть стабильно в течение
// 1 секунды.

#include <Servo.h>

const byte SERVO_CTRL  = 5;       // Сервопривод (PWM)
const byte FLOATER_STATE = 17;     // Состояние поплавка (внутренняя подтяжка)

// Центр серво и отклонение от центра в градусы.
const int SERVO_CENTER = 90;
const int SERVO_OFFSET = 45;

// Два крайних положения заслонки: центр +/- 45 градусов.
const int VALVE_POS_A = SERVO_CENTER - SERVO_OFFSET;  // 45: поплавок разомкнут
const int VALVE_POS_B = SERVO_CENTER + SERVO_OFFSET;  // 135: поплавок замкнут

const unsigned long DEBOUNCE_MS = 1000;    // насколько долго состояние должно быть стабильно
const unsigned long SERVO_MOVE_MS = 800;  // длительность перемещения между крайними позициями

Servo valveServo;

// Состояние движения серво и дребезга поплавка.
int targetPos = VALVE_POS_A;
int currentPos = VALVE_POS_A;
int motionStartPos = VALVE_POS_A;
int floaterPrev = -1;
unsigned long motionStartedAt = 0;
unsigned long stateSince = 0;
bool servoMoving = false;

void setServoTarget(int position, unsigned long now) {
    if (position == targetPos) {
        return;
    }

    targetPos = position;
    motionStartPos = currentPos;
    motionStartedAt = now;
    servoMoving = (motionStartPos != targetPos);
}

void updateServoMotion(unsigned long now) {
    if (!servoMoving) {
        return;
    }

    const unsigned long elapsed = now - motionStartedAt;
    if (elapsed >= SERVO_MOVE_MS) {
        currentPos = targetPos;
        servoMoving = false;
    } else {
        const float progress = (float)elapsed / SERVO_MOVE_MS;
        // Smoothstep задает нулевую скорость в начале и конце перемещения.
        const float easedProgress = progress * progress * (3.0f - 2.0f * progress);
        currentPos = motionStartPos + (int)((targetPos - motionStartPos) * easedProgress);
    }

    valveServo.write(currentPos);
}

void setup() {
    pinMode(FLOATER_STATE, INPUT_PULLUP);  // поплавок замыкает пин на землю

    valveServo.attach(SERVO_CTRL);
    valveServo.write(currentPos);  // начальное положение
}

void loop() {
    const unsigned long now = millis();
    const int floaterNow = digitalRead(FLOATER_STATE);

    updateServoMotion(now);

    if (floaterNow == floaterPrev) {
        // Состояние не меняется: ждём, пока оно продержится DEBOUNCE_MS.
        if (now - stateSince >= DEBOUNCE_MS) {
            const int newTarget = (floaterNow == LOW) ? VALVE_POS_B : VALVE_POS_A;
            setServoTarget(newTarget, now);
        }
    } else {
        stateSince = now;  // состояние изменилось - стартуем отсчёт заново
    }

    floaterPrev = floaterNow;

    delay(10);
}
