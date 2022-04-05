/**
 * 2022-03-10
 * 新增虚拟串口，连接esp12f
 * 新增模式，MODE_BUDDING 20含苞待放；MODE_ONLYLIGHT 21仅照明
 * 
 * 2022-03-11
 * 上送mode、postion
 */

//是否打开调试信息
#define DEBUG_FLAG 

//构建数据json
#include <ArduinoJson.h>
#include "Ticker.h"
void upModeToMQTT();
void upPosToMQTT(byte index, byte data);
Ticker timer1(upModeToMQTT, 2000);

/**
 * 2022-03-14
 * RTTTL
 * 不可用，与SoftPWM冲突
 */

//控制花开花合
#include <Servo.h>
//控制花蕊色彩
#include <FastLED.h>
//控制花瓣灯亮度
#include "SoftPWM.h"

#define TOUCH_SENSOR_PIN 2

#define NUM_LEDS 7
#define DATA_PIN 11
CRGB leds[NUM_LEDS];

Servo myservo;
#define SERVO_PIN 9
#define SERVO_OPEN 81         //花瓣展开最大位置
#define SERVO_SAFE_MIDDLE 160
#define SERVO_CLOSED 176      //花瓣闭合最小位置

#define MODE_SLEEPING 0
#define MODE_BLOOM 3
#define MODE_BLOOMING 4
#define MODE_BLOOMED 5
#define MODE_FADE 6
#define MODE_FADING 7
#define MODE_FADED 8
#define MODE_FALLINGASLEEP 9
#define MODE_BUDDING 20
#define MODE_ONLYLIGHT 21
#define MODE_MUSIC 22
byte mode = MODE_FADED;

byte servoPosition = SERVO_SAFE_MIDDLE;//默认在中间位置
bool curState = 0;

byte petalPins[] = {3,4,5,6,7,8};
byte maxBrightness = 255;

//软串口
#include <SoftwareSerial.h>
const int virtualTXD = 13;
const int virtualRXD = 12;
SoftwareSerial arduino2esp(virtualRXD, virtualTXD);

//arduino esp交互
String comdata_str  = "";//指令文件

void setup(){
  //2022-03-10
  //初始化硬串口
  Serial.begin(115200);
  //初始化虚拟串口
  arduino2esp.begin(9600);

  //2022-03-10
  //初始化OUTPUT引脚
  for(int i = 0; i < 6; i++){
    pinMode(petalPins[i], OUTPUT);
  }
  
  pinMode(TOUCH_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_SENSOR_PIN), _touchISR, RISING);

  randomSeed(analogRead(A7));
  SoftPWMBegin();

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(maxBrightness);      
  
  myservo.attach(SERVO_PIN); 

  // 定时器
  timer1.start();

  //2022-03-14
  //定义蜂鸣器引脚模式为输出
  pinMode(BUZZER_PIN, OUTPUT);

  #ifdef DEBUG_FLAG
    Serial.println("Tulip PowerOn");
  #endif
}

int counter = 0;
byte holdon = 50;

