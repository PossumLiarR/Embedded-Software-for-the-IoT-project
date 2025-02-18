/*-----ESP32CAM_Smart_Surveillance-----*/

/*-----Libraries included-----*/
#include <stdio.h>              //Standard libraries    
#include <stdlib.h>       
#include <string.h>
#include "config.h"             //Header file for custom Wi-Fi and Telegram_Chat Bot credentials
#include "esp_camera.h"         //ESP32CAM camera driver
#include <WiFi.h>               //ESP32 Wi-Fi support
#include <WiFiClientSecure.h>   //Expands WiFi.h with SSL TLS secure connections
#include <HTTPClient.h>         //Library to easily handle HTTP GET, POST and PUT requests to a web server
#include <HardwareSerial.h>     //Hardware serial library for Wiring


/*-----ESP32CAM Pin Configuration for Ai-Thinker OV2640 camera-----*/
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


/*-----Global definitions-----*/
//Motion Detection Constants
#define FRAME_WIDTH               320   //Width of the captured frame
#define FRAME_HEIGHT              240   //Height of the captured frame
#define MOTION_THRESHOLD          110   //Threshold to exceed to detect motion in a single pixel (increase if too sensitive, decrease if not sensitive enough)
#define MIN_PIXEL_CHANGE         3000   //Threshold to exceed: how many pixels in the frame have to be different to detect motion?
#define SEND_IMAGE_OVERFLOW_MAX     6   //Image spam management variable
#define MOTION_LIMIT               30   //Max number of frames without motion detectable before resetting in the default position (CENTER)

//Variables
int frames_without_motion = 0;     
int image_counter_to_send = 0;
//char prevDir = 'C';               //Starting direction
uint8_t* prevFrame;                 //Pointer to PSRAM buffer 


/*-----Function headers-----*/
void setupCamera();
void sendPhotoTelegram();
void updateMax(int *maxDiff, char *direction, int newDiff, char newDirection);
char findMaxDirection(int leftDiff, int rightDiff, int centerDiff, int upDiff, int downDiff);
char detectMotion(uint8_t* frame, int lenght);
void wifiTask(void *pvParameters);


/*-----Setup-----*/
void setup(){
  Serial.begin(115200);                         //Initializes Serial [Baud]
  Serial2.begin(115200, SERIAL_8N1, 15, 14);    //Initializes Serial2 (RX = pin 15) (TX = pin 14) [Baud]
  
  xTaskCreatePinnedToCore(                      //Connects to Wi-Fi (Runs only on Core 0)
    wifiTask,    //Function
    "WiFiTask",  //Task Name
    4096,        //Stack Size
    NULL,        //Parameters
    1,           //Priority
    NULL,        //Task Handle
    0            //Run on Core 0
  );

//WiFi.begin(ssid, password);
//while(WiFi.status() != WL_CONNECTED) {
//  Serial.print(".");
//  delay(500);
//}
  Serial.println("\nConnected to Wi-Fi!");
  WiFi.setSleep(WIFI_PS_NONE);                //Keeps the board connected to Wi-Fi (faster response times with higher power consumption)
  
  prevFrame = (uint8_t*) ps_malloc(FRAME_WIDTH * FRAME_HEIGHT);    //Allocates memory in PSRAM
  if(!prevFrame) {
    Serial.println("Failed to allocate PSRAM for prevFrame!");
    while(1);     //Stops execution if memory allocation fails
  }
  
  setupCamera();                              //Initializes the camera
//WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //Handle error -> cam_hal: EV-VSYNC-OVF
  
  memset(prevFrame, 0, sizeof(prevFrame));    //Initializes previous frame buffer (sets all pixels to 0)    
  Serial.printf("ESP32-CAM Ready!\n");
}


