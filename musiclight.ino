#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include "Freenove_WS2812_Lib_for_ESP32.h"

// ================= 硬件引脚定义 =================
#define LED_PIN     13
#define LED_COUNT   48
#define LED_CHANNEL 0
#define ZONE_COUNT  6

// 麦克风 I2S 引脚
#define I2S_WS      5  
#define I2S_SCK     6  
#define I2S_SD      4  
#define I2S_PORT    I2S_NUM_0

// ================= FFT 参数与音频调优 =================
#define SAMPLES         512   
#define SAMPLING_FREQ   16000 

// 【全新量级的门限参数】(数据已归一化，现在非常精准)
double noiseFloor = 0.7;  // 底噪门限：建议在 0.5 ~ 3.0 之间。如果安静时灯还会微亮，调大它。
double peakSignal = 9.0;  // 满载峰值：建议在 10.0 ~ 30.0 之间。如果你要很大声灯才能全亮，调小它。

double vReal[SAMPLES];
double vImag[SAMPLES];

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQ);

uint8_t audioBands[6] = {0}; 

// 全局设置参数
bool enableDebug = false;        
uint8_t globalBrightness = 255;  
bool isPowerOn = true;

// AP 热点配置
const char *ap_ssid = "ESP32_Audio_LED"; 
const char *ap_pass = "12345678";        

Freenove_ESP32_WS2812 strip(LED_COUNT, LED_PIN, LED_CHANNEL, TYPE_GRB);
WebServer server(80);

const int ZONE_START[ZONE_COUNT] = {  0, 18, 20, 38, 40, 44 };
const int ZONE_SIZE[ZONE_COUNT]  = { 18,  2, 18,  2,  4,  4 };

// 特效枚举：Audio_Blink(呼吸闪烁) 和 Audio_VU(进度条)
enum Effect { FX_SOLID=0, FX_BREATHE=1, FX_RAINBOW=2, FX_CHASE=3, FX_OFF=4, FX_AUDIO_BLINK=5, FX_AUDIO_VU=6 };

struct ZoneState {
  uint8_t r, g, b, brightness; 
  Effect  effect;
  uint8_t hue, pos, bVal;
  int8_t  bDir;
};

ZoneState zones[ZONE_COUNT];
uint8_t ledR[LED_COUNT], ledG[LED_COUNT], ledB[LED_COUNT];
unsigned long lastLed = 0;
unsigned long lastDebugPrint = 0;

bool startAccessPoint() {
  WiFi.mode(WIFI_AP);
  
  // =========== 核心修复：绕过 ESP32-S3 Arduino 封装 Bug ===========
  // 1. 禁用省电，防止 Beacon 广播中断
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  // 2. 原生 IDF 配置并启动
  wifi_config_t ap_config = {};
  strcpy((char*)ap_config.ap.ssid, ap_ssid);
  strcpy((char*)ap_config.ap.password, ap_pass);
  ap_config.ap.ssid_len = strlen(ap_ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap_config.ap.max_connection = 4;
  ap_config.ap.beacon_interval = 100;

  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  esp_wifi_start();

  // 3. 配置 IP (Arduino 的 softAPConfig 仍有效并需要)
  IPAddress apIp(192, 168, 4, 1);
  IPAddress apGw(192, 168, 4, 1);
  IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apGw, apMask);

  return true;
}

void hsv2rgb(uint8_t h, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
  uint8_t reg = h / 43, rem = (h - reg * 43) * 6;
  uint8_t p = 0, q = (uint16_t)v*(255-rem)/255, t = (uint16_t)v*rem/255;
  switch (reg % 6) {
    case 0:r=v;g=t;b=p;break; case 1:r=q;g=v;b=p;break;
    case 2:r=p;g=v;b=t;break; case 3:r=p;g=q;b=v;break;
    case 4:r=t;g=p;b=v;break; default:r=v;g=p;b=q;break;
  }
}

// 【修复请求：取消随机，恢复固定颜色】
void initZones() {
  uint8_t dr[]={255, 0,   0,   255, 0,   128};
  uint8_t dg[]={80,  200, 0,   120, 180, 0};
  uint8_t db[]={0,   255, 255, 0,   255, 255};
  
  for(int i=0; i<ZONE_COUNT; i++) {
    Effect defaultEffect = (ZONE_SIZE[i] > 4) ? FX_AUDIO_VU : FX_AUDIO_BLINK;
    zones[i] = {dr[i], dg[i], db[i], 255, defaultEffect, (uint8_t)(i*40), 0, 0, 1};
  }
}

