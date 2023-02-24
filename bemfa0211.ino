/*
 *  Project：魔方灯
 *  Created on: 2022-07-09
 *  Author: 奇客物志 (zhang zhuo)
 *  V2.0
 */
 
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>
#include <FastLED.h>
#include <Arduino.h>
#include <arduino_homekit_server.h>


#define LOG_D(fmt, ...)   printf_P(PSTR(fmt "\n") , ##__VA_ARGS__);

//LED引脚
#define PIN            12
// 数量
#define NUMPIXELS      24

int keyPin = 14;//按键连接到开发板的14脚用于检测按键的状态
int inputValue = 0;

bool received_sat = false;
bool received_hue = false;

bool is_on = false;
float current_brightness =  100.0;
float current_sat = 0.0;
float current_hue = 0.0;
int rgb_colors[3];

int mode1 = 0;
// 
bool isshow = false;
// 
long touchDownTime = 0, touchUpTime = 0, firstTouchTime = 0;
// 
bool isOne = 0, isDouble = 0;
// 按键状态 0：无任何操作，1：单机  2: 双击   3：长按
int touchStatus = 0;

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
CRGB leds[NUMPIXELS];//////////////FASTLED
int delayval = 1000; // delay for half a second

uint8_t colorR = 150;
uint8_t colorG = 0;
uint8_t colorB = 150;
uint8_t colorW = 200;
uint8_t _colorW = 0;
int interval = 25;    //灯效节奏
int brt_set = 0;      //未调整亮度时呼吸灯初始亮度

bool wifioff = true;

#define TIMER_MS 5000
unsigned long last_change = 0;
unsigned long current_time = 0;


#define server_ip "bemfa.com"        //巴法云服务器地址默认即可
#define server_port "8344"           //服务器端口，tcp创客云端口8344

//********************需要修改的部分*******************//


String UID = "15f0049113df440706219fac94578402";  //用户私钥，可在控制台获取,修改为自己的UID
String TOPIC = "lampcube001";                     //主题名字，可在控制台新建
//const int LED_Pin = D2;                         //单片机LED引脚值，D2是NodeMcu引脚命名方式，其他esp8266型号将D2改为自己的引脚                         
String upUrl = "http://bin.bemfa.com/b/3BcMTVmMDA0OTExM2RmNDQwNzA2MjE5ZmFjOTQ1Nzg0MDI=lampcube001.bin";//固件链接，在巴法云控制台复制、粘贴到这里即可

//**************************************************//
//最大字节数
#define MAX_PACKETSIZE 512
//设置心跳值30s
#define KEEPALIVEATIME 30*1000
//tcp客户端相关初始化，默认即可
WiFiClient TCPclient;
String TcpClient_Buff = "";//初始化字符串，用于接收服务器发来的数据
unsigned int TcpClient_BuffIndex = 0;
unsigned long TcpClient_preTick = 0;
unsigned long preHeartTick = 0;//心跳
unsigned long preTCPStartTick = 0;//连接
bool preTCPConnected = false;
//相关函数初始化
//连接WIFI
void doWiFiTick();
void startSTA();

//TCP初始化连接
void doTCPClientTick();
void startTCPClient();
void sendtoTCPServer(String p);

//led控制函数，具体函数内容见下方
void turnOnLed();
void turnOffLed();



/*
  *发送数据到TCP服务器
 */
void sendtoTCPServer(String p){
  if (!TCPclient.connected()) 
  {
    Serial.println("Client is not readly");
    return;
  }
  TCPclient.print(p);
   preHeartTick = millis();//心跳计时开始，需要每隔60秒发送一次数据
}


/*
  *初始化和服务器建立连接 :style="value.online?'订阅设备在线':'无订阅设备'"  color:#9A9A9A;
*/
void startTCPClient(){
  if(TCPclient.connect(server_ip, atoi(server_port))){
    Serial.print("\nConnected to server:");
    Serial.printf("%s:%d\r\n",server_ip,atoi(server_port));
    
    String tcpTemp="";  //初始化字符串
    tcpTemp = "cmd=1&uid="+UID+"&topic="+TOPIC+"\r\n"; //构建订阅指令
    sendtoTCPServer(tcpTemp); //发送订阅指令
    tcpTemp="";//清空
    /*
     //如果需要订阅多个主题，可再次发送订阅指令
      tcpTemp = "cmd=1&uid="+UID+"&topic="+主题2+"\r\n"; //构建订阅指令
      sendtoTCPServer(tcpTemp); //发送订阅指令
      tcpTemp="";//清空
     */
    
    preTCPConnected = true;

    TCPclient.setNoDelay(true);
  }
  else{
    Serial.print("Failed connected to server:");
    Serial.println(server_ip);
    TCPclient.stop();
    preTCPConnected = false;
  }
  preTCPStartTick = millis();
}

