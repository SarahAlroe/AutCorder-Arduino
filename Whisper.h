#pragma once

#include "Arduino.h"
#include "FS.h"
class Whisper {
  public:
    Whisper();
    void init(String serverName, String serverPath, String lang, String token);
    String transcribeFile(File fileToSend);

  private:
    String serverName;
    String serverPath; 
    String lang = ""; 
    String token;
};