void renderZone(int z) {
  int s0 = ZONE_START[z], sz = ZONE_SIZE[z];
  ZoneState& s = zones[z];
  if (s.effect == FX_BREATHE) {
    s.bVal += s.bDir * 3;
    if (s.bVal >= 252) { s.bVal = 252; s.bDir = -1; }
    if (s.bVal == 0)   {               s.bDir =  1; }
  }
  
  for (int i = 0; i < sz; i++) {
    uint8_t r=0,g=0,b=0;
    switch (s.effect) {
      case FX_OFF: break;
      case FX_SOLID:
        r=s.r*s.brightness/255; g=s.g*s.brightness/255; b=s.b*s.brightness/255; break;
      case FX_BREATHE: {
        uint8_t lv=(uint16_t)s.bVal*s.brightness/255;
        r=s.r*lv/255; g=s.g*lv/255; b=s.b*lv/255; break;
      }
      case FX_RAINBOW:
        hsv2rgb(s.hue+(uint8_t)(i*256/sz), s.brightness, r,g,b); break;
      case FX_CHASE:
        if(i==s.pos%sz){r=s.r*s.brightness/255;g=s.g*s.brightness/255;b=s.b*s.brightness/255;}
        break;
      case FX_AUDIO_BLINK: {
        uint8_t currentVol = audioBands[z % 6];
        uint16_t curveVol = (currentVol * currentVol) / 255; 
        uint8_t dynamicBr = (curveVol * s.brightness) / 255;
        r = s.r * dynamicBr / 255; 
        g = s.g * dynamicBr / 255; 
        b = s.b * dynamicBr / 255;
        break;
      }
      case FX_AUDIO_VU: {
        uint8_t currentVol = audioBands[z % 6];
        uint16_t curveVol = (currentVol * currentVol) / 255; 
        int litLeds = (curveVol * sz) / 255; 
        if (i < litLeds) {
          r = s.r * s.brightness / 255; 
          g = s.g * s.brightness / 255; 
          b = s.b * s.brightness / 255;
        }
        break;
      }
    }
    ledR[s0+i]=r; ledG[s0+i]=g; ledB[s0+i]=b;
  }
  if (s.effect==FX_RAINBOW) s.hue+=2;
  if (s.effect==FX_CHASE && ++s.pos>=sz) s.pos=0;
}

void pushLeds() {
  for (int i=0;i<LED_COUNT;i++) strip.setLedColorData(i,ledR[i],ledG[i],ledB[i]);
  strip.show();
}