/**
 * 固件升级函数
 * 在需要升级的地方，加上这个函数即可，例如setup中加的updateBin(); 
 * 原理：通过http请求获取远程固件，实现升级
 */
void updateBin(){
  Serial.println("start update");    
  WiFiClient UpdateClient;

  ESPhttpUpdate.onStart(update_started);//当升级开始时
  ESPhttpUpdate.onEnd(update_finished); //当升级结束时
  ESPhttpUpdate.onProgress(update_progress); //当升级中
  ESPhttpUpdate.onError(update_error); //当升级失败时
  
  t_httpUpdate_return ret = ESPhttpUpdate.update(UpdateClient, upUrl);
  switch(ret) {
    case HTTP_UPDATE_FAILED:      //当升级失败
        Serial.println("[update] Update failed.");
        break;
    case HTTP_UPDATE_NO_UPDATES:  //当无升级
        Serial.println("[update] Update no Update.");
        break;
    case HTTP_UPDATE_OK:         //当升级成功
        Serial.println("[update] Update ok.");
        break;
  }
}
/*
  *检查数据，发送心跳
*/
void doTCPClientTick(){
 //检查是否断开，断开后重连
   if(WiFi.status() != WL_CONNECTED) return;
  if (!TCPclient.connected()) {//断开重连
  if(preTCPConnected == true){
    preTCPConnected = false;
    preTCPStartTick = millis();
    Serial.println();
    Serial.println("TCP Client disconnected.");
    TCPclient.stop();
  }
  else if(millis() - preTCPStartTick > 1*1000)//重新连接
    startTCPClient();
  }
  else
  {
    if (TCPclient.available()) {//收数据
      char c =TCPclient.read();
      TcpClient_Buff +=c;
      TcpClient_BuffIndex++;
      TcpClient_preTick = millis();
      
      if(TcpClient_BuffIndex>=MAX_PACKETSIZE - 1){
        TcpClient_BuffIndex = MAX_PACKETSIZE-2;
        TcpClient_preTick = TcpClient_preTick - 200;
      }
    }
    if(millis() - preHeartTick >= KEEPALIVEATIME){//保持心跳
      preHeartTick = millis();
      Serial.println("--Keep alive:");
      sendtoTCPServer("ping\r\n"); //发送心跳，指令需\r\n结尾，详见接入文档介绍
    }
  }
  if((TcpClient_Buff.length() >= 1) && (millis() - TcpClient_preTick>=200))
  {
    TCPclient.flush();
    Serial.print("Rev string: ");
    TcpClient_Buff.trim(); //去掉首位空格
    Serial.println(TcpClient_Buff); //打印接收到的消息
    String getTopic = "";
    String getMsg = "";
    if(TcpClient_Buff.length() > 15){//注意TcpClient_Buff只是个字符串，在上面开头做了初始化 String TcpClient_Buff = "";
          //此时会收到推送的指令，指令大概为 cmd=2&uid=xxx&topic=light002&msg=off
          int topicIndex = TcpClient_Buff.indexOf("&topic=")+7; //c语言字符串查找，查找&topic=位置，并移动7位，不懂的可百度c语言字符串查找
          int msgIndex = TcpClient_Buff.indexOf("&msg=");//c语言字符串查找，查找&msg=位置
          getTopic = TcpClient_Buff.substring(topicIndex,msgIndex);//c语言字符串截取，截取到topic,不懂的可百度c语言字符串截取
          getMsg = TcpClient_Buff.substring(msgIndex+5);//c语言字符串截取，截取到消息
          Serial.print("topic:------");
          Serial.println(getTopic); //打印截取到的主题值
          Serial.print("msg:--------");
          Serial.println(getMsg);   //打印截取到的消息值
   }
   if(getMsg  == "on"){       //如果收到指令on==打开灯
     turnOnLed();
   }else if(getMsg == "off"){ //如果收到指令off==关闭灯
      turnOffLed();
    }else if(getMsg == "update"){  //如果收到指令update
      updateBin();//执行升级函数
      pixels.setPixelColor(0, pixels.Color(0,0,255));//升级过程中第一个灯闪烁
      pixels.show();
      delay(50);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
      pixels.setPixelColor(0, pixels.Color(0,0,255));
      pixels.show();
      delay(50);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
    }

   TcpClient_Buff="";
   TcpClient_BuffIndex = 0;
  }
}

