# mqtt interface

The mqtt interface serves two purposes.
* Data is made available at regular intervals.
* Defined commands can be sent to the device.

Every topic starts as subtopic of **ebus/**

These topics are available on every device.
|***topic***          |***description***
|:-                   |:-
|**device**           |
|ebus/device          |information about your device                                   
|ebus/device/ebus     |basic ebus adapter settings (Configration)
|ebus/device/firmware |details of installed firmware
|ebus/device/wifi     |wifi details
|**counter**          | 
|ebus/arbitration     |arbitration over common interface 
|ebus/arbitration/won |details of won arbitration 
|ebus/arbitration/lost|details of lost arbitration     
|**commands**         |
|ebus/config/restart  |restarting of the device 
|-                    |
|**EBUS_INTERNAL=1**  |
|**output**           |
|ebus/commands        |list of installed commands
|ebus/value           |received values of installed commands
|ebus/sent            |values of 'send' command: subtopic=master; value=slave;
|ebus/raw             |values of 'raw' printout: subtopic=master; value=slave;
|**counter**          |
|ebus/messages        |processed messages
|ebus/errors          |errors of finite state machine  
|ebus/resets          |resets of finite state machine 
|ebus/requets         |bus requests (arbitration)
|**commands**         |
|ebus/config/insert   |insert new command
|ebus/config/remove   |remove loaded command
|ebus/config/list     |list loaded commands
|ebus/config/load     |loading saved commands
|ebus/config/save     |saving loaded commands
|ebus/config/wipe     |wiping saved commands
|ebus/config/send     |sending of given ebus command(s) once
|ebus/config/raw      |enable/disable of raw data printout
|ebus/config/filter   |filter for raw data printout
|**response**         |
|ebus/config/loading  |bytes of loaded commands
|ebus/config/saving   |bytes of saved commands
|ebus/config/wiping   |bytes of wiped commands
|ebus/conifg/error    |message of last occurred error