void loop(){
  timer1.update();

  //接收来自esp的消息
  if(arduino2esp.available() > 2)//放置2防止波动
  {
    String comdata = "";
    comdata_str = "";
    while(arduino2esp.available() > 0)//循环串口是否有数据
    {
      comdata += char(arduino2esp.read());
      delay(2);
    }

    comdata_str = comdata;//如果需要处理在此处进行

    if(comdata_str != "")//指令判断
    {
      #ifdef DEBUG_FLAG
        Serial.print("GetCmd:");
        Serial.println(comdata_str);
      #endif

      if(comdata_str.equals("BLOOM"))//开花
      {
        #ifdef DEBUG_FLAG
          Serial.println(F("BLOOM"));
        #endif
        
        if(mode == MODE_SLEEPING || mode == MODE_BUDDING || mode == MODE_ONLYLIGHT)
        {
          changeMode(MODE_BLOOM);
        }
      } else if(comdata_str.equals("FADE"))//花谢
      {
        #ifdef DEBUG_FLAG
          Serial.println(F("FADE"));
        #endif
        
        if(mode == MODE_BLOOMED)
        {
          changeMode(MODE_FADE);
        }
      } else if(comdata_str.equals("BUDDING"))//含苞待放
      {
        #ifdef DEBUG_FLAG
          Serial.println(F("BUDDING"));
        #endif
        
        if(mode == MODE_SLEEPING || mode == MODE_ONLYLIGHT)//休眠的时候含苞待放
        {
          changeMode(MODE_BUDDING);
        }
      } else if(comdata_str.equals("ONLYLIGHT"))//仅照明
      {
        #ifdef DEBUG_FLAG
          Serial.println(F("ONLYLIGHT"));
        #endif
        
        if(mode == MODE_SLEEPING || mode == MODE_BUDDING)//仅当休眠的时候进行照明，花瓣正常打开时已经是照明状态
        {
          changeMode(MODE_ONLYLIGHT);
        }
      } else if(comdata_str.equals("SLEEP"))//休眠
      {
        #ifdef DEBUG_FLAG
          Serial.println(F("SLEEP"));
        #endif
        
        if(mode == MODE_BUDDING || mode == MODE_ONLYLIGHT)//当郁金香是含苞待放、仅照明时，进入休眠。对于花开、花谢不影响
        {
          changeMode(MODE_SLEEPING);
        }
      } else if(comdata_str.equals("MUSIC"))// 音乐
      {
        /**
         * 2022-03-14
         * 音乐可指定编号，或者曲谱，示例：MUSIC#1,MUSIC#2
         * 不可用
         */
         
      } else {
        //Do nothing
        #ifdef DEBUG_FLAG
          Serial.println(F("Can't identify order."));
        #endif
      }
    }
  }

  boolean done = true;
  switch (mode) {
    case MODE_BLOOM:
      prepareCrossFadeBloom();
      changeMode(MODE_BLOOMING);
      break;

    case MODE_BLOOMING:
      done = crossFade() && done;
      done = openPetals() && done;
      done = petalsBloom(counter) && done;
      if (done) {
        changeMode(MODE_BLOOMED);

        showColorLed();
      }
      break;

    case MODE_FADE:
      changeMode(MODE_FADING);
      break;

    case MODE_FADING:
      done = crossFade() && done;
      done = closePetals() && done;
      done = petalsFade(counter) && done;
      if (done) {
        changeMode(MODE_FADED);
      }
      break;

    case MODE_FADED:
      changeMode(MODE_FALLINGASLEEP);
      break;

    case MODE_FALLINGASLEEP:
      done = crossFade() && done;
      done = closePetals() && done;
      if (done) {
        changeMode(MODE_SLEEPING);

        closeColorLed();
      }      
      break;

    case MODE_BUDDING:
      budding();
      break;
    case MODE_ONLYLIGHT:
      onlylight();
      break;
  }

  counter++;
  delay(holdon);
}

/**
 * 定时任务
 * 将mode、lux数据发送到MQTT服务器
 */
void upModeToMQTT(){
  DynamicJsonDocument updataDoc(512);
  updataDoc["mode"] = mode;
  updataDoc["lux"] = analogRead(A1);
  serializeJsonPretty(updataDoc, arduino2esp);
}

//将数据发送到MQTT服务器
void upPosToMQTT(byte serverPos){
  DynamicJsonDocument updataDoc(512);
  updataDoc["pos"] = serverPos;
  serializeJsonPretty(updataDoc, arduino2esp);
}

//改变模式
void changeMode(byte newMode) {
  if (mode != newMode) {
    mode = newMode;
    counter = 0;
  }
}