//当升级开始时，打印日志
void update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
}

//当升级结束时，打印日志
void update_finished() {
  Serial.println("CALLBACK:  HTTP update process finished");
  pixels.setPixelColor(0, pixels.Color(0,255,0));
      pixels.show();
      delay(50);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
      pixels.setPixelColor(0, pixels.Color(0,255,0));
      pixels.show();
      delay(50);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
}

//当升级中，打印日志
void update_progress(int cur, int total) {
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
  pixels.setPixelColor(0, pixels.Color(255,0,0));
  pixels.show();
  delay(50);
  pixels.setPixelColor(0, pixels.Color(0,0,0));
  pixels.show();
}

//当升级失败时，打印日志
void update_error(int err) {
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}
/*
  *初始化wifi连接
*/
void startSTA(){
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
//  WiFi.begin(wifi_name, wifi_password);
  WiFiManager wm;

  bool res;

  res = wm.autoConnect("魔方灯-娟子姐专用版", "12345678"); // password protected ap

  if (!res) {
    Serial.println("Failed to connect");
    // ESP.restart();
  }
  else {
    Serial.println("connected...成功 ！)");
    pixels.setPixelColor(0, pixels.Color(255,0,255));
      pixels.show();
      delay(100);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
      delay(100);
      pixels.setPixelColor(0, pixels.Color(255,0,255));
      pixels.show();
      delay(100);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
  }
}



/**************************************************************************
                                 WIFI
***************************************************************************/
/*
  WiFiTick
  检查是否需要初始化WiFi
  检查WiFi是否连接上，若连接成功启动TCP Client
  控制指示灯
*/
void doWiFiTick(){
  static bool startSTAFlag = false;
  static bool taskStarted = false;
  static uint32_t lastWiFiCheckTick = 0;

  if (!startSTAFlag) {
    startSTAFlag = true;
    startSTA();
    my_homekit_setup();    //homekit初始化
  }

  //未连接1s重连
  if ( WiFi.status() != WL_CONNECTED ) {
    if (millis() - lastWiFiCheckTick > 1000) {
      lastWiFiCheckTick = millis();
    }
  }
  //连接成功建立
  else {
    if (taskStarted == false) {
      taskStarted = true;
      Serial.print("\r\nGet IP Address: ");
      Serial.println(WiFi.localIP());
      startTCPClient();
//      my_homekit_setup();    //homekit初始化
    }
  }
}
//打开灯泡
void turnOnLed(){
  Serial.println("Turn ON");
  for(int i=0;i<4;i++){
    for(int a=0;a<255;a++){
          pixels.setPixelColor(i, pixels.Color(0,a,0));
          pixels.show(); 
          delay(2);
     }
    for(int a=255;a>=0;a--){
          pixels.setPixelColor(i, pixels.Color(0,a,0));
          pixels.show(); 
          delay(2);
     }
}
}
//关闭灯泡
void turnOffLed(){
  Serial.println("Turn OFF");
  for(int i=0;i<NUMPIXELS;i++){
    pixels.setPixelColor(i, pixels.Color(0,0,0));
  }
   pixels.show(); 
   delay(100);
}