// ================= Web 服务器 =================
void handleRoot() {
  const char* names[]={"A(18)","B(2)","C(18)","D(2)","E(4)","F(4)"};
  String h = "<!DOCTYPE html><html><head><meta charset=UTF-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>LED Audio</title><style>"
    "body{background:#111;color:#eee;font:14px sans-serif;padding:8px}"
    "h2{color:#f90;text-align:center;margin-bottom:10px}"
    ".panel{background:#222;padding:10px;border-radius:6px;margin-bottom:15px;border:1px solid #444}"
    "td{padding:5px;border-bottom:1px solid #222}"
    "select,input{background:#222;color:#eee;border:1px solid #444;border-radius:4px}"
    "input[type=color]{width:40px;height:28px;padding:1px}"
    "input[type=range]{width:80px;accent-color:#f90}"
    "button{background:#f90;border:none;border-radius:4px;padding:4px 10px;"
    "color:#111;font-weight:bold;cursor:pointer}"
    "</style></head><body><h2>LED Audio Ctrl</h2>";

  char g_buf[1024];
  snprintf(g_buf, sizeof(g_buf),
    "<div class='panel'>"
    "<b>Power State: </b><button id='btn_pwr' onclick='tp()'>%s</button><br><br>"
    "<b>Global Brightness: </b><input type=range id='g_br' min=0 max=255 value=%d onchange='sg()'><br><br>"
    "<b>Debug Print Output: </b><select id='g_dbg' onchange='sg()'>"
    "<option value=0%s>OFF</option><option value=1%s>ON</option></select><br><br>"
    "<b>Noise Floor: </b><input type=number id='g_nf' step=0.1 min=0 value=%.1f onchange='sg()'><br><br>"
    "<b>Peak Signal: </b><input type=number id='g_ps' step=0.1 min=0 value=%.1f onchange='sg()'>"
    "</div><table><tr><th>Zone</th><th>Effect</th><th>Color</th><th>Brightness</th><th></th></tr>",
    isPowerOn ? "ON" : "OFF",
    globalBrightness, enableDebug ? "" : " selected", enableDebug ? " selected" : "",
    noiseFloor, peakSignal
  );
  h += g_buf;

  for (int i=0;i<ZONE_COUNT;i++) {
    char row[768]; 
    snprintf(row,sizeof(row),
      "<tr><td><b>%s</b></td>"
      "<td><select id='f%d'>"
        "<option value=0%s>Solid</option><option value=1%s>Breathe</option>"
        "<option value=2%s>Rainbow</option><option value=3%s>Chase</option>"
        "<option value=4%s>Off</option><option value=5%s>Audio Blink</option>"
        "<option value=6%s>Audio VU</option>"
      "</select></td>"
      "<td><input type=color id='c%d' value='#%02x%02x%02x'></td>"
      "<td><input type=range id='b%d' min=0 max=255 value=%d></td>"
      "<td><button onclick='s(%d)'>OK</button></td></tr>",
      names[i],i,
      zones[i].effect==0?" selected":"", zones[i].effect==1?" selected":"",
      zones[i].effect==2?" selected":"", zones[i].effect==3?" selected":"",
      zones[i].effect==4?" selected":"", zones[i].effect==5?" selected":"", 
      zones[i].effect==6?" selected":"", 
      i,zones[i].r,zones[i].g,zones[i].b, i,zones[i].brightness,i);
    h += row;
  }

  char js_buf[256];
  snprintf(js_buf, sizeof(js_buf), "var pwr = %d;", isPowerOn ? 1 : 0);
  h += "</table><script>";
  h += js_buf;
  h +=
    "function tp(){"
      "pwr = 1 - pwr;"
      "document.getElementById('btn_pwr').innerText = pwr ? 'ON' : 'OFF';"
      "var x=new XMLHttpRequest(); x.open('GET','/global?pwr='+pwr); x.send();"
    "}"
    "function s(i){"
      "var c=document.getElementById('c'+i).value;"
      "var r=parseInt(c.slice(1,3),16), g=parseInt(c.slice(3,5),16), b=parseInt(c.slice(5,7),16);"
      "var fx=document.getElementById('f'+i).value, br=document.getElementById('b'+i).value;"
      "var x=new XMLHttpRequest();"
      "x.open('GET','/set?z='+i+'&r='+r+'&g='+g+'&b='+b+'&fx='+fx+'&br='+br); x.send();"
    "}"
    "function sg(){"
      "var br=document.getElementById('g_br').value, dbg=document.getElementById('g_dbg').value;"
      "var nf=document.getElementById('g_nf').value, ps=document.getElementById('g_ps').value;"
      "var x=new XMLHttpRequest(); x.open('GET','/global?br='+br+'&dbg='+dbg+'&nf='+nf+'&ps='+ps); x.send();"
    "}"
    "</script></body></html>";
  server.send(200,"text/html",h);
}

void handleGlobal() {
  if(server.hasArg("pwr")) {
    isPowerOn = (server.arg("pwr").toInt() == 1);
    strip.setBrightness(isPowerOn ? globalBrightness : 0);
  }
  if(server.hasArg("br")) {
    globalBrightness = constrain(server.arg("br").toInt(), 0, 255);
    if (isPowerOn) strip.setBrightness(globalBrightness); 
  }
  if(server.hasArg("dbg")) {
    enableDebug = (server.arg("dbg").toInt() == 1);
  }
  if(server.hasArg("nf")) {
    noiseFloor = server.arg("nf").toDouble();
  }
  if(server.hasArg("ps")) {
    peakSignal = server.arg("ps").toDouble();
  }
  server.send(200, "text/plain", "Global OK");
}

void handleSet() {
  if(!server.hasArg("z")){server.send(400,"text/plain","missing z");return;}
  int z=server.arg("z").toInt();
  if(z<0||z>=ZONE_COUNT){server.send(400,"text/plain","bad z");return;}
  ZoneState& st=zones[z];
  if(server.hasArg("r"))  st.r          =constrain(server.arg("r").toInt(),0,255);
  if(server.hasArg("g"))  st.g          =constrain(server.arg("g").toInt(),0,255);
  if(server.hasArg("b"))  st.b          =constrain(server.arg("b").toInt(),0,255);
  if(server.hasArg("br")) st.brightness =constrain(server.arg("br").toInt(),0,255);
  if(server.hasArg("fx")) st.effect     =(Effect)constrain(server.arg("fx").toInt(),0,6); 
  server.send(200,"text/plain","OK");
}

