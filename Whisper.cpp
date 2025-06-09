#include "Arduino.h"
#include "Whisper.h"
#include <NetworkClientSecure.h>

static const char* TAG = "Whisper";

 const char* WHISPER_CERT = "-----BEGIN CERTIFICATE-----\n"
"MIIEVzCCAj+gAwIBAgIRAIOPbGPOsTmMYgZigxXJ/d4wDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAw\n"
"WhcNMjcwMzEyMjM1OTU5WjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n"
"RW5jcnlwdDELMAkGA1UEAxMCRTUwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQNCzqK\n"
"a2GOtu/cX1jnxkJFVKtj9mZhSAouWXW0gQI3ULc/FnncmOyhKJdyIBwsz9V8UiBO\n"
"VHhbhBRrwJCuhezAUUE8Wod/Bk3U/mDR+mwt4X2VEIiiCFQPmRpM5uoKrNijgfgw\n"
"gfUwDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQGCCsGAQUFBwMCBggrBgEFBQcD\n"
"ATASBgNVHRMBAf8ECDAGAQH/AgEAMB0GA1UdDgQWBBSfK1/PPCFPnQS37SssxMZw\n"
"i9LXDTAfBgNVHSMEGDAWgBR5tFnme7bl5AFzgAiIyBpY9umbbjAyBggrBgEFBQcB\n"
"AQQmMCQwIgYIKwYBBQUHMAKGFmh0dHA6Ly94MS5pLmxlbmNyLm9yZy8wEwYDVR0g\n"
"BAwwCjAIBgZngQwBAgEwJwYDVR0fBCAwHjAcoBqgGIYWaHR0cDovL3gxLmMubGVu\n"
"Y3Iub3JnLzANBgkqhkiG9w0BAQsFAAOCAgEAH3KdNEVCQdqk0LKyuNImTKdRJY1C\n"
"2uw2SJajuhqkyGPY8C+zzsufZ+mgnhnq1A2KVQOSykOEnUbx1cy637rBAihx97r+\n"
"bcwbZM6sTDIaEriR/PLk6LKs9Be0uoVxgOKDcpG9svD33J+G9Lcfv1K9luDmSTgG\n"
"6XNFIN5vfI5gs/lMPyojEMdIzK9blcl2/1vKxO8WGCcjvsQ1nJ/Pwt8LQZBfOFyV\n"
"XP8ubAp/au3dc4EKWG9MO5zcx1qT9+NXRGdVWxGvmBFRAajciMfXME1ZuGmk3/GO\n"
"koAM7ZkjZmleyokP1LGzmfJcUd9s7eeu1/9/eg5XlXd/55GtYjAM+C4DG5i7eaNq\n"
"cm2F+yxYIPt6cbbtYVNJCGfHWqHEQ4FYStUyFnv8sjyqU8ypgZaNJ9aVcWSICLOI\n"
"E1/Qv/7oKsnZCWJ926wU6RqG1OYPGOi1zuABhLw61cuPVDT28nQS/e6z95cJXq0e\n"
"K1BcaJ6fJZsmbjRgD5p3mvEf5vdQM7MCEvU0tHbsx2I5mHHJoABHb8KVBgWp/lcX\n"
"GWiWaeOyB7RP+OfDtvi2OsapxXiV7vNVs7fMlrRjY1joKaqmmycnBvAq14AEbtyL\n"
"sVfOS66B8apkeFX2NY4XPEYV4ZSCe8VHPrdrERk2wILG3T/EGmSIkCYVUMSnjmJd\n"
"VQD9F6Na/+zmXCc=\n"
"-----END CERTIFICATE-----\n";


Whisper::Whisper() {
}

void Whisper::init(String serverName, String serverPath, String lang, String token) {
  ESP_LOGI(TAG, "Initialising Whisper at %s", serverName.c_str());
  this->serverName = serverName;
  this->serverPath = serverPath;
  this->lang = lang;
  this->token = token;
}

String Whisper::transcribeFile(File fileToSend){
  fileToSend.seek(0);
  NetworkClientSecure *client = new NetworkClientSecure;
  client->setCACert(WHISPER_CERT);
  int serverPort = 443;
  if (client->connect(this->serverName.c_str(), serverPort)) {
    String head = "--AutCam\r\n"
                  "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                  "deepdml/faster-whisper-large-v3-turbo-ct2"
                  "\r\n--AutCam\r\n"
                  "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
                  "text"
                  "\r\n--AutCam\r\n";
    if (this->lang != ""){
      head += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
      head += this->lang;
      head += "\r\n--AutCam\r\n";
    }
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"";
    head += fileToSend.name();
    head += "\"\r\nContent-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--AutCam--\r\n\r\n";
    uint32_t imageLen = fileToSend.size();
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;
    client->println("POST " + this->serverPath + " HTTP/1.1");
    client->println("Host: " + serverName);
    client->println("Accept: */*");
    client->println("Content-Type: multipart/form-data; boundary=AutCam");
    client->println("Authorization: Bearer " + this->token);
    client->println("Content-Length: " + String(totalLen));
    client->println();
    client->print(head);
  
    uint8_t buffer[1024];
    while( fileToSend.available() ) {
      size_t read_bytes = fileToSend.read( buffer, 1024 );
      client->write(buffer, read_bytes);
    }
    client->print(tail);
    unsigned long timeout = millis();
    while(client->available() == 0){
      if(millis() - timeout > 120000){ // Larger timeout
        ESP_LOGW(TAG, "Client Timeout !");
        client->stop();
        return "";
      }
    }
    String firstLine = client->readStringUntil('\r');
    ESP_LOGI(TAG, "< %s", firstLine.c_str());
    firstLine = firstLine.substring(firstLine.indexOf(" ")+1, firstLine.indexOf(" ")+4); // Take the 3 character responsecode right after first space.
    bool got200Code = firstLine.toInt() < 300;
    while(client->available()) {
      String line = client->readStringUntil('\r');
      ESP_LOGI(TAG, "< %s", line.c_str());
      if (line == "\n"){ // Is the CR (LF) CR LF ending sequence of the header
        client -> read(); // Read the remaining LF
        break;
      }
    }
    String body = "";
    while(client->available()) {
      body += client->readStringUntil('\n');
    }
    client->stop();
    return body;
  }else{
    return ""; // Failed to connect
  }
}