/**************************************************************************
                                 LED效果
***************************************************************************/
void closeLed() {    //关灯
  for(int i=0;i<NUMPIXELS;i++){
    pixels.setPixelColor(i, pixels.Color(0,0,0));
  }
   pixels.show(); 
   delay(delayval); 
}
void colorLed() {    //彩色led
   for(int i=0;i<NUMPIXELS;i++){
    pixels.setPixelColor(i, pixels.Color(random(255),random(255),random(255)));
    pixels.show(); 
   delay(delayval/4);
  }
}
void breathLed() {   //呼吸灯1
  for(int i=0;i<NUMPIXELS;i=i+4){
    for(int a=0;a<255;a++){
      if(i==0){
          pixels.setPixelColor(i, pixels.Color(0,a,0));
          pixels.setPixelColor(i+1, pixels.Color(0,a,0));
          pixels.setPixelColor(i+2, pixels.Color(0,a,0));
          pixels.setPixelColor(i+3, pixels.Color(0,a,0));
          pixels.show(); 
          delay(8);
          }
       else if(i==4){
          pixels.setPixelColor(i, pixels.Color(a,a,0));
          pixels.setPixelColor(i+1, pixels.Color(a,a,0));
          pixels.setPixelColor(i+2, pixels.Color(a,a,0));
          pixels.setPixelColor(i+3, pixels.Color(a,a,0));
          pixels.show(); 
          delay(8);
        }
        else if(i==8){
          pixels.setPixelColor(i, pixels.Color(a,a,a));
          pixels.setPixelColor(i+1, pixels.Color(a,a,a));
          pixels.setPixelColor(i+2, pixels.Color(a,a,a));
          pixels.setPixelColor(i+3, pixels.Color(a,a,a));
          pixels.show(); 
          delay(8);
        }
        else if(i==12){
          pixels.setPixelColor(i, pixels.Color(a,0,0));
          pixels.setPixelColor(i+1, pixels.Color(a,0,0));
          pixels.setPixelColor(i+2, pixels.Color(a,0,0));
          pixels.setPixelColor(i+3, pixels.Color(a,0,0));
          pixels.show(); 
          delay(8);
        }
        else if(i==16){
          pixels.setPixelColor(i, pixels.Color(0,a,a));
          pixels.setPixelColor(i+1, pixels.Color(0,a,a));
          pixels.setPixelColor(i+2, pixels.Color(0,a,a));
          pixels.setPixelColor(i+3, pixels.Color(0,a,a));
          pixels.show(); 
          delay(8);
        }
        else if(i==20){
          pixels.setPixelColor(i, pixels.Color(0,0,a));
          pixels.setPixelColor(i+1, pixels.Color(0,0,a));
          pixels.setPixelColor(i+2, pixels.Color(0,0,a));
          pixels.setPixelColor(i+3, pixels.Color(0,0,a));
          pixels.show(); 
          delay(8);
        }        
     }
    for(int a=255;a>=0;a--){
          if(i==0){
          pixels.setPixelColor(i, pixels.Color(0,a,0));
          pixels.setPixelColor(i+1, pixels.Color(0,a,0));
          pixels.setPixelColor(i+2, pixels.Color(0,a,0));
          pixels.setPixelColor(i+3, pixels.Color(0,a,0));
          pixels.show(); 
          delay(8);
          }
       else if(i==4){
          pixels.setPixelColor(i, pixels.Color(a,a,0));
          pixels.setPixelColor(i+1, pixels.Color(a,a,0));
          pixels.setPixelColor(i+2, pixels.Color(a,a,0));
          pixels.setPixelColor(i+3, pixels.Color(a,a,0));
          pixels.show(); 
          delay(8);
        }
        else if(i==8){
          pixels.setPixelColor(i, pixels.Color(a,a,a));
          pixels.setPixelColor(i+1, pixels.Color(a,a,a));
          pixels.setPixelColor(i+2, pixels.Color(a,a,a));
          pixels.setPixelColor(i+3, pixels.Color(a,a,a));
          pixels.show(); 
          delay(8);
        }
        else if(i==12){
          pixels.setPixelColor(i, pixels.Color(a,0,0));
          pixels.setPixelColor(i+1, pixels.Color(a,0,0));
          pixels.setPixelColor(i+2, pixels.Color(a,0,0));
          pixels.setPixelColor(i+3, pixels.Color(a,0,0));
          pixels.show(); 
          delay(8);
        }
        else if(i==16){
          pixels.setPixelColor(i, pixels.Color(0,a,a));
          pixels.setPixelColor(i+1, pixels.Color(0,a,a));
          pixels.setPixelColor(i+2, pixels.Color(0,a,a));
          pixels.setPixelColor(i+3, pixels.Color(0,a,a));
          pixels.show(); 
          delay(8);
        }
        else if(i==20){
          pixels.setPixelColor(i, pixels.Color(0,0,a));
          pixels.setPixelColor(i+1, pixels.Color(0,0,a));
          pixels.setPixelColor(i+2, pixels.Color(0,0,a));
          pixels.setPixelColor(i+3, pixels.Color(0,0,a));
          pixels.show(); 
          delay(8);
        }
     }
  }
   delay(delayval);
}
//
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

//彩虹
void rainbow(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256; j++) {
    for(i=0; i<NUMPIXELS; i++) {
      pixels.setPixelColor(i, Wheel((i+j) & 255));
    }
    pixels.show();
    delay(wait);
  }
}
//依次点亮
 void onetoone(){
  for(int i=0;i<NUMPIXELS;i++){

    // pixels.Color takes RGB values, from 0,0,0 up to 255,255,255
    pixels.setPixelColor(i, pixels.Color(0,150,0)); // mode1rately bright green color.

    pixels.show(); // This sends the updated pixel color to the hardware.

    delay(delayval/4); // Delay for a period of time (in milliseconds).

  }
 }

//
void colorWipe(uint32_t c, uint8_t wait) {
  for (uint16_t i = 0; i < pixels.numPixels(); i++) {
    
      pixels.setPixelColor(i, c);
      pixels.show();
      delay(wait);
    }
}