// ================= I2S 及 FFT 音频处理任务 =================
void initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLING_FREQ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void audioTask(void *pvParameters) {
  size_t bytesIn = 0;
  int32_t rxData[SAMPLES];
  
  // 掐头去尾：从索引 4 开始，直接过滤掉 0~125Hz 的底噪和交流电(AC 50Hz)杂音
  const int bandLimits[7] = {4, 8, 16, 32, 64, 128, 250}; 
  const float bandEQ[6] = {1.0, 1.2, 1.5, 2.0, 2.5, 3.0}; // EQ均衡补偿

  static double dc_offset = 0; // IIR 滤波器状态记录

  while (1) {
    esp_err_t result = i2s_read(I2S_PORT, &rxData, sizeof(rxData), &bytesIn, portMAX_DELAY);
    
    if (result == ESP_OK && bytesIn > 0) {
      
      for (uint16_t i = 0; i < SAMPLES; i++) {
        // 【核心修复1】数字归一化：将庞大的 32bit 数据直接除以 2147483648
        // 把声音转换成固定在 -1.0 到 +1.0 的平滑小数，防溢出、防爆音
        double sample = (double)rxData[i] / 2147483648.0; 
        
        // 【核心修复2】DSP 动态高通滤波器 (消除直流漂移和环境沉闷噪音)
        dc_offset = dc_offset * 0.995 + sample * 0.005;
        
        vReal[i] = sample - dc_offset; 
        vImag[i] = 0.0;
      }

      FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
      FFT.compute(FFTDirection::Forward);
      FFT.complexToMagnitude();

      for (int b = 0; b < 6; b++) {
        double maxEnergy = 0;
        for (int i = bandLimits[b]; i < bandLimits[b + 1]; i++) {
          if (vReal[i] > maxEnergy) maxEnergy = vReal[i];
        }
        
        maxEnergy = maxEnergy * bandEQ[b]; // 补偿高音

        int mapped = 0;
        if (maxEnergy > noiseFloor) {
          // 将能量从 [noiseFloor, peakSignal] 映射到 [0, 255]
          mapped = (int)((maxEnergy - noiseFloor) * 255.0 / (peakSignal - noiseFloor));
          mapped = constrain(mapped, 0, 255);
        }
        
        if (mapped > audioBands[b]) {
          audioBands[b] = mapped; 
        } else {
          audioBands[b] = (audioBands[b] * 0.85) + (mapped * 0.15); // 平滑跌落
        }
      }
    }
    vTaskDelay(1); 
  }
}

// ================= 主循环 =================
void setup() {
  Serial.begin(115200);
  delay(1000); 

  // 1. 最先初始化 WiFi，避免被后期大内存分配或硬件占用干扰
  bool apOk = false;
  for (int i = 0; i < 5 && !apOk; i++) {
    apOk = startAccessPoint();
    if (!apOk) {
      delay(300);
    }
  }

  Serial.print("\n=== Access Point Started ===");
  Serial.print("\nSSID: ");
  Serial.println(ap_ssid);
  Serial.print("Password: ");
  Serial.println(ap_pass);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("AP Status: ");
  Serial.println(apOk ? "OK" : "FAIL");

  // 2. 启动 Web 服务
  server.on("/",       handleRoot);
  server.on("/set",    handleSet);
  server.on("/global", handleGlobal); 
  server.begin();

  // 3. 初始化灯带
  strip.begin();
  strip.setBrightness(globalBrightness); 
  initZones(); 
  memset(ledR,0,sizeof(ledR)); memset(ledG,0,sizeof(ledG)); memset(ledB,0,sizeof(ledB));
  pushLeds();

  // 4. 初始化音频并挂载到 Core 1，保留 Core 0 给 WiFi 专享
  initI2S();
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 10000, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  
  if(now - lastLed >= 20){ 
    lastLed = now;
    for(int z=0; z<ZONE_COUNT; z++) renderZone(z);
    pushLeds();
  }

  if (enableDebug && (now - lastDebugPrint >= 40)) {
    lastDebugPrint = now;
    Serial.printf("LowFreq:%d, MidL:%d, Mid:%d, MidH:%d, High:%d, VHigh:%d\n",
                  audioBands[0], audioBands[1], audioBands[2], 
                  audioBands[3], audioBands[4], audioBands[5]);
  }
}
