# 🏠 Sistema Domótico Inteligente con ESP32

Proyecto final desarrollado utilizando tres microcontroladores ESP32 para implementar un sistema domótico distribuido capaz de controlar iluminación, acceso y monitoreo en tiempo real mediante comunicación I2C, UART y WiFi.

---

## Descripción

El proyecto consiste en una maqueta de casa inteligente diseñada como un Sistema Automatizado de Fachada Inteligente para Asistencia y Accesibilidad, capaz de automatizar distintas funciones relacionadas con iluminación, acceso y monitoreo del entorno.

La arquitectura del sistema se encuentra distribuida en tres módulos ESP32 encargados de diferentes tareas para optimizar el procesamiento y la organización del sistema:

- ESP32 #1 → Control principal y administración de estados
- ESP32 #2 → Manejo de actuadores y sensores
- ESP32 #3 → Interfaz web y conexión WiFi

El sistema permite controlar funciones tanto de manera local como remota mediante una página web alojada en la ESP32.

---

## Características

- Control de LEDs desde botones físicos y página web
- Ajuste de brillo
- Apertura/cierre de puerta con servomotor
- Visualización de estados en pantalla OLED
- Encendido automático de iluminación mediante sensor LDR
- Red WiFi local creada por ESP32
- Comunicación UART entre módulos
- Comunicación I2C entre controlador y actuadores
- Actualización de estados en tiempo real

---

## Arquitectura del sistema

### ESP32 #1 – Control principal
- Administración de estados del sistema
- Comunicación I2C con ESP32 #2
- Comunicación UART con ESP32 #3
- Actualización de pantalla OLED

### ESP32 #2 – Actuadores y sensores
- Control de LEDs
- Lectura del sensor LDR
- Control de servomotor
- Recepción de comandos mediante I2C

### ESP32 #3 – Interfaz web
- Creación de red WiFi Access Point
- Servidor HTTP
- Página HTML + JavaScript
- Comunicación UART con ESP32 #1

---

## Hardware utilizado

- 3 microcontroladores ESP32
- Pantalla OLED SSD1306
- LEDs
- Sensor LDR
- Servomotor
- Resistencias
- Protoboards
- Cables dupont
- Cable 22 awg
- Fuente de alimentación USB

---

## Funciones del sistema

- Apertura y cierre remoto de puerta
- Encendido y apagado de iluminación
- Ajuste de brillo de LEDs
- Monitoreo de estados en pantalla OLED
- Detección automática de iluminación ambiental mediante LDR
- Reinicio general del sistema
- Control inalámbrico mediante navegador web

---

## 📸 Evidencias

### Maqueta del sistema


### Página web
![web](Imágenes/web.png)

### Arquitectura general

---

## Integrantes

- Karol Noemi Araujo Saavedra
- Paulina Guisel Huerta Márquez
- Santiago Ivozed Duarte Hérnandez