void colorScan()//跑马灯
{
  colorWipe(pixels.Color(255, 0, 0), interval); // Red
  colorWipe(pixels.Color(0, 255, 0), interval); // Green
  colorWipe(pixels.Color(0, 0, 255), interval);
  colorWipe(pixels.Color(0, 255, 255), interval);
  colorWipe(pixels.Color(255, 0, 255), interval);
  colorWipe(pixels.Color(255, 0, 0), interval);

}

///////////////////////////////////FASTLEDS的示例效果
void fadeall() {
  for (int i = 0; i < NUMPIXELS; i++) {
    leds[i].nscale8(250);
  }
}

//潮汐
void cylon() {
  static uint8_t hue = 0;
  for (int i = 0; i < NUMPIXELS; i++) {
    
      leds[i] = CHSV(hue++, 255, 255);
      FastLED.show();
      fadeall();
      delay(interval*2);
    
  }

  for (int i = (NUMPIXELS) - 1; i >= 0; i--) {
    
      leds[i] = CHSV(hue++, 255, 255);
      FastLED.show();
      fadeall();
      delay(interval*2);
    }
}


void pixelShow()
{


  if ( colorW < _colorW)
  {
    for (int b = _colorW; b > colorW - 1; b--)
    {
      pixels.setBrightness(b);
      for (int i = 0; i < NUMPIXELS; i++)
      {
        pixels.setPixelColor(i, colorR, colorG, colorB);

      }
      pixels.show();
      delay(5);
    }
  }
  else if ( colorW > _colorW)
  {
    for (int b = _colorW; b < colorW + 1; b++)
    {
      pixels.setBrightness(b);
      for (int i = 0; i < NUMPIXELS; i++)
      {
        pixels.setPixelColor(i, colorR, colorG, colorB);
      }
      pixels.show();
      delay(5);
    }
  }


  else
  {
    pixels.setBrightness(colorW);
    for (int i = 0; i < NUMPIXELS; i++)
    {
      pixels.setPixelColor(i, colorR, colorG, colorB);

    }
    pixels.show();
  }
  _colorW = colorW;

}
void breath()//呼吸灯2
{

  for (int brt = brt_set; brt < 255 ; brt++) {

      colorW = brt;
      pixelShow();
      delay(interval/2);
    
  }
  for (int brt = 255; brt > brt_set + 1; brt--) {
    
      colorW = brt;
      pixelShow();
      delay(interval/2);

  }
}


//void pride()
//{
//  static uint16_t sPseudotime = 0;
//  static uint16_t sLastMillis = 0;
//  static uint16_t sHue16 = 0;
//
//  uint8_t sat8 = beatsin88( 87, 220, 250);
//  uint8_t brightdepth = beatsin88( 341, 96, 224);
//  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));
//  uint8_t msmultiplier = beatsin88(147, 23, 60);
//
//  uint16_t hue16 = sHue16;//gHue * 256;
//  uint16_t hueinc16 = beatsin88(113, 1, 3000);
//
//  uint16_t ms = millis();
//  uint16_t deltams = ms - sLastMillis ;
//  sLastMillis  = ms;
//  sPseudotime += deltams * msmultiplier;
//  sHue16 += deltams * beatsin88( 400, 5, 9);
//  uint16_t brightnesstheta16 = sPseudotime;
//
//  for ( uint16_t i = 0 ; i < NUMPIXELS; i++) {
//    hue16 += hueinc16;
//    uint8_t hue8 = hue16 / 256;
//
//    brightnesstheta16  += brightnessthetainc16;
//    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;
//
//    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
//    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
//    bri8 += (255 - brightdepth);
//
//    CRGB newcolor = CHSV( hue8, sat8, bri8);
//
//    uint16_t pixelnumber = i;
//    pixelnumber = (NUMPIXELS - 1) - pixelnumber;
//
//    nblend( leds[pixelnumber], newcolor, 64);
//  }
//  FastLED.show();
//}
//
//
//void chasing()
//{
//  for (int i = 0; i < NUMPIXELS; i++) {
//    FastLED.clear();
//    if (i - 3 > 0)
//    {
//      leds[i - 1] = CRGB(int(colorR * 0.5), int(colorG * 0.5), int(colorB * 0.5));
//      leds[i - 2] =  CRGB(int(colorR * 0.2), int(colorG * 0.2), int(colorB * 0.2));
//    }
//    leds[i] = CRGB(colorR, colorG, colorB);
//    FastLED.show();
//    delay(interval);
//  }
//}


