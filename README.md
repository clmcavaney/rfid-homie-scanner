Taken the idea/code from www.internetoflego.com project by Cory Guynn.
See http://www.internetoflego.com/rfid-scanner-wemos-rc522-mqtt/ for reference
Original repository - https://github.com/dexterlabora/rfid-homie-scanner

Added in a DHT11 temperature and humidity sensor


# Background

# RFID Homie Scanner

An Internet of LEGO project to understand how the RFID-RC522 scanner works. 

Communication via WiFi & MQTT using Homie.h
 * The ID will be sent to the MQTT topic.
 * When a success verification occurs, the lights and speaker will be activated.

## Hardware
* Wemos D1-mini (ESP8266 12-e)
* RFID-RC522 
* x2 LEDs

## Network
* WiFi
* MQTT broker
* MQTT server to respond to scanner messages


## About
Written by Cory Guynn

For write-ups and other cool projects, check out the blog:

www.InternetOfLEGO.com

2016

## License
MIT

