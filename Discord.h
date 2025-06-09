#pragma once

#include "Arduino.h"
#include "FS.h"
class Discord {
  public:
    Discord();
    void init(String id, String token, String botName);
    bool sendFile(File fileToSend);
    bool sendMessage(String messageToSend);

  private:
    String id;
    String token;
    String name;
};