void powermode()
{
  // 单击判断逻辑
  if (isOne && millis() - firstTouchTime > 150)
  {
    isDouble = 0;
    isOne = 0;
    touchStatus = 1;
  }
  if (touchStatus == 1)
  {
    isshow = (isshow == 0) ? 1 : 0;
  }
  else if (touchStatus == 2)
  {
    mode1 = (mode1 == 0) ? 1 : 0;
  }
  // 如果是长按，且按下到按起时间超过五秒，则重启系统
  else if (touchStatus == 3 && (touchUpTime - touchDownTime) >= 3000)
  {
    mode1 = 3;
  }
  // 重置按键状态
  touchStatus = 0;
}

/**
 * 显示模式0
 */
void showMode0()
{
//  digitalWrite(LED_BUILTIN, isshow ? LOW : HIGH);
  if(isshow ==1){
//    closeLed();//关灯
    Serial.println("模式1   单击 可退出");
    Serial.println("呼吸灯1");
    breathLed();//呼吸灯
    closeLed();
    current_time = millis();
    Serial.println(current_time);
    if (current_time>1800000){
//      Serial.println(true);
      Serial.println("我要进行深度睡眠了");
      closeLed();
      pinMode(13, OUTPUT);     
      digitalWrite(13, LOW);
      ESP.deepSleep(0);
    }
  }
  else{  //正常流程
    switch(random(7))
    {
      case 0: 
        Serial.println("依次点亮");
        onetoone();
        closeLed();
        break;
      case 1: 
        Serial.println("彩灯");
        colorLed();//彩灯
        closeLed();
        break;
      case 2: 
        Serial.println("呼吸灯1");
        breathLed();//呼吸灯
        closeLed();
        break;
      case 3: 
        Serial.println("彩虹灯");
        rainbow(interval);//彩虹灯
        closeLed();
        break;
      case 4: 
        Serial.println("跑马灯");
        colorScan();//跑马灯
        closeLed();
        break;
      case 5: 
        Serial.println("潮汐");
        cylon();//潮汐
        closeLed();
        break;
      case 6: 
        Serial.println("呼吸灯2");
        breath();//呼吸灯2
        closeLed();
        break;
       default:
        Serial.println("呼吸灯2-1");
        breath();//呼吸灯2
        closeLed();
    }
//    Serial.println("依次点亮");
//    onetoone();
//    closeLed();//关灯
//    Serial.println("彩灯");
//    colorLed();//彩灯
//    closeLed();
//    Serial.println("呼吸灯1");
//    breathLed();//呼吸灯
//    closeLed();
//    Serial.println("彩虹灯");
//    rainbow(interval);//彩虹灯
//    closeLed();
//    Serial.println("跑马灯");
//    colorScan();//跑马灯
//    closeLed();
//    Serial.println("潮汐");
//    cylon();//潮汐
//    closeLed();
//    Serial.println("呼吸灯2");
//    breath();//呼吸灯2
//    closeLed();
//    Serial.println("模式1");
//    pride();
//    closeLed();
//    Serial.println("模式2");
//    chasing();
//    closeLed();
 
    current_time = millis();
    Serial.println(current_time);
    if (current_time>120000){
      Serial.println(true);
      Serial.println("我要进行深度睡眠了");
      closeLed();
      pinMode(13, OUTPUT);     
      digitalWrite(13, LOW); 
//     pinMode(2, INPUT);     
//     digitalWrite(2, HIGH); 
//     pinMode(15, INPUT);     
//     digitalWrite(15, HIGH); 
//     pinMode(14, INPUT);     
//     digitalWrite(14, HIGH);
      ESP.deepSleep(0);
    }
    
  }
}

/**
 * 显示模式1
 */
void showMode1()
{
  if (isshow == 1)
  {
//    closeLed();//关灯
//    lightning();

    Serial.println("模式3  单击后双击 或 双击后单击 可退出");
    Serial.println("跑马灯");
    colorScan();//跑马灯
//    closeLed();
    current_time = millis();
    Serial.println(current_time);
    if (current_time>900000){
//      Serial.println(true);
      Serial.println("我要进行深度睡眠了");
      closeLed();
      pinMode(13, OUTPUT);     
      digitalWrite(13, LOW);
      ESP.deepSleep(0);
    }
  }
  else
  {
//    closeLed();//关灯
//    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("模式4  双击  可退出");
    Serial.println("呼吸灯2");
    breath();//呼吸灯2
//    closeLed();
    current_time = millis();
    Serial.println(current_time);
    if (current_time>900000){
//      Serial.println(true);
      Serial.println("我要进行深度睡眠了");
      closeLed();
      pinMode(13, OUTPUT);     
      digitalWrite(13, LOW);
      ESP.deepSleep(0);
    }
  }
}
/**
 * 显示模式2
 */
