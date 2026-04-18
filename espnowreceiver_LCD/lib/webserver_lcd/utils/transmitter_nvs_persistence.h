#ifndef TRANSMITTER_NVS_PERSISTENCE_H
#define TRANSMITTER_NVS_PERSISTENCE_H

class TransmitterNvsPersistence {
public:
    static void init();
    static void loadFromNVS();
    static void saveToNVS();
    static void persist();
    static void notifyAndPersist();
};

#endif
