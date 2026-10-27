// Pines y constantes de la placa LCDWiki ES3C35P (3.5" ESP32-S3, ST77922 QSPI).
// Fuente de los pines: especificación oficial (lcdwiki.com, CR2025-MI6872) y las
// definiciones probadas de esta misma placa en xiaozhi-esp32 y NerdMiner.
#pragma once

#define VERSION "1.1.0"

// Pantalla ST77922 por QSPI (su reset va al pin EN del ESP32, no hay GPIO)
#define LCD_CS 10
#define LCD_SCK 12
#define LCD_D0 11
#define LCD_D1 13
#define LCD_D2 14
#define LCD_D3 9
#define LCD_BL 41  // luz de fondo, nivel alto = encendida

// Táctil (está dentro del mismo ST77922) por I2C, bus compartido con el códec de audio
#define I2C_SDA 38
#define I2C_SCL 39
#define TOUCH_RST 48
#define TOUCH_ADDR 0x55

// Audio: códec ES8311 + amplificador
#define I2S_MCLK 17
#define I2S_BCLK 18
#define I2S_WS 21
#define I2S_DOUT 15
#define AMP_EN 1  // nivel bajo = amplificador encendido
#define ES8311_ADDR 0x18

#define LED_RGB 40
#define BOTON_BOOT 0

// Lector de huella AS608 en el conector "UART" de la placa:
//   RXD de la placa (GPIO44) <- TX del lector
//   TXD de la placa (GPIO43) -> RX del lector
// El lector se alimenta con 3,3 V del conector de expansión (no con el 5V del UART).
// No se usan IO45/IO46: deciden cómo arranca el ESP32 y el lector podría impedir el arranque.
#define HUELLA_RX 44
#define HUELLA_TX 43
#define HUELLA_BAUD 57600

// Zona horaria POSIX (por defecto Chile continental). Ej.: "CET-1CEST,M3.5.0,M10.5.0/3" (Madrid),
// "<-03>3" (Buenos Aires), "CST6CDT,M4.1.0,M10.5.0" (México): https://github.com/nayarsystems/posix_tz_db
#define ZONA_HORARIA "<-04>4<-03>,M9.1.6/24,M4.1.6/24"

#define PIN_INICIAL "1234"  // cámbiala en Ajustes → Cambiar clave al instalar
#define SEG_MARCA_REPETIDA 120  // misma persona dentro de 2 min = no se cuenta de nuevo

// Servidor (backend) donde quedan las marcas y están las personas: ver docs/API.md y server/.
// La dirección y la llave (RELOJ_TOKEN) se cargan desde http://reloj.local y quedan en la NVS.
#define PANEL_URL_INICIAL ""

// Color de acento de la pantalla (RGB). El resto es negro, blanco y grises.
#define COLOR_ACENTO 0x6C5CE7