void showMode2()
{
  if (isshow == 1)
  {
//    closeLed();//关灯
//    lightning();
//    Serial.println("模式5  长按后单击  不可退出");
//    doWiFiTick();
    Serial.println("我要进行深度睡眠了");
    closeLed();
    pinMode(13, OUTPUT);     
    digitalWrite(13, LOW);
    ESP.deepSleep(0);
  }
  else
  {
//    closeLed();//关灯
//    digitalWrite(LED_BUILTIN, LOW);
//    Serial.println("模式6   长按  不可退出");

    doWiFiTick();
    my_homekit_loop();
    doTCPClientTick();
    current_time = millis();
    Serial.println(current_time);
    if (current_time>900000){
//      Serial.println(true);
      Serial.println("我要进行深度睡眠了");
      closeLed();
      pinMode(13, OUTPUT);     
      digitalWrite(13, LOW);
      ESP.deepSleep(0);
    }
//    delay(10);
  } 
}

void setupledmode(){             //初始化时三灯交替闪烁，防止意外情况无法进入远程升级模式
  LEDS.showColor(CRGB(150, 0, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 150, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 0, 150));
  delay(500);
  LEDS.showColor(CRGB(150, 0, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 150, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 0, 150));
  delay(500);
  LEDS.showColor(CRGB(150, 0, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 150, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 0, 150));
  delay(500);
  LEDS.showColor(CRGB(150, 0, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 150, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 0, 150));
  delay(500);
  LEDS.showColor(CRGB(150, 0, 0));
  delay(500);
  LEDS.showColor(CRGB(0, 0, 0));
}

// 
void setup() {
  Serial.begin(115200);
  attachInterrupt(digitalPinToInterrupt(keyPin), touchDownInterrupt, FALLING);
  pinMode(13, OUTPUT);     
  digitalWrite(13, HIGH); 
  LEDS.addLeds<WS2812, PIN, GRB>(leds, NUMPIXELS);
  LEDS.setBrightness(50);
  delay(500);
  setupledmode();   //初始化时三灯交替闪烁，防止意外情况无法进入远程升级模式
  pixels.begin(); // This initializes the NeoPixel library.
//  attachInterrupt(digitalPinToInterrupt(keyPin), touchDownInterrupt, FALLING);
//  my_homekit_setup();    //homekit初始化
  Serial.println("system is start");
  // pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output - di ESP12F
  // digitalWrite(LED_BUILTIN, LOW); 
//   pinMode(0, INPUT);     
//   digitalWrite(0, HIGH); 
//   pinMode(2, INPUT);     
//   digitalWrite(2, HIGH); 
//   pinMode(15, INPUT);     
//   digitalWrite(15, HIGH); 
//   pinMode(14, INPUT);     
//   digitalWrite(14, HIGH);
}

//循环
void loop() {
  powermode();
  if (mode1 == 0)
  {
    // 模式0
    showMode0();
  }
  else if (mode1 == 1)
  {
    // 模式1
    showMode1();
  }
  else if (mode1 == 3)
  {
    showMode2();
  }
  

  
}

/**
 * 按键按下去的中断
 * 
 */
ICACHE_RAM_ATTR void touchDownInterrupt()
{
//  Serial.println("按下1");
  // 如果已经按下过一次，且第二次按下的时间不超过150ms则表示双击
  if (isOne && millis() - firstTouchTime <= 150)
  {
    isOne = 0;
    isDouble = 1;
    touchStatus = 2;
//    Serial.println("按下2");
  }
  // 记录按下的时间
  touchDownTime = millis();
  attachInterrupt(digitalPinToInterrupt(keyPin), touchUpInterrupt, RISING);
}

/**
 * 按键弹起来的中断
 * 
 */
ICACHE_RAM_ATTR void touchUpInterrupt()
{
  // 记录按键按起时间
  touchUpTime = millis();
  // 按键按下到按起时间超过700毫秒则表示处于长按状态
  if ((touchUpTime - touchDownTime) > 700)
  {
    touchStatus = 3;
    Serial.println("长按");
  }
  else if (isDouble)
  {
    isDouble = 0;
    Serial.println("双击");
  }
  else
  {
    isOne = 1;
    firstTouchTime = millis();
    Serial.println("短按");
  }
  attachInterrupt(digitalPinToInterrupt(keyPin), touchDownInterrupt, FALLING);
}

