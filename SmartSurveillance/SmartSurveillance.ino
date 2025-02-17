//standard includes
#include <stdio.h>        
#include <stdlib.h>       
#include <string.h>       
#include "config.h"
#include "esp_camera.h"   // ESP32-CAM camera driver
#include <WiFi.h>         //ESP32 wifi support
#include <HTTPClient.h>   //Library to easily make HTTP GET, POST and PUT requests to a web server
#include <WiFiClientSecure.h>
#include <HardwareSerial.h>

//ESP32-CAM Camera Pin Configuration for Ai-Thinker OV2640 camera

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

#define THRESHOLD_VALUE           128   // Imposta il valore di soglia

//Motion Detection Constants
#define FRAME_WIDTH               320   // Width of the captured frame 320
#define FRAME_HEIGHT              240   // Height of the captured frame 240
#define MOTION_THRESHOLD          110   // Threshold to detect motion (increase if too sensitive, decrease if not sensitive enough)
#define MIN_PIXEL_CHANGE         3000   //Threshold on how many pixels must be different to detect motion
#define SEND_IMAGE_OVERFLOW_MAX     6   //Spam management variables
#define MOTION_LIMIT               30   //Max number of frames without motion detectable before resetting in default position
#define DEBUG                    true   //Debug prints

int frames_without_motion = 0;
int image_counter_to_send = 0;
char prevDir = 'C';
//Motion Detection Variables
uint8_t* prevFrame;  // Pointer to PSRAM buffer 
//Function Prototypes
void setupCamera();
void sendPhotoTelegram();
void updateMax(int *maxDiff, char *direction, int newDiff, char newDirection);
char findMaxDirection(int leftDiff, int rightDiff, int centerDiff, int upDiff, int downDiff);
char detectMotion(uint8_t* frame, int lenght);

void wifiTask(void *pvParameters){
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        //delay(500);
    }
    Serial.println("\n Connected to Wi-Fi!");
    vTaskDelete(NULL);  // Task deletes itself after connecting
}

//setup
void setup(){
    
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, 15, 14);

    // Connect to Wi-Fi
    xTaskCreatePinnedToCore(
        wifiTask,    // Function
        "WiFiTask",  // Task Name
        4096,        // Stack Size
        NULL,        // Parameters
        1,           // Priority
        NULL,        // Task Handle
        0            // Run on Core 0
    );
    /*WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        //delay(500);
    }
    Serial.println("\n Connected to Wi-Fi!");
    WiFi.setSleep(WIFI_PS_NONE);*/

    // Allocate memory in PSRAM
    prevFrame = (uint8_t*) ps_malloc(FRAME_WIDTH * FRAME_HEIGHT);
    if (!prevFrame) {
        Serial.println("Failed to allocate PSRAM for prevFrame!");
        while (1);  // Stop execution if memory allocation fails
    }

    Serial.println("MOTION DETECT");
    //Initialize the camera
    setupCamera();
    //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //handle error: cam_hal: EV-VSYNC-OVF
    //Initialize previous frame buffer (set all pixels to 0)
    memset(prevFrame, 0, sizeof(prevFrame));
    
    Serial.printf("ESP32-CAM Ready!\n");
    
}