//触摸按钮外部中断
void _touchISR() {
  //2022-03-10  当休眠、含苞待放、仅照明的时候均可切换到绽放状态
  if (mode == MODE_SLEEPING || mode == MODE_BUDDING || mode == MODE_ONLYLIGHT) {
    changeMode(MODE_BLOOM);
  }
  else if (mode == MODE_BLOOMED) {
    changeMode(MODE_FADE);
  }
}

//花瓣灯开
boolean petalsBloom(int j) {
  if (j < 25) {
    return false; // delay
  }
  if (j > 75) {
    return true;
  }
  int val = (j - 25) / 2;
  for (int i = 0; i < 6; i++) {
    SoftPWMSet(petalPins[i], val * 10);
  }
  return false;
}

//花瓣灯谢
boolean petalsFade(int j) {
  if (j > 51) {
    return true;
  }
  for (int i = 0; i < 6; i++) {
    SoftPWMSet(petalPins[i], (51 - j) * 10 / 2);
  }
  return false;
}

/**
 * 含苞待放
 * 花蕊呼吸彩灯或白光，花瓣呼吸灯
 */
int h = 0;
void budding() {
  //花蕊
  for(int i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = CHSV((h + (255 / NUM_LEDS) * i),255,255);
    FastLED.show();
  }

  delay(2);
  h = (h + 3) % 255;

  //花瓣
  
}

/**
 * 仅照明
 * 花蕊白光，花瓣白光
 */
void onlylight() {
  //花蕊
  showColorLed();

  //花瓣
  for (int i = 0; i < 6; i++) {
    SoftPWMSet(petalPins[i], 255);
  }
}

//沉鱼
boolean openPetals() {
  if (servoPosition <= SERVO_OPEN) {
    return true;
  }
  servoPosition --;
  myservo.write(servoPosition);

  if(servoPosition % 3 == 0){
    upPosToMQTT(servoPosition);
  }
  return false;
}

//羞花
boolean closePetals() {
  if (servoPosition >= SERVO_CLOSED) {
    return true;
  }
  servoPosition ++;
  myservo.write(servoPosition);

  if(servoPosition % 3 == 0){
    upPosToMQTT(servoPosition);
  }
  return false;
}

boolean crossFade() {
  return true;
}

//随机颜色绽放
void prepareCrossFadeBloom(void) {
  byte color = random(0, 10);
  switch (color) {
    case 0: // white
      prepareCrossFade(140, 140, 140);
      break;
    case 1: // red
      prepareCrossFade(140, 5, 0);
      break;
    case 2: // purple
      prepareCrossFade(128, 0, 128);
      break;
    case 3: // pink
      prepareCrossFade(140, 0, 70);
      break;
    case 4: // orange
      prepareCrossFade(255, 70, 0);
      break;
    case 5: // yellow
      prepareCrossFade(255, 255, 0);
      break;
    case 6: // AQUA
      prepareCrossFade(0, 255, 255);
      break;
    case 7: // INDIANRED
      prepareCrossFade(205, 92, 92);
      break;
    case 8: // SALMON
      prepareCrossFade(250, 128, 114);
      break;
    case 9: // LIGHTSALMON
      prepareCrossFade(255, 160, 122);
      break;
    case 10: // #F1C40F
      prepareCrossFade(241, 196, 15);
      break;
  }
}

void prepareCrossFade(byte red, byte green, byte blue) {
  CRGB myRGBColor(red, green, blue);
  fill_solid(leds, NUM_LEDS, myRGBColor);
  FastLED.show();
  delay(10);
}

//花蕊白色
void showColorLed(void)
{
  for(int i = 0; i < NUM_LEDS; i++ )
  {
    leds[i] = CRGB::White;
    FastLED.show();
    delay(2);
  }
}

//花蕊关闭
void closeColorLed(void)
{
  for(int i = 0; i < NUM_LEDS; i++ )
  {
    leds[i] = CRGB::Black;
    FastLED.show();
    delay(2);
  }
}
