# Smart Home

Control your home remotely with [Smart Home App](https://github.com/rutvora/Smart-Home-App)

## Features
- Automatically connects to broker given by broker.local
- Can be configured via [Smart Home App](https://github.com/rutvora/Smart-Home-App)
- Automatically shifts to hotspot mode to edit/change configuration in case of WiFi connection failure
    - _NB: You can only configure it when it is in the hotspot mode_
- Can control max 9 devices (pin D0 to D8 on the NodeMCU board)
    - _NB: The MQTT messages received refer to these pin numbers which are then internally translated to the GPIO pin numbers_
  
