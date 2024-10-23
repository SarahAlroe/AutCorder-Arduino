#pragma once

#include "Arduino.h"
#include "FS.h"
class Discord {
  public:
    Discord();
    void init(String id, String token, String botName);
    bool sendFile(File fileToSend);

  private:
    String id;
    String token;
    String name;
};