//Main loop
void loop(){
    //Capture a frame from the camera
    camera_fb_t* frame = esp_camera_fb_get();
    if (!frame) {
        Serial.printf("Failed to capture frame\n");
        return;  //failed to capture frame
    }
    // Detect motion in the captured frame
    char motionDir = detectMotion(frame->buf, frame->len);
    if(frames_without_motion >= MOTION_LIMIT){         //If there has been no movements for MOTION_LIMIT frames, then it resets to default position
         Serial2.print('C');
         Serial.print('C');
         delay(1200);
         esp_camera_fb_return(frame);
         frame = esp_camera_fb_get();
         memcpy(prevFrame, frame->buf, frame->len);
         esp_camera_fb_return(frame);
         frames_without_motion=0;
         return;                 
    }
    // If motion is detected, send a command via UART
    if (motionDir != 'N') {
        frames_without_motion = 0;
        Serial.print(motionDir);
        if(motionDir != 'C') Serial2.print(motionDir);
        //Sends a picture each SEND_IMAGE_OVERFLOW_MAX motion detections
        if(++image_counter_to_send >= SEND_IMAGE_OVERFLOW_MAX){
            size_t jpg_len;
            uint8_t *jpg_buf;
            frame2jpg(frame, 12, &jpg_buf, &jpg_len);  //use camera hardware to convert to jpg
            sendPhotoTelegram(jpg_buf, jpg_len);   //send jpg
            free(jpg_buf);
            image_counter_to_send = 0; //reset the counter
        }
        //if servos moved, recapture the picture
        delay(500);                      //wait for servos to move
        esp_camera_fb_return(frame);     //release memory
        frame = esp_camera_fb_get();     //take a new picture
        prevDir=motionDir;
    }else{
      image_counter_to_send = 0;   
      ++frames_without_motion;
    }
    //Save in prevFrame, for the next comparison
    memcpy(prevFrame, frame->buf, frame->len);
    //Release memory used by the frame
    esp_camera_fb_return(frame);

    //Delay to reduce CPU usage
    //vTaskDelay(100 / portTICK_PERIOD_MS);
}
//Function to Initialize the Camera
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

    config.pin_xclk = XCLK_GPIO_NUM; //external clock signal
    config.pin_pclk = PCLK_GPIO_NUM; //pixel clock
    config.pin_vsync = VSYNC_GPIO_NUM; //Vertical sync signal
    config.pin_href = HREF_GPIO_NUM; //Horizontal sync signal
    config.pin_sscb_sda = SIOD_GPIO_NUM; //I2C SDA
    config.pin_sscb_scl = SIOC_GPIO_NUM; //I2C SCL
    config.pin_pwdn = PWDN_GPIO_NUM; //power down pin (not used)
    config.pin_reset = RESET_GPIO_NUM; //reset pin (not used)
    config.xclk_freq_hz = 5000000;  // Set clock frequency (10MHz)
    config.pixel_format = PIXFORMAT_GRAYSCALE;  // Grayscale mode for faster processing
    config.frame_size = FRAMESIZE_QVGA; // 320x240 resolution   FRAMESIZE_QVGA
    config.jpeg_quality = 12; // JPEG quality (lower is better)
    config.fb_count = 1; //Number of frame buffers 1
    //config.fb_location = CAMERA_FB_IN_PSRAM;
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.printf("Camera initialization failed!\n");
        return;
    }
}
void updateMax(int *maxDiff, char *direction, int newDiff, char newDirection) {
    if (newDiff > *maxDiff) {
        *maxDiff = newDiff;
        *direction = newDirection;
    }
}
char findMaxDirection(int leftDiff, int rightDiff, int centerDiff, int upDiff, int downDiff) {
    char direction = 'N'; // Default: No movement
    int maxDiff = 0;

    updateMax(&maxDiff, &direction, upDiff, 'U');
    updateMax(&maxDiff, &direction, downDiff, 'D');
    updateMax(&maxDiff, &direction, leftDiff, 'L');
    updateMax(&maxDiff, &direction, rightDiff, 'R');
    updateMax(&maxDiff, &direction, centerDiff, 'C');
    return direction;
}
char detectMotion(uint8_t* frame, int lenght) {
    if (!prevFrame) {
        prevFrame = (uint8_t*) ps_malloc(lenght);
        if (!prevFrame) {
            Serial.println("Errore: Memoria insufficiente per prevFrame!");
            return 'N';  // Nessun movimento
        }
        memcpy(prevFrame, frame, lenght);
        return 'N';  // Nessun movimento alla prima esecuzione
    }

    int leftDiff = 0, rightDiff = 0, centerDiff = 0;
    int upDiff = 0, downDiff = 0;
    int motionPixels = 0;

    for (int i = 0; i < lenght; i++) {
        int diff = abs(frame[i] - prevFrame[i]);

        if (diff > MOTION_THRESHOLD) {
            motionPixels++;
            int x = i % FRAME_WIDTH;
            int y = i / FRAME_WIDTH;

            // Localizzazione movimento
            if (x < FRAME_WIDTH / 3) leftDiff++;
            else if (x > 2 * FRAME_WIDTH / 3) rightDiff++;
            else centerDiff++;

            if (y < FRAME_HEIGHT / 3) upDiff++;
            else if (y > 2 * FRAME_HEIGHT / 3) downDiff++;
        }
    }

    // Salva il frame attuale come nuovo frame precedente
    //memcpy(prevFrame, frame, lenght);
    return findMaxDirection(leftDiff, rightDiff, centerDiff, upDiff, downDiff);
}
void sendPhotoTelegram(uint8_t* frame, size_t len) {
    if(WiFi.status() != WL_CONNECTED){
        Serial.println("WiFi disconnected");
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
    Serial.println("📤 Sending photo to Telegram...");

    WiFiClientSecure client;
    //client.setInsecure();  // Ignore SSL verification (Telegram requires HTTPS)
    client.setCACert(telegram_root_ca);
    // Construct Telegram API URL
    String url = "https://api.telegram.org/bot" + String(botToken) + "/sendPhoto";
    
    if (!client.connect("api.telegram.org", 443)) { // Connect to Telegram server
        Serial.println("❌ Connection to Telegram failed!");
        return;
    }

    // Define boundary for multipart request
    String boundary = "----ESP32Boundary";
    
    // Construct the initial HTTP headers
    String payloadHeader = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
                           String(chatID) + "\r\n" +
                           "--" + boundary + "\r\n" +
                           "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n"
                           "Content-Type: image/jpeg\r\n\r\n";

    // Closing boundary
    String payloadFooter = "\r\n--" + boundary + "--\r\n";
    // Calculate total content length
    int contentLength = payloadHeader.length() + len + payloadFooter.length();

    // Send HTTP POST request
    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(contentLength));
    client.println();  // Empty line to indicate end of headers

    // Send the headers and image data
    client.print(payloadHeader);  // Send initial form-data headers
    client.write(frame, len);  // Send the raw image data
    client.print(payloadFooter);  // Send the closing boundary

    // Wait for response from Telegram
    Serial.println("📨 Waiting for response...");
    while (client.connected()) {
        String response = client.readStringUntil('\n');
        //Serial.println(response);
        if (response == "\r") break;  // Headers end at a blank line
    }

    Serial.println("✅ Photo sent successfully!");
    client.stop();  // Close connection
}
