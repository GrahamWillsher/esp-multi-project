On the receiver we need to save the wifi settings in nvs these being hostname, wifi ssid, and password, and the ip address and whether is it static or dynamic. The password can just be stored and displayed as plain text.

Please use the same structure as for the transmitter nvs settings. also the receiver will then needs its own cache where it reads this data into on first boot and saves any changes. it should also use the same process to update any information, data written to cache first then NVS. PLease suggest how this amended data will be updated /displayed on relevant web pages.

On the transmitter we now need to be able to save the rest of the information in the transmitter/config page e.g. battery configuration, power settings inverter configuration etc. on exactly the same lines as mqtt configuration and ethernet ip configuration.

Please write this as a document and suggest any necessary changes improvements to make this work, but your guide for the structure of the receiver saving should be the transmitter as we already have this working.