# RetroLink Project Context

## Who I Am
I'm Masa, a CS student at UC Irvine. I'm building RetroLink as a portfolio project. I am a Computer Science student who wants to learn what I'm doing in my code. Make sure that all instructions are oriented for me using STM32CubeIDE, not STM32CubeMX. Be concise and direct.

## What RetroLink Is
A custom ECU breakout/diagnostic system for a 1995 Toyota Pickup with a 3VZ-E V6 engine. The 3VZ-E has **no factory CAN bus** — RetroLink creates one. It reads analog sensor signals (coolant temp, throttle position) through a signal conditioning circuit, converts them with the STM32's ADC, transmits the data over a CAN bus via a TJA1050 transceiver, and displays live readings on an SSD1306 OLED screen.

**GitHub repo:** github.com/MasaCTC/retrolink-ecu

## Hardware
- **MCU:** STM32F446RE Nucleo-64 board
- **Signal conditioning:** LM358 op-amp voltage follower + voltage divider (5V→3.3V safe). Note: LM358 is not rail-to-rail, saturates at ~3.8V with 5V supply → divider output maxes at ~2.59V. MCP6002 is a future upgrade.
- **CAN transceiver:** TJA1050 module — translates STM32's 3.3V single-ended TX/RX to differential CAN_H/CAN_L
- **Display:** SSD1306 0.96" 128x64 I2C OLED (UCTRONICS UCT-602602), address 0x3C
- **Sensors (for bench testing):** Currently using voltage sources / potentiometers on PA0 and PA1

## Pin Assignments
| Function | Pin | Notes |
|----------|-----|-------|
| ADC CH0 (CLT) | PA0 | Analog input |
| ADC CH1 (TPS) | PA1 | Analog input |
| CAN1_RX | PB8 | AF9, needs GPIO_PULLUP (critical fix) |
| CAN1_TX | PB9 | AF9 |
| I2C1_SCL | PB6 | AF4, open-drain |
| I2C1_SDA | PB7 | AF4, open-drain |
| USART2_TX | PA2 | Serial debug output |
| USART2_RX | PA3 | Serial debug input |
| LD2 (LED) | PA5 | GPIO output, toggles on CAN RX |

## Clock Configuration
- HSI 16 MHz, no PLL, APB1 divider = 1
- CAN baud rate: 16MHz / 2 / (1+13+2) = 500 kbps (Prescaler=2, BS1=13, BS2=2, SJW=1)
- I2C1: 100 kHz standard mode

## CubeMX Peripherals Configured
- ADC1 (Channel 0, Channel 1, single conversion, software trigger)
- CAN1 (Normal mode, 500kbps, AutoRetransmission=DISABLE, CAN1_RX0 interrupt enabled)
- I2C1 (100kHz, 7-bit addressing, on PB6/PB7)
- USART2 (115200 baud, 8N1)
- FreeRTOS CMSIS_V2 with 3 tasks

## FreeRTOS Tasks
1. **sensorTask** (AboveNormal priority, 512B stack): Reads PA0 and PA1 via ADC polling at 10 Hz, stores raw counts in `adc_raw[2]` and voltage in `sensor_voltage[2]`.
2. **canTask** (Normal priority, 512B stack): Packs both ADC values into a 4-byte CAN frame (ID 0x100, big-endian) and transmits at 5 Hz.
3. **displayTask** (BelowNormal priority, 1024B stack): Initializes SSD1306 OLED, then at 2 Hz: clears framebuffer, draws "RetroLink" title (11x18 font), divider line, CLT and TPS voltages (7x10 font), pushes to display. Also outputs to UART for serial debugging.

## Project File Structure
```
RetroLink/Core/
├── Inc/
│   ├── main.h
│   ├── ssd1306.h          — OLED driver API (framebuffer, draw functions)
│   └── ssd1306_fonts.h    — Font type header
├── Src/
│   ├── main.c             — All user code (tasks, CAN filter/start, GPIO init)
│   ├── ssd1306.c          — OLED driver (I2C commands, framebuffer, text rendering)
│   ├── ssd1306_fonts.c    — Font bitmap data (7x10 and 11x18)
│   ├── stm32f4xx_hal_msp.c — Peripheral GPIO/clock init (CubeMX-generated)
│   └── stm32f4xx_it.c     — Interrupt handlers (CAN1_RX0_IRQHandler)
```

## Key Technical Details & Past Bugs

**PB8 Pull-Up Fix (critical):** The STM32 CAN peripheral samples the physical CAN_RX pin (PB8) during init-to-normal transition. Without GPIO_PULLUP, the pin floats low (dominant), the peripheral never sees 11 recessive bits, and HAL_CAN_Start times out. Fixed by reconfiguring PB8 with GPIO_PULLUP in USER CODE BEGIN 2, before HAL_CAN_Start.

**CAN ACK Errors (expected):** With only one node on the bus, no device sends ACK. TEC climbs to 128, enters error passive mode. ESR reads 0x00800033 (TEC=128, LEC=ACK error). This is normal — AutoRetransmission=DISABLE (NART) means the controller sends once and moves on. These errors will disappear when a second node (e.g., OBDII reader) is connected.

**LD2 Toggle:** PA5 is manually configured as GPIO output (CubeMX only enabled clocks). The CAN RX callback toggles it on receipt of ID 0x100.

**SSD1306 Driver:** Custom driver based on afiskon/stm32-ssd1306. Uses framebuffer pattern — 1024-byte RAM buffer mirrors the 128x64 display. Drawing functions modify buffer, UpdateScreen() sends all 8 pages over I2C. Font data stored as uint16_t rows (MSB = leftmost pixel).

**Serial Monitor:** Use Mac Terminal: `screen /dev/cu.usbmodem1103 115200`. Must stop CubeIDE debug session first or port will be busy.

## Current main.c
The full source is in the repo. Key sections:
- USER CODE BEGIN 2: PB8 pull-up fix, CAN filter config (accept all), HAL_CAN_Start, activate RX notification
- USER CODE BEGIN 4: CAN RX callback (toggles LD2 on ID 0x100)
- USER CODE BEGIN MX_GPIO_Init_2: PA5 as GPIO output
- StartSensorTask: ADC polling both channels at 10 Hz
- StartCanTask: CAN TX of sensor data at 5 Hz
- StartDisplayTask: OLED init + live sensor voltage display at 2 Hz + UART debug

## Completed Days
- **Day 1:** CubeMX project setup, FreeRTOS tasks, ADC reading
- **Day 2:** Signal conditioning circuit on breadboard, CAN loopback testing
- **Day 3:** CAN normal mode with TJA1050 transceiver, extensive debugging (PB8 pull-up discovery)
- **Day 4:** SSD1306 OLED driver library, live sensor display on screen

## Remaining Days
- **Day 5:** Physical build cleanup + portfolio photos/video
- **Day 6:** Code cleanup + GitHub polish (README, comments, documentation)
- **Day 7:** Vehicle integration test (connect to actual truck sensors)
