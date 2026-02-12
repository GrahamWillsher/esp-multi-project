We need to send a connection notice when MQTT has returned that it has logged in correctly. This should also be instigated by the transmitter and sent to the receiver to the cache again.
On the appropriate page on the receiver the MQTT should be changed from a red dot which indicates connecting to a green dot which indicates connected.
At the moment it doesn't look like MQTT information is part of the cache, please check as when you go to transmitter/config the mqtt enabled box is not ticked.
Please check and review and give  me a document highlighting the changes required to get this to work property, and any other improvements that you can recommend.
THis sort of updating of the cache when data becomes available would also be necessary for the IP address. so any improvements that you suggest should take into account of other static information being available after the first set of data has been send to the receiver during the initial handshaking.
