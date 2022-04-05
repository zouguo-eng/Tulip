/**
 * 2022-03-10
 * 虚拟串口
 * 
 * 2022-03012
 * 规定文件交互格式json、raw
 * 上行:json
 * {"pos":110}
 * {"mode":0}
 * 下行:raw
 * BLOOM、FADE、BUDDING、ONLYLIGHT、SLEEP
**/

//是否打开调试信息
#define DEBUG_FLAG 

//设备信息
#ifdef ESP8266
String ChipId = String(ESP.getChipId(), HEX);
#elif ESP32
String ChipId = String((uint32_t) ESP.getEfuseMac(), HEX);
#endif
//配网库
#include <IotWebConf.h>
#include <IotWebConfUsing.h>
// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN 14
String owner = "wzyshine";//所属者
String deviceType = "esp8266";//设备类别
String thingName = deviceType + String("-") + ChipId;
const char wifiInitialApPassword[] = "wzyshine";
#define CONFIG_VERSION "v2"
//参数
#define  MQTT_MSG_SIZE    256
char mqttServer[32];
char mqttUserName[32];
char mqttUserPassword[32];
String topicGateway = owner + "/" + deviceType + "gw";
char mqttTopicGateway[22];//将设备唯一标识发送到MQTTF服务端
String topicPrefix = owner + "/" + deviceType;
char mqttTopicPrefix[32];//MQTT前缀
char mqttTopic[MQTT_MSG_SIZE];
#define  mqttPort   1883
//Method
void handleRoot();
void wifiConnected();
void configSaved();
bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper);

