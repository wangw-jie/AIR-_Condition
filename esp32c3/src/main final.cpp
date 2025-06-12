
/* 

   T使用步骤：
   1. 创建一个 BLE Server
   2. 创建一个 BLE Service
   3. 创建一个 BLE Characteristic
   4. 创建一个 BLE Descriptor
   5. 开始服务
   6. 开始广播


*/
 #include <Arduino.h>
 #include <BLEDevice.h>
 #include <BLEServer.h>
 #include <BLEUtils.h>
 #include <BLE2902.h>
 #include "SSD1306Wire.h"
 #include "HardwareSerial.h"
 #include <Preferences.h> // 添加头文件
 #include <esp_system.h> // 用于esp_random()
 #include "esp_gap_ble_api.h"
 #include "esp_gattc_api.h"
 #include "esp_bt_defs.h"
 #include "esp_bt_device.h"

 HWCDC SerialUSB; //USB串口对象
 const int I2C_ADDR = 0x3c;              // oled屏幕的I2c地址
 #define SDA_PIN 0                       
 #define SCL_PIN 1                     
 /* 新建一个oled屏幕对象，需要输入IIC地址，SDA和SCL引脚号 */
 SSD1306Wire oled(I2C_ADDR, SDA_PIN, SCL_PIN);
 //测试屏幕显示
 void drawRect(void) {
   for (int16_t i=0; i<oled.getHeight()/2; i+=2) {
     oled.drawRect(i, i, oled.getWidth()-2*i, oled.getHeight()-2*i);
     oled.display();
     delay(50);
   }
 }


 Preferences prefs; // 持久化存储对象
 std::string receive_uuid; // 接收的UUID
 uint8_t txValue = 0, mode = 0, temperature = 20, speed = 5, direct = 0;  
 uint8_t oldMode = 0, oldtemperature = 0, oldspeed = 0, olddirect = 0; //保存上次的模式                //后面需要发送的值
 BLEServer *pServer = NULL;                   //BLEServer指针 pServer
 BLECharacteristic *pTxCharacteristic = NULL; //BLECharacteristic指针 pTxCharacteristic
 bool deviceConnected = false;                //本次连接状态
 bool oldDeviceConnected = false;             //上次连接状态
 bool opennn = false; //设备开关状态
 bool oldopennn = false; //上次设备开关状态
 uint32_t dynamicPasskey = 0;  // 动态配对码
 bool ble_link_encrypted = false;  // 是否加密连接
 BLEAddress* pPairedDevice = nullptr;  // 已配对设备地址指针
 esp_ble_bond_dev_t esp_bd_addr_list[10];  // 已配对设备地址列表
 int esp_bond_dev_num = 10;   // 已配对设备数量

 
 #define DEVICE_ID_SERVICE_UUID "0000fff9-0000-1000-8000-00805f9b34fb" // 设备标识服务
 #define SERVICE_UUID "d1537b56-6512-4a88-8ffc-cc7c49598710" // service UUID
 #define MODE_CHAR_UUID    "0000fff0-0000-1000-8000-00805f9b34fb" // 模式特征UUID
 #define TEMP_CHAR_UUID    "0000fff1-0000-1000-8000-00805f9b34fb"  // 温度特征UUID
 #define SPEED_CHAR_UUID   "0000fff2-0000-1000-8000-00805f9b34fb" // 风速特征UUID
 #define DIRECTION_CHAR_UUID "0000fff3-0000-1000-8000-00805f9b34fb" // 风向特征UUID
 #define OPEN_UUID         "0000fff4-0000-1000-8000-00805f9b34fb" // 开关特征UUID

 // 保存参数到Flash
 void saveParams() {
    prefs.begin("bleparam", false);
    prefs.putUChar("mode", mode);
    prefs.putUChar("temp", temperature);
    prefs.putUChar("speed", speed);
    prefs.putUChar("direct", direct);
    prefs.end();
 }

 // 从Flash读取参数
 void loadParams() {
    prefs.begin("bleparam", true);
    mode = prefs.getUChar("mode", 0);
    temperature = prefs.getUChar("temp", 20);
    speed = prefs.getUChar("speed", 5);
    direct = prefs.getUChar("direct", 0);
    prefs.end();
 }

 // 保存配对设备信息
 void savePairedDeviceInfo(BLEAddress address) {
    prefs.begin("bledevice", false);
    prefs.putString("address", address.toString().c_str());
    prefs.end();
 }

 // 加载配对设备信息
 bool loadPairedDeviceInfo() {
    prefs.begin("bledevice", true);
    String savedAddress = prefs.getString("address", "");
    prefs.end();
    
    if (savedAddress.length() > 0) {
        if (pPairedDevice != nullptr) {
            delete pPairedDevice;
        }
        pPairedDevice = new BLEAddress(savedAddress.c_str());
        return true;
    }
    return false;
 }

 // 清除所有存储的数据
 void clearAllStoredData() {
    prefs.begin("bleparam", false);
    prefs.clear();
    prefs.end();

    prefs.begin("bledevice", false);
    prefs.clear();
    prefs.end();

    // 清除配对设备指针
    if (pPairedDevice != nullptr) {
        delete pPairedDevice;
        pPairedDevice = nullptr;
    }

    SerialUSB.println("已清除所有存储数据");
 }

 class MySecurityCallbacks : public BLESecurityCallbacks {
     // 处理配对请求
    uint32_t onPassKeyRequest() override {
        SerialUSB.print("配对码请求，当前配对码: ");
        SerialUSB.println(dynamicPasskey);
        return dynamicPasskey;
    }
    void onPassKeyNotify(uint32_t pass_key) override {
        SerialUSB.print("配对码通知: ");
        SerialUSB.println(pass_key);
    }
    bool onConfirmPIN(uint32_t pass_key) override {
        SerialUSB.print("确认配对码: ");
        SerialUSB.println(pass_key);
        return true; // 自动确认
    }
    bool onSecurityRequest() override {
        return true;
    }
    // 处理配对完成事件
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        if (cmpl.success) {
            SerialUSB.println("配对成功");
            ble_link_encrypted = true;

            // 保存配对设备地址
            BLEAddress remoteAddress(cmpl.bd_addr);
            savePairedDeviceInfo(remoteAddress);
            if (pPairedDevice != nullptr) {
                delete pPairedDevice;
             }
            pPairedDevice = new BLEAddress(remoteAddress);
            SerialUSB.print("已保存配对设备地址: ");
            SerialUSB.println(remoteAddress.toString().c_str());
            //配对成功过后，连接完成后设置连接状态
            if (pServer != nullptr) {
                deviceConnected = true; // 设置连接状态为连接
            }
        } else {
            SerialUSB.println("配对失败");
            ble_link_encrypted = false;
            // 配对失败时断开连接
            if (pServer != nullptr) {
                pServer->disconnect(0);
            }
        }
    }
 };


 class MyServerCallbacks : public BLEServerCallbacks
 {   // 处理连接事件
     void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override
     {

        BLEAddress remoteAddress(param->connect.remote_bda);
        SerialUSB.print("设备尝试连接，地址: ");
        SerialUSB.println(remoteAddress.toString().c_str());

        // 如果不是已配对设备，主动发起配对
        if (pPairedDevice == nullptr || !pPairedDevice->equals(remoteAddress)) {
            SerialUSB.println("未配对设备，正在发起配对...");
            
            // 主动发起配对请求
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
            
            // 此时不立即设置连接状态，等待配对完成
            deviceConnected = false;
        } else {
            SerialUSB.println("已配对设备，允许连接");
            ble_link_encrypted = true;
            deviceConnected = true;
        }
     }

     void onDisconnect(BLEServer *pServer)
     {
         deviceConnected = false;
         ble_link_encrypted = false; // 断开连接时重置加密状态
         SerialUSB.println(" 蓝牙未连接 ");
         oldDeviceConnected = true;
     }
 };

 class MyCallbacks : public BLECharacteristicCallbacks
 {
    void onWrite(BLECharacteristic *pCharacteristic) override
    {
         // 检查是否加密连接
        if (!ble_link_encrypted) 
        {
            SerialUSB.println("未加密连接，拒绝操作！");
        }
         // 处理接收到的特征值
        BLEUUID uuid = pCharacteristic->getUUID();
        if (uuid.equals(BLEUUID(OPEN_UUID)))
        {
            opennn = !opennn; // 切换设备状态
            SerialUSB.print("设备状态切换为: ");
            SerialUSB.println(opennn ? "开启" : "关闭");
        }
        // 如果设备已开启，根据各个UUID处理其他特征值
        else if (opennn)
        {
            if (uuid.equals(BLEUUID(TEMP_CHAR_UUID)))
            {
                temperature = atoi(pCharacteristic->getValue().c_str());
                saveParams();
            }
            else if (uuid.equals(BLEUUID(MODE_CHAR_UUID)))
            {
                mode = atoi(pCharacteristic->getValue().c_str());
                saveParams();
            }
            else if (uuid.equals(BLEUUID(DIRECTION_CHAR_UUID)))
            {
                direct = atoi(pCharacteristic->getValue().c_str());
                saveParams();
            }
            else if (uuid.equals(BLEUUID(SPEED_CHAR_UUID)))
            {
                speed = atoi(pCharacteristic->getValue().c_str());
                saveParams();
            }
        }
        
        else
        {
            SerialUSB.print("Unknown command: ");
            SerialUSB.println(uuid.toString().c_str());
        }
    }
 };

 void setup()
 {
    //  clearAllStoredData();  //清除已配对设备信息
     loadParams(); // 启动时加载参数
     loadPairedDeviceInfo(); // 加载配对设备信息
     opennn = false; // 默认设备状态为关闭
     SerialUSB.begin(9600);
     while (!SerialUSB) {
         delay(10);
     }

     // 生成动态6位配对码
     dynamicPasskey = 100000 + (esp_random() % 900000);

     oled.init();
     oled.flipScreenVertically();          // 设置屏幕翻转
     oled.setContrast(255);                // 设置屏幕亮度
     drawRect();                           // 测试屏幕显示
     oled.clear();          // 清除屏幕
     oled.drawString(0,0, "PIN:" + String(dynamicPasskey));//显示配对码
     oled.display();


     // 创建一个 BLE 设备
     // 设置配对码和安全参数
     BLEDevice::init("AIR Conditioner1"); // 设置BLE设备名称
     BLESecurity *pSecurity = new BLESecurity();
     pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND); // 推荐使用带配对和绑定的模式
     pSecurity->setCapability(ESP_IO_CAP_OUT); // 输出配对码
     pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
     pSecurity->setKeySize(6);   //配对码长度
     pSecurity->setStaticPIN(dynamicPasskey); // 设置配对码
     pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
     BLEDevice::setSecurityCallbacks(new MySecurityCallbacks());  //加密安全回调

     // 创建一个 BLE 服务
     pServer = BLEDevice::createServer();
     pServer->setCallbacks(new MyServerCallbacks()); //设置服务回调

     // 添加设备标识服务
     BLEService *pIdService = pServer->createService(DEVICE_ID_SERVICE_UUID);
     BLECharacteristic *pIdChar = pIdService->createCharacteristic(
        BLEUUID((uint16_t)0xFFF9),
        BLECharacteristic::PROPERTY_READ
     );
     pIdChar->setValue("AIR-001");  //设备标识值
     pIdService->start();   // 启动设备标识服务

     // 设置广播数据
     BLEAdvertising *pAdvertising = pServer->getAdvertising();
     BLEAdvertisementData advertisementData; // 广播数据
     advertisementData.setCompleteServices(BLEUUID(DEVICE_ID_SERVICE_UUID));  // 设置广播服务UUID
     advertisementData.setName("AIR Conditioner1"); // 设置广播名称
     pAdvertising->setAdvertisementData(advertisementData); // 设置广播数据
     
     // 在扫描响应中放入主服务 UUID
     BLEAdvertisementData scanResponse;
     scanResponse.setCompleteServices(BLEUUID(SERVICE_UUID));
     pAdvertising->setScanResponseData(scanResponse);

     BLEService *pService = pServer->createService(SERVICE_UUID);  // 创建主服务

     // 创建空调参数 BLE 特征
     auto modeChar = pService->createCharacteristic(MODE_CHAR_UUID,
                                                   BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
     auto tempChar = pService->createCharacteristic(TEMP_CHAR_UUID,
                                                  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
     auto speedChar = pService->createCharacteristic(SPEED_CHAR_UUID,
                                                   BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
     auto directionChar = pService->createCharacteristic(DIRECTION_CHAR_UUID,
                                                       BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
     auto openChar = pService->createCharacteristic(OPEN_UUID,
                                                   BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);


     //设置特征回调
     static MyCallbacks charCallback;    
     modeChar->setCallbacks(&charCallback);
     tempChar->setCallbacks(&charCallback);
     speedChar->setCallbacks(&charCallback);
     directionChar->setCallbacks(&charCallback);
     openChar->setCallbacks(&charCallback);

     pService->start();                  // 开始服务
     pServer->getAdvertising()->start(); // 开始广播
     SerialUSB.println(" 等待一个客户端连接，且发送通知... ");
 }

 void loop()
 {
     static uint8_t oldMode = 0, oldtemperature = 0, oldspeed = 0, olddirect = 0;
     static bool oldopennn = false;
     // deviceConnected 已连接
     if (deviceConnected)
     {
         if(opennn)
         {
             // 只在参数变化或opennn由false变为true时刷新OLED
             if ((oldMode != mode) || (oldtemperature != temperature) || (oldspeed != speed) || (olddirect != direct) || (!oldopennn && opennn))
             {
                 oled.clear();
                 oled.display();
                 oled.setFont(ArialMT_Plain_16);       // 设置字体
                 oled.drawString(0,0, "Mode:" + String(mode));
                 oled.drawString(0,15, "Temperature:" + String(temperature));
                 oled.drawString(0,30, "Speed:" + String(speed));
                 oled.drawString(0,45, "Direction:" + String(direct ? "Up" : "Down"));
                 oled.display();
                 oldMode = mode;
                 oldtemperature = temperature;
                 oldspeed = speed;
                 olddirect = direct;
             }
         }
         else
         {
             // opennn 由 true 变为 false 时清屏
             if (oldopennn) {
                 oled.clear();
                 oled.display();
             }
         }
         oldopennn = opennn;

     }

     // disconnecting  断开连接
     if (!deviceConnected && oldDeviceConnected)
     {
         SerialUSB.println(" 蓝牙已断开 ");
         delay(500);                  // 留时间给蓝牙缓冲
         pServer->startAdvertising(); // 重新广播
         SerialUSB.println(" 开始广播 ");
         oldDeviceConnected = deviceConnected;
     }

     // connecting  正在连接
     if (deviceConnected && !oldDeviceConnected)
     {
         // do stuff here on connecting
         oldDeviceConnected = deviceConnected;
     }

 }