//==============================
// HomeKit setup and loop
//==============================

// access your HomeKit characteristics defined in my_accessory.c

extern "C" homekit_server_config_t accessory_config;
extern "C" homekit_characteristic_t cha_on;
extern "C" homekit_characteristic_t cha_bright;
extern "C" homekit_characteristic_t cha_sat;
extern "C" homekit_characteristic_t cha_hue;

static uint32_t next_heap_millis = 0;
void my_homekit_setup() {
//  homekit_storage_reset();  //homekit重置连接
 for(int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, 0, 0, 0);
  }
  pixels.show();
  delay(1000);
  rgb_colors[0] = 255;
  rgb_colors[1] = 255;
  rgb_colors[2] = 255;
  
  cha_on.setter = set_on;
  cha_bright.setter = set_bright;
  cha_sat.setter = set_sat;
  cha_hue.setter = set_hue;
  
  arduino_homekit_setup(&accessory_config);

}

void my_homekit_loop() {
  arduino_homekit_loop();
  const uint32_t t = millis();
  if (t > next_heap_millis) {
    // show heap info every 5 seconds
    next_heap_millis = t + 5 * 1000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
        ESP.getFreeHeap(), arduino_homekit_connected_clients_count());

  }
}

void set_on(const homekit_value_t v) {
    bool on = v.bool_value;
    cha_on.value.bool_value = on; //sync the value

    if(on) {
        is_on = true;
        Serial.println("On");
    } else  {
        is_on = false;
        Serial.println("Off");
    }

    updateColor();
}

void set_hue(const homekit_value_t v) {
    Serial.println("set_hue");
    float hue = v.float_value;
    cha_hue.value.float_value = hue; //sync the value

    current_hue = hue;
    received_hue = true;
    
    updateColor();
}

void set_sat(const homekit_value_t v) {
    Serial.println("set_sat");
    float sat = v.float_value;
    cha_sat.value.float_value = sat; //sync the value

    current_sat = sat;
    received_sat = true;
    
    updateColor();

}

void set_bright(const homekit_value_t v) {
    Serial.println("set_bright");
    int bright = v.int_value;
    cha_bright.value.int_value = bright; //sync the value

    current_brightness = bright;

    updateColor();
}

void updateColor()
{
  if(is_on)
  {
   
      if(received_hue && received_sat)
      {
        HSV2RGB(current_hue, current_sat, current_brightness);
        received_hue = false;
        received_sat = false;
      }
      
      int b = map(current_brightness,0, 100,75, 255);
      Serial.println(b);
  
      pixels.setBrightness(b);
      for(int i = 0; i < NUMPIXELS; i++)
      {
  
        pixels.setPixelColor(i, pixels.Color(rgb_colors[0],rgb_colors[1],rgb_colors[2]));
  
      }
      pixels.show();

    }
  else if(!is_on) //lamp - switch to off
  {
      Serial.println("is_on == false");
      pixels.setBrightness(0);
      for(int i = 0; i < NUMPIXELS; i++)
      {
        pixels.setPixelColor(i, pixels.Color(0,0,0));
      }
      pixels.show();
  }
}

void HSV2RGB(float h,float s,float v) {

  int i;
  float m, n, f;

  s/=100;
  v/=100;

  if(s==0){
    rgb_colors[0]=rgb_colors[1]=rgb_colors[2]=round(v*255);
    return;
  }

  h/=60;
  i=floor(h);
  f=h-i;

  if(!(i&1)){
    f=1-f;
  }

  m=v*(1-s);
  n=v*(1-s*f);

  switch (i) {

    case 0: case 6:
      rgb_colors[0]=round(v*255);
      rgb_colors[1]=round(n*255);
      rgb_colors[2]=round(m*255);
    break;

    case 1:
      rgb_colors[0]=round(n*255);
      rgb_colors[1]=round(v*255);
      rgb_colors[2]=round(m*255);
    break;

    case 2:
      rgb_colors[0]=round(m*255);
      rgb_colors[1]=round(v*255);
      rgb_colors[2]=round(n*255);
    break;

    case 3:
      rgb_colors[0]=round(m*255);
      rgb_colors[1]=round(n*255);
      rgb_colors[2]=round(v*255);
    break;

    case 4:
      rgb_colors[0]=round(n*255);
      rgb_colors[1]=round(m*255);
      rgb_colors[2]=round(v*255);
    break;

    case 5:
      rgb_colors[0]=round(v*255);
      rgb_colors[1]=round(m*255);
      rgb_colors[2]=round(n*255);
    break;
  }
}
