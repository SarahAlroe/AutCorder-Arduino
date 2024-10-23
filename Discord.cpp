#include "Arduino.h"
#include "Discord.h"
#include <NetworkClientSecure.h>

extern bool SERIAL_DEBUG;

Discord::Discord() {
}

void Discord::init(String id, String token, String botName) {
  this->id = id;
  this->token = token;
  this->name = botName;
}

bool Discord::sendFile(File fileToSend){
    /*
  * Get current discordapp.com certificate:
  * openssl s_client -showcerts -connect discordapp.com:443
  */
  const char* DISCORD_CERT = "-----BEGIN CERTIFICATE-----\n"
  "MIIDzTCCArWgAwIBAgIQCjeHZF5ftIwiTv0b7RQMPDANBgkqhkiG9w0BAQsFADBa\n"
  "MQswCQYDVQQGEwJJRTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJl\n"
  "clRydXN0MSIwIAYDVQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTIw\n"
  "MDEyNzEyNDgwOFoXDTI0MTIzMTIzNTk1OVowSjELMAkGA1UEBhMCVVMxGTAXBgNV\n"
  "BAoTEENsb3VkZmxhcmUsIEluYy4xIDAeBgNVBAMTF0Nsb3VkZmxhcmUgSW5jIEVD\n"
  "QyBDQS0zMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEua1NZpkUC0bsH4HRKlAe\n"
  "nQMVLzQSfS2WuIg4m4Vfj7+7Te9hRsTJc9QkT+DuHM5ss1FxL2ruTAUJd9NyYqSb\n"
  "16OCAWgwggFkMB0GA1UdDgQWBBSlzjfq67B1DpRniLRF+tkkEIeWHzAfBgNVHSME\n"
  "GDAWgBTlnVkwgkdYzKz6CFQ2hns6tQRN8DAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0l\n"
  "BBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBIGA1UdEwEB/wQIMAYBAf8CAQAwNAYI\n"
  "KwYBBQUHAQEEKDAmMCQGCCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5j\n"
  "b20wOgYDVR0fBDMwMTAvoC2gK4YpaHR0cDovL2NybDMuZGlnaWNlcnQuY29tL09t\n"
  "bmlyb290MjAyNS5jcmwwbQYDVR0gBGYwZDA3BglghkgBhv1sAQEwKjAoBggrBgEF\n"
  "BQcCARYcaHR0cHM6Ly93d3cuZGlnaWNlcnQuY29tL0NQUzALBglghkgBhv1sAQIw\n"
  "CAYGZ4EMAQIBMAgGBmeBDAECAjAIBgZngQwBAgMwDQYJKoZIhvcNAQELBQADggEB\n"
  "AAUkHd0bsCrrmNaF4zlNXmtXnYJX/OvoMaJXkGUFvhZEOFp3ArnPEELG4ZKk40Un\n"
  "+ABHLGioVplTVI+tnkDB0A+21w0LOEhsUCxJkAZbZB2LzEgwLt4I4ptJIsCSDBFe\n"
  "lpKU1fwg3FZs5ZKTv3ocwDfjhUkV+ivhdDkYD7fa86JXWGBPzI6UAPxGezQxPk1H\n"
  "goE6y/SJXQ7vTQ1unBuCJN0yJV0ReFEQPaA1IwQvZW+cwdFD19Ae8zFnWSfda9J1\n"
  "CZMRJCQUzym+5iPDuI9yP+kHyCREU3qzuWFloUwOxkgAyXVjBYdwRVKD05WdRerw\n"
  "6DEdfgkfCv4+3ao8XnTSrLE=\n"
  "-----END CERTIFICATE-----\n";
  
  fileToSend.seek(0);
  NetworkClientSecure pclient;
  pclient.setCACert(DISCORD_CERT);
  String serverName = "discord.com";
  String serverPath = "/api/webhooks/";
  int serverPort = 443;
  if (pclient.connect(serverName.c_str(), serverPort)) {
    String head = "--AutCam\r\nContent-Disposition: form-data; name=\"payload_json\"\r\n\r\n{\"username\": \""+name+"\", \"content\": \""+fileToSend.name()+"\"}\r\n--AutCam\r\nContent-Disposition: form-data; name=\"file1\"; filename=\""+fileToSend.name()+"\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--AutCam--\r\n";
    uint32_t imageLen = fileToSend.size();
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;
    pclient.println("POST " + serverPath + id + "/" + token + " HTTP/1.1");
    pclient.println("Host: " + serverName);
    pclient.println("Accept: */*");
    pclient.println("Content-Type: multipart/form-data; boundary=AutCam");
    pclient.println("Content-Length: " + String(totalLen));
    pclient.println();
    pclient.print(head);
  
    uint8_t buffer[1024];
    while( fileToSend.available() ) {
      size_t read_bytes = fileToSend.read( buffer, 1024 );
      pclient.write(buffer, read_bytes);
    }
    pclient.print(tail);
    unsigned long timeout = millis();
    while(pclient.available() == 0){
      if(millis() - timeout > 5000){
        if (SERIAL_DEBUG) {Serial.println(">>> Client Timeout !");}
        pclient.stop();
        return false;
      }
    }
    String firstLine = pclient.readStringUntil('\r');
    if (SERIAL_DEBUG) {Serial.print(firstLine);}
    firstLine = firstLine.substring(firstLine.indexOf(" ")+1, firstLine.indexOf(" ")+4); // Take the 3 character responsecode right after first space.
    bool got200Code = firstLine.toInt() < 300;
    while(pclient.available()) {
      String line = pclient.readStringUntil('\r');
      if (SERIAL_DEBUG) {Serial.print(line);}
    }
    pclient.stop();
    return got200Code;
  }else{
    return false; // Failed to connect
  }
  return true;
}
