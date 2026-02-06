We have the files in C:\\Users\\GrahamWillsher\\OneDrive - Graham Willsher\\Visual Studio Code\\Battery-Emulator-9.2.4 - AI\\Battery-Emulator-9.2.4 that are the original project and my files in workspace C:\\Users\\GrahamWillsher\\ESP32Projects.



The background is that we are moving from a single hardware device configuration (although there are numerous hardware environments available for that build) to my version which splits the webserver coding to a second device the lilygo T-Display-s3, to reduce the overhead on the single device setup. This means that there are 'settings' webpages for 2 devices rather than just the one.



I need to migrate the existing single device project to my 2 device version.



We need to extract all/any missing the webserver pages to go on my receiver but in the coding format that fits in with what I already have.

The remaining code should be left on the transmitter, which is the core of the realtime control system, and should integrate with the existing code that I have the main items being:



* espnow messaging to the second device - used for webserver updating/displaying data
* the use of Ethernet for communications/MQTT - and setting system time/date
* Any tasks that are in my code should be made to a lower priority so that they do not interfere with the high priority tasks of the main control code.



The new webserver setup currently does not send any data back to be stored on the transmitter, this will need to be addressed by both the addition of a 'save' button on the appropriate webpages and the necessary inclusion of new/updated espnow messages and appropriate handlers to send the changed/entered data back from the receive to the transmitter, and for the transmitter to store in the appropriate locations.



We only have 1 hardware board for the main device (transmitter) so the configuration should be adjusted to account for that (maybe just a single hardware selection)

The LED is now a simulated LED on the receiver, which only needs to know which main colour it is flashing (red/Green/Orange), so the code for that will need to be adjusted.

The transmitter does not need to setup a wifi AP as all communication to it will be via the receiver, and if necessary for a virgin board a new espnow stub will be written.



Please go through these projects and suggest a full implementation plan to carry out this migration, which can be carried out in bitesize and easily testable steps, as this is a large code migration, and any other suggestions that you can think of to aid this migration.



Any code improvements that you can see should be suggested for later improvements, when you carry out a comprehensive code review for the whole project, suggesting improvements in speed/efficiency/readability/maintainability.