/*-----Main loop-----*/
void loop(){
  camera_fb_t* frame = esp_camera_fb_get();               //Captures new frame from the camera
  if(!frame) {
      Serial.printf("Failed to capture frame!\n");
      return;  //Failed to capture frame
  }
  
  char motionDir = detectMotion(frame->buf, frame->len);  //Detects motion in the captured frame  
  
  if(frames_without_motion >= MOTION_LIMIT){    //If there has been no movements for MOTION_LIMIT frames, resets the camera into the default position (CENTER)
    Serial2.print('C');
    Serial.print('C');
  //delay(500);
    esp_camera_fb_return(frame);                //Memory and variables management to get ready for following iterations
    frame = esp_camera_fb_get();
    memcpy(prevFrame, frame->buf, frame->len);
    esp_camera_fb_return(frame);
    frames_without_motion = 0;
    return;                 
  }  
  
  if(motionDir != 'N') {                      //If motion is detected, ESP32CAM sends the state via UART to MSP432
    frames_without_motion = 0;
    Serial.print(motionDir);
    
    if(motionDir != 'C')                      //Checks to guarantee that detected movement in the center does not move the ESP32CAM
      Serial2.print(motionDir);
    
    if(++image_counter_to_send >= SEND_IMAGE_OVERFLOW_MAX){   //Sends a picture to the Telegram_Chat Bot each SEND_IMAGE_OVERFLOW_MAX motion detections
      size_t jpg_len;
      uint8_t *jpg_buf;
      frame2jpg(frame, 12, &jpg_buf, &jpg_len);           //Uses the camera hardware to convert to .jpg (Bot accepts only .jpg and .png)
      sendPhotoTelegram(jpg_buf, jpg_len);                //Sends .jpg file
      free(jpg_buf);
      image_counter_to_send = 0;   //Counter reset
    }

    delay(500);                      //Waits for servos to move
    esp_camera_fb_return(frame);     //Releases the allocated memory of the frame
    frame = esp_camera_fb_get();     //If servos moved in this iteration, recaptures the picture
  //prevDir = motionDir;
  }
  else{                           //When motion is NOT detected in this iteration
    image_counter_to_send = 0;   
    ++frames_without_motion;
  }

  memcpy(prevFrame, frame->buf, frame->len);    //Saves current frame as prevFrame for the next comparison
  esp_camera_fb_return(frame);                  //Releases memory used by the frame
//vTaskDelay(100 / portTICK_PERIOD_MS);         //Delay to reduce CPU usage
}


/*-----Function bodies-----*/

//Wi-Fi configuration
void wifiTask(void *pvParameters){
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
  //delay(500);
  }
  Serial.println("\nConnected to Wi-Fi!");
  vTaskDelete(NULL);  //Task deletes itself after connecting
}


//Camera initialization
void setupCamera() {
  camera_config_t config; 

//LED Controller for camera clock    
  config.ledc_channel = LEDC_CHANNEL_0; 
  config.ledc_timer = LEDC_TIMER_0;

//GPIO pins
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;                    //External CLK pin
  config.pin_pclk = PCLK_GPIO_NUM;                    //Pixel CLK pin
  config.pin_vsync = VSYNC_GPIO_NUM;                  //Vertical sync signal pin
  config.pin_href = HREF_GPIO_NUM;                    //Horizontal sync signal pin
  config.pin_sscb_sda = SIOD_GPIO_NUM;                //I2C: SDA pin
  config.pin_sscb_scl = SIOC_GPIO_NUM;                //I2C: SCL pin
  config.pin_pwdn = PWDN_GPIO_NUM;                    //Power down pin (not used)
  config.pin_reset = RESET_GPIO_NUM;                  //Reset pin (not used)
  config.xclk_freq_hz = 5000000;                      //CLK frequency (suggested: 10MHz)
  config.pixel_format = PIXFORMAT_GRAYSCALE;          //Pixel format: Grayscale mode for faster processing
  config.frame_size = FRAMESIZE_QVGA;                 //Captured frame resolution -> FRAMESIZE_QVGA = 320x240
  config.jpeg_quality = 12;                           //JPEG quality (the lower the better)
  config.fb_count = 1;                                //Number of frame buffers
//config.fb_location = CAMERA_FB_IN_PSRAM;
  
  if(esp_camera_init(&config) != ESP_OK) {            //Checks to see if it initialized correctly 
    Serial.printf("Camera initialization failed!\n");
    return;
  }
}


//Maintains the direction where the highest motion got detected
void updateMax(int *maxDiff, char *direction, int newDiff, char newDirection) {
  if(newDiff > *maxDiff) {
    *maxDiff = newDiff;
    *direction = newDirection;
  }
}


//Chooses direction in the frame where there is more motion happening and returns its value
char findMaxDirection(int leftDiff, int rightDiff, int centerDiff, int upDiff, int downDiff) {
  char direction = 'N';               //Default: No motion
  int maxDiff = 0;

  updateMax(&maxDiff, &direction, upDiff, 'U');
  updateMax(&maxDiff, &direction, downDiff, 'D');
  updateMax(&maxDiff, &direction, leftDiff, 'L');
  updateMax(&maxDiff, &direction, rightDiff, 'R');
  updateMax(&maxDiff, &direction, centerDiff, 'C');
  return direction;
}