DNSServer dnsServer;
WebServer server(80);
IotWebConf iotWebConf(thingName.c_str(), &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameterGroup group1 = IotWebConfParameterGroup("c_factor", "MQTT configuration");
IotWebConfTextParameter mqttServerParam = IotWebConfTextParameter("MQTT Server", "mqttServer", mqttServer, sizeof(mqttServer));
IotWebConfTextParameter mqttUserNameParam = IotWebConfTextParameter("MQTT UserName", "mqttUser", mqttUserName, sizeof(mqttUserName));
IotWebConfTextParameter mqttUserPasswordParam = IotWebConfTextParameter("MQTT Password", "mqttPass", mqttUserPassword, sizeof(mqttUserPassword), "password");
IotWebConfTextParameter mqttTopicPrefixParam = IotWebConfTextParameter("MQTT Prefix", "mqttPrefix", mqttTopicPrefix, sizeof(mqttTopicPrefix));

//联网、MQTT、定时
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>

String comdata_str  = "";//指令文件
int comdata_index = -1;

//软串口
#include <SoftwareSerial.h>
const int virtualTXD = 5;
const int virtualRXD = 4;
SoftwareSerial esp2arduino(virtualRXD, virtualTXD);

//网络状态指示灯
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

//定时任务、MQTT
Ticker ticker;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
int countTicker;    // Ticker计数用变量
String curLocalIP = "null";

void setup() {
  Serial.begin(115200);
  //初始化虚拟串口
  esp2arduino.begin(9600);

  //初始化数组
  strcpy(mqttTopicGateway, topicGateway.c_str());
  strcpy(mqttTopicPrefix, topicPrefix.c_str());

  group1.addItem(&mqttServerParam);
  group1.addItem(&mqttUserNameParam);
  group1.addItem(&mqttUserPasswordParam);
//  group1.addItem(&mqttTopicPrefixParam);//自定义topic

  //初始化配网
  iotWebConf.setStatusPin(STATUS_PIN);
//  iotWebConf.setConfigPin(CONFIG_PIN);
  iotWebConf.addParameterGroup(&group1);
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.skipApStartup();

  bool validConfig = iotWebConf.init();
  if (!validConfig)
  {
    mqttServer[0] = '\0';
    mqttUserName[0] = '\0';
    mqttUserPassword[0] = '\0';
  }  

//  server.on("/", handleRoot);
//  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.on("/", [] { iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });

  // Ticker定时对象
  ticker.attach(1, tickerCount);
}

void loop() {
  //应该尽可能频繁的调用
  iotWebConf.doLoop();

  if (esp2arduino.available() > 2)//判读是否串口有数据
  {
    String comdata = "";//缓冲字符串
    comdata_str = "";
    while (esp2arduino.available() > 0)//循环串口是否有数据
    {
      comdata += char(esp2arduino.read());//叠加字节的数据到comdata
      delay(2);//延时等待响应
    }
    
    comdata_str = comdata;//如果需要处理在此处进行

    if(comdata_str != "" && comdata_str.startsWith("{") || comdata_str.endsWith("}"))//数据格式有效性判断,json基本型
    {
      #ifdef DEBUG_FLAG
        Serial.print("CheckData:");
        Serial.println(comdata_str);
      #endif

      //发布位置和模式数据到MQTT服务器
      pubMQTTMsg(comdata_str, mqttTopicPrefix, "sendup", true);
    }
  }

  if (mqttClient.connected()) { // 如果开发板成功连接服务器
    // 10秒心跳
    if (countTicker >= 10){
      pubMQTT();
      countTicker = 0;
    }    
    // 保持心跳
    mqttClient.loop();
  } else {                  // 如果开发板未能成功连接服务器
    if(WiFi.status() == WL_CONNECTED){
      connectMQTTServer();    // 则尝试连接MQTT服务器
    }
  }
}

void tickerCount(){
  countTicker++;
  if(countTicker > 9999)
  {
    countTicker = 0;//防无限增长
  }
}

//连接MQTT服务器
void connectMQTTServer(){
  // 设置MQTT服务器和端口号
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(onMqttMessage);
  
  // 根据ESP8266的MAC地址生成客户端ID（避免与其它ESP8266的客户端ID重名）
  #ifdef DEBUG_FLAG
    Serial.println(WiFi.macAddress());
  #endif
  String clientId = deviceType + "-" + WiFi.macAddress();

  // 连接MQTT服务器
  if (mqttClient.connect(clientId.c_str())) { 
    #ifdef DEBUG_FLAG
      Serial.println(F("MQTT Server Connected."));
      Serial.println(F("Server Address: "));
      Serial.println(mqttServer);
      Serial.println(F("ClientId:"));
      Serial.println(clientId);
    #endif

    /**
     * 订阅Topic在此定义
     */
    mqttClient.subscribe(mqttFullTopic(mqttFullTopic(mqttTopicPrefix, "sendown"), ChipId.c_str()));//接收指令的Topic

    //将设备信息发送到MQTT服务端
    pubMQTTMsg(thingName + ",IP is:" + curLocalIP, mqttTopicGateway, "device", false);   
  } else {
    Serial.print(F("MQTT Server Connect Failed. Client State:"));
    Serial.println(mqttClient.state());
    delay(3000);
  }   
}

//MQTT订阅消息
void onMqttMessage(char* topic, byte* payload, unsigned int mlength)  {
  char newMsg[MQTT_MSG_SIZE];

  if (mlength > 0) {
    memset(newMsg, '\0' , sizeof(newMsg));
    memcpy(newMsg, payload, mlength);

    String msg = "";
    for(int i = 0; i < mlength; i++){
      msg += String(newMsg[i]);
    }

    #ifdef DEBUG_FLAG
      Serial.println(F("[MQTT Recv:]"));
      Serial.println(topic);
      Serial.println(msg);
    #endif

    if ( !strcmp(topic, mqttFullTopic(mqttFullTopic(mqttTopicPrefix, "sendown"), ChipId.c_str()))) {
      if(msg.length() > 0){
        /**
         * 不做解析，直接将指令发送到Arduino
         */
        esp2arduino.print(msg);
      }
    }  
  }  
}

//MQTT前缀，MQTT后缀
char* mqttFullTopic(const char topicPrefix[], const char action[]) {
  strcpy (mqttTopic, topicPrefix);
  strcat (mqttTopic, "/");
  strcat (mqttTopic, action);
  #ifdef DEBUG_FLAG
    Serial.print(F("CurrentTopic:"));Serial.println(mqttTopic);
  #endif
  return mqttTopic;
}

// 发布信息
void pubMQTT(){
  String actionPrefix = "heartbeat/" + ChipId;;
  char actionArr[22];
  strcpy(actionArr, actionPrefix.c_str());
  // 这么做是为确保不同用户进行MQTT信息发布时，ESP8266客户端名称各不相同，
  String topicString = mqttFullTopic(mqttTopicPrefix, actionArr);
  char publishTopic[topicString.length() + 1];  
  strcpy(publishTopic, topicString.c_str());

  // 建立发布信息
  String messageString = String(countTicker);
  char publishMsg[messageString.length() + 1];   
  strcpy(publishMsg, messageString.c_str());
  
  // 实现ESP8266向主题发布信息
  if(mqttClient.publish(publishTopic, publishMsg)){
    #ifdef DEBUG_FLAG
      Serial.println(F("PublicMsgAuto:"));
      Serial.println(publishTopic);
      Serial.println(publishMsg);
    #endif    
  } else {
    Serial.println(F("Message Publish Failed.")); 
  }
}

//发布消息指定内容
void pubMQTTMsg(String msg, const char topicPrefix[], const char topic[], boolean takeLabel){
  String topicString = "";
  if(takeLabel){
    char actionArr[22];
    strcpy(actionArr, topic);
    strcat(actionArr, "/");
    strcat(actionArr, ChipId.c_str());
    topicString = mqttFullTopic(topicPrefix, actionArr);
  }else{
    topicString = mqttFullTopic(topicPrefix, topic);
  }
  
  char publishTopic[topicString.length() + 1];  
  strcpy(publishTopic, topicString.c_str());

  // 建立发布信息。信息内容以Hello World为起始，后面添加发布次数。WiFi.macAddress();
  String messageString = msg; 
  char publishMsg[messageString.length() + 1];   
  strcpy(publishMsg, messageString.c_str());
  
  // 实现ESP8266向主题发布信息
  if(mqttClient.publish(publishTopic, publishMsg)){
    #ifdef DEBUG_FLAG
      Serial.println(F("PublicMsg:"));
      Serial.println(publishTopic);
      Serial.println(publishMsg);    
    #endif
  } else {
    Serial.println(F("Message Publish Failed.")); 
  }
}

//处理网页访问
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>郁金香配网 TulipConfiguration</title></head><body><div>Param page of ";
  s += iotWebConf.getThingName();
  s += ".</div>";
  s += "<ul>";
  s += "<li>MQTT Server: ";
  s += mqttServer;
  s += "</ul>";
  s += "Go to <a href='config'>configure page(ClickMe)</a> to change values.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void wifiConnected()
{
  #ifdef DEBUG_FLAG
    Serial.println(F("WiFi was connected."));
  #endif
  //获取局域IP
  curLocalIP = WiFi.localIP().toString().c_str();
  // 连接MQTT服务器
  connectMQTTServer();
}

void configSaved()
{
  #ifdef DEBUG_FLAG
    Serial.println(F("Configuration was updated."));
  #endif
}

boolean formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper) {

  boolean valid = true;
  int l = server.arg(mqttServerParam.getId()).length();
  if (l == 0) {
    mqttServerParam.errorMessage = "Please provide an MQTT server";
    valid = false;
  }
  return valid;
}