//Performs motion detection with the previous frame and the captured frame
char detectMotion(uint8_t* frame, int lenght) {
  if(!prevFrame) {                                          //Checks memory allocations and return 'N' for the very first iteration ('N' = No motion)
    prevFrame = (uint8_t*) ps_malloc(lenght);
    if(!prevFrame) {
      Serial.println("Error: Not enough memory for prevFrame!");
      return 'N';  
    }
    memcpy(prevFrame, frame, lenght);
    return 'N';
  }

  int leftDiff = 0, rightDiff = 0, centerDiff = 0;
  int upDiff = 0, downDiff = 0;
  int motionPixels = 0;

  for (int i = 0; i < lenght; i++) {              //For every pixel of the frame ...
    int x = i % FRAME_WIDTH;                      //Divides the frame into 9 equal size block for the localization of the motion in the frame
    int y = i / FRAME_WIDTH;
    
    int diff = abs(frame[i] - prevFrame[i]);      //... it compares its value with the prevFrame value (diff). ...  
    if(diff > MOTION_THRESHOLD) {                 //... If diff overcomes the custom threshold, then the pixel gets considered as part of a motion and gets localized.
      motionPixels++;
      
      //Motion localization
      if(x < FRAME_WIDTH/3)             ++leftDiff;       
      else if(x > (2*FRAME_WIDTH)/3)    ++rightDiff;
      else                              ++centerDiff;
      if(y < FRAME_HEIGHT/3)            ++upDiff;
      else if (y > (2*FRAME_HEIGHT)/3)  ++downDiff;
    }
  }

//memcpy(prevFrame, frame, lenght);
  return findMaxDirection(leftDiff, rightDiff, centerDiff, upDiff, downDiff);
}


//Sends .jpg of the frame to the Telegram_Chat Bot
void sendPhotoTelegram(uint8_t* frame, size_t len) {
  if(WiFi.status() != WL_CONNECTED){                    //Checks if Wi-Fi is still connected (if not, then it reconnects)
    Serial.println("WiFi is disconnected!");
    xTaskCreatePinnedToCore(
      wifiTask,    // Function
      "WiFiTask",  // Task Name
      4096,        // Stack Size
      NULL,        // Parameters
      1,           // Priority
      NULL,        // Task Handle
      0            // Run on Core 0
    );
  }
  
  Serial.println("Sending photo to Telegram...");
  WiFiClientSecure client;
//client.setInsecure();                     //Ignores SSL verification (not useful because Telegram requires HTTPS)   
  client.setCACert(telegram_root_ca);       //If certificate is not expired, HTTP -> HTTPS
    
//Telegram API URL construction
  String url = "https://api.telegram.org/bot" + String(botToken) + "/sendPhoto";
  if(!client.connect("api.telegram.org", 443)) {                                  //Checks the connection to the Telegram_Chat Bot
    Serial.println("Connection to Telegram_Chat Bot failed!");
    return;
  }

  String boundary = "----ESP32Boundary";                                                //Defines boundary for multipart request  
  String payloadHeader = "--" + boundary + "\r\n"                                       //Constructs the initial HTTP headers
                         "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + 
                         String(chatID) + "\r\n" +
                         "--" + boundary + "\r\n" +
                         "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n"
                         "Content-Type: image/jpeg\r\n\r\n";
  String payloadFooter = "\r\n--" + boundary + "--\r\n";                                //Closes the boundary
  
  int contentLength = payloadHeader.length() + len + payloadFooter.length();    //Calculates the total content length

//Sending the HTTP POST request (secured)
  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println();  //Empty line that indicates the end of headers

//Sending the headers and image data
  client.print(payloadHeader);  //Sends the initial form-data headers
  client.write(frame, len);     //Sends the raw image data
  client.print(payloadFooter);  //Sends the closing boundary

//Waiting for response from Telegram
  Serial.println("Waiting for response...");
  while(client.connected()) {
    String response = client.readStringUntil('\n');
  //Serial.println(response);
    if(response == "\r") break;  //Headers end at a blank line
  }

  Serial.println("Photo sent successfully!");
  client.stop();  //Closes connection
}
