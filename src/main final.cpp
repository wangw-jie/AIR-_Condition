
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
#include <esp_system.h>  // 用于esp_random()
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"

#define DEVICE_NUMBER 1 // 设备编号

HWCDC SerialUSB;           // USB串口对象
const int I2C_ADDR = 0x3c; // oled屏幕的I2c地址
#define SDA_PIN 0
#define SCL_PIN 1
/* 新建一个oled屏幕对象，需要输入IIC地址，SDA和SCL引脚号 */
SSD1306Wire oled(I2C_ADDR, SDA_PIN, SCL_PIN);
// 测试屏幕显示
void drawRect(void)
{
    for (int16_t i = 0; i < oled.getHeight() / 2; i += 2)
    {
        oled.drawRect(i, i, oled.getWidth() - 2 * i, oled.getHeight() - 2 * i);
        oled.display();
        delay(50);
    }
}

Preferences prefs;        // 持久化存储对象
std::string receive_uuid; // 接收的UUID
uint8_t txValue = 0, mode = 0, temperature = 20, speed = 5, direct = 0;
uint8_t oldtemperature = 0;
String oldMode = String(0), oldspeed = String(0), olddirect = String(0); // 保存上次的模式               //后面需要发送的值
BLEServer *pServer = NULL;                                               // BLEServer指针 pServer
BLECharacteristic *pTxCharacteristic = NULL;                             // BLECharacteristic指针 pTxCharacteristic
bool deviceConnected = false;                                            // 本次连接状态
bool oldDeviceConnected = false;                                         // 上次连接状态
bool opennn = false;                                                     // 设备开关状态
bool oldopennn = false;                                                  // 上次设备开关状态
uint32_t dynamicPasskey = 0;                                             // 动态配对码
bool ble_link_encrypted = false;                                         // 是否加密连接
BLEAddress *pPairedDevice = nullptr;                                     // 已配对设备地址指针
esp_ble_bond_dev_t esp_bd_addr_list[10];                                 // 已配对设备地址列表
int esp_bond_dev_num = 10;                                               // 已配对设备数量
String current_mode = "";                                                // 当前模式
String current_speed = "";                                               // 当前风速
String current_direct = "";                                              // 当前风向

#define DEVICE_ID_SERVICE_UUID "0000fff8-0000-1000-8000-00805f9b34fb" // 设备标识服务
#define SERVICE_UUID "13b528f9-f225-4c8d-a3db-2c9ab927a22e"           // service UUID
#define MODE_CHAR_UUID "0000fff0-0000-1000-8000-00805f9b34fb"         // 模式特征UUID
#define TEMP_CHAR_UUID "0000fff1-0000-1000-8000-00805f9b34fb"         // 温度特征UUID
#define SPEED_CHAR_UUID "0000fff2-0000-1000-8000-00805f9b34fb"        // 风速特征UUID
#define DIRECTION_CHAR_UUID "0000fff3-0000-1000-8000-00805f9b34fb"    // 风向特征UUID

// 保存参数到Flash
void saveParams()
{
    prefs.begin("bleparam", false);
    prefs.putUChar("mode", mode);
    prefs.putUChar("temp", temperature);
    prefs.putUChar("speed", speed);
    prefs.putUChar("direct", direct);
    prefs.end();
}

// 从Flash读取参数
void loadParams()
{
    prefs.begin("bleparam", true);
    mode = prefs.getUChar("mode", 0);
    temperature = prefs.getUChar("temp", 20);
    speed = prefs.getUChar("speed", 5);
    direct = prefs.getUChar("direct", 0);
    prefs.end();
}

// 保存配对设备信息
void savePairedDeviceInfo(BLEAddress address)
{
    prefs.begin("bledevice", false);
    prefs.putString("address", address.toString().c_str());
    prefs.end();
}

// 加载配对设备信息
bool loadPairedDeviceInfo()
{
    prefs.begin("bledevice", true);
    String savedAddress = prefs.getString("address", "");
    prefs.end();

    if (savedAddress.length() > 0)
    {
        if (pPairedDevice != nullptr)
        {
            delete pPairedDevice;
        }
        pPairedDevice = new BLEAddress(savedAddress.c_str());
        return true;
    }
    return false;
}

// 清除所有存储的数据
void clearAllStoredData()
{
    prefs.begin("bleparam", false);
    prefs.clear();
    prefs.end();

    prefs.begin("bledevice", false);
    prefs.clear();
    prefs.end();

    // 清除配对设备指针
    if (pPairedDevice != nullptr)
    {
        delete pPairedDevice;
        pPairedDevice = nullptr;
    }

    SerialUSB.println("已清除所有存储数据");
}

// 显示当前模式
void displayMode(int mode)
{
    if (mode == 1)
    {
        current_mode = "Heat";
    }
    else if (mode == 2)
    {
        current_mode = "Cool";
    }
    else if (mode == 3)
    {
        current_mode = "Dry";
    }
    else if (mode == 4)
    {
        current_mode = "Fan";
    }
    else
    {
        current_mode = "Cool"; // 默认模式为Cool
    }
}
// 显示当前风速
void displayspeed(int speed)
{
    if (speed == 1)
    {
        current_speed = "Low";
    }
    else if (speed == 2)
    {
        current_speed = "Medium";
    }
    else if (speed == 3)
    {
        current_speed = "High";
    }
    else
    {
        current_speed = "Auto";
    }
}
// 显示当前风向
void displaydirection(int direction)
{
    if (direction == 1)
    {
        current_direct = "Up";
    }
    else if (direction == 2)
    {
        current_direct = "Middle";
    }
    else if (direction == 3)
    {
        current_direct = "Down";
    }
    else
    {
        current_direct = "Auto";
    }
}

class MySecurityCallbacks : public BLESecurityCallbacks
{
    // 处理配对请求
    uint32_t onPassKeyRequest() override
    {
        SerialUSB.print("配对码请求，当前配对码: ");
        SerialUSB.println(dynamicPasskey);
        return dynamicPasskey;
    }
    void onPassKeyNotify(uint32_t pass_key) override
    {
        SerialUSB.print("配对码通知: ");
        SerialUSB.println(pass_key);
    }
    bool onConfirmPIN(uint32_t pass_key) override
    {
        SerialUSB.print("确认配对码: ");
        SerialUSB.println(pass_key);
        return true; // 自动确认
    }
    bool onSecurityRequest() override
    {
        return true;
    }
    // 处理配对完成事件
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override
    {
        if (cmpl.success)
        {
            SerialUSB.println("配对成功");
            ble_link_encrypted = true;

            // 保存配对设备地址
            BLEAddress remoteAddress(cmpl.bd_addr);
            savePairedDeviceInfo(remoteAddress);
            if (pPairedDevice != nullptr)
            {
                delete pPairedDevice;
            }
            pPairedDevice = new BLEAddress(remoteAddress);
            SerialUSB.print("已保存配对设备地址: ");
            SerialUSB.println(remoteAddress.toString().c_str());
            // 配对成功过后，连接完成后设置连接状态
            if (pServer != nullptr)
            {
                deviceConnected = true; // 设置连接状态为连接
            }
        }
        else
        {
            SerialUSB.println("配对失败");
            ble_link_encrypted = false;
            // 配对失败时断开连接
            if (pServer != nullptr)
            {
                pServer->disconnect(0);
            }
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks
{ // 处理连接事件
    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override
    {

        BLEAddress remoteAddress(param->connect.remote_bda);
        SerialUSB.print("设备尝试连接，地址: ");
        SerialUSB.println(remoteAddress.toString().c_str());

        // 如果不是已配对设备，主动发起配对
        if (pPairedDevice == nullptr || !pPairedDevice->equals(remoteAddress))
        {
            SerialUSB.println("未配对设备，正在发起配对...");
            oled.clear();                                           // 清除屏幕
            oled.setFont(ArialMT_Plain_24);                       // 设置字体
            oled.drawString(0, 32, "PIN:" + String(dynamicPasskey)); // 显示配对码
            oled.display();

            // 主动发起配对请求
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);

            // 此时不立即设置连接状态，等待配对完成
            deviceConnected = false;
        }
        else
        {
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
        std::string rxValue = pCharacteristic->getValue(); // 获取接收到的值
        // 获取对应特征值的指针
        BLECharacteristic *pModeChar = pServer->getServiceByUUID(SERVICE_UUID)->getCharacteristic(MODE_CHAR_UUID);
        BLECharacteristic *pTempChar = pServer->getServiceByUUID(SERVICE_UUID)->getCharacteristic(TEMP_CHAR_UUID);
        BLECharacteristic *pSpeedChar = pServer->getServiceByUUID(SERVICE_UUID)->getCharacteristic(SPEED_CHAR_UUID);
        BLECharacteristic *pDirectChar = pServer->getServiceByUUID(SERVICE_UUID)->getCharacteristic(DIRECTION_CHAR_UUID);

        //获取接收到的信息
        if (uuid.equals(BLEUUID(TEMP_CHAR_UUID)))
        {
            temperature = *(pCharacteristic->getValue().c_str());
            saveParams();
            pCharacteristic->setValue(rxValue);
            pCharacteristic->notify();
            // 通过温度特征值回传
            pTempChar->setValue(rxValue);
            pTempChar->notify();
        }
        else if (uuid.equals(BLEUUID(MODE_CHAR_UUID)))
        {
            mode = *(pCharacteristic->getValue().c_str());
            SerialUSB.print("当前模式: ");
            SerialUSB.println(mode);
            saveParams();
            // 通过模式特征值回传
            pModeChar->setValue(rxValue);
            pModeChar->notify();
        }
        else if (uuid.equals(BLEUUID(DIRECTION_CHAR_UUID)))
        {
            direct = *(pCharacteristic->getValue().c_str());
            saveParams();
            // 通过风向特征值回传
            pDirectChar->setValue(rxValue);
            pDirectChar->notify();
        }
        else if (uuid.equals(BLEUUID(SPEED_CHAR_UUID)))
        {
            speed = *(pCharacteristic->getValue().c_str());
            saveParams();
            // 通过风速特征值回传
            pSpeedChar->setValue(rxValue);
            pSpeedChar->notify();
        }
        // }

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
    loadParams();           // 启动时加载参数
    loadPairedDeviceInfo(); // 加载配对设备信息
    SerialUSB.begin(9600);
    opennn = false; // 默认设备状态为关闭

    // 根据设备编号生成设备名称
    String deviceName = "AIR-" + String(DEVICE_NUMBER);
    String deviceId = "AIR-" + String(DEVICE_NUMBER, DEC).substring(0, 3);

    // 生成动态6位配对码
    dynamicPasskey = 100000 + (esp_random() % 900000); // 生成100到999之间的随机数

    oled.init();
    oled.flipScreenVertically();                            // 设置屏幕翻转
    oled.setContrast(255);                                  // 设置屏幕亮度
    drawRect();                                             // 测试屏幕显示
    oled.clear();                                            // 清除屏幕
    oled.display();                                           // 显示内容

    // 创建一个 BLE 设备
    // 设置配对码和安全参数
    BLEDevice::init(deviceName.c_str());
    SerialUSB.print("设备MAC地址: ");
    SerialUSB.println(BLEDevice::getAddress().toString().c_str());
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND); // 推荐使用带配对和绑定的模式
    pSecurity->setCapability(ESP_IO_CAP_OUT);                       // 输出配对码
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setKeySize(6);                // 配对码长度
    pSecurity->setStaticPIN(dynamicPasskey); // 设置配对码
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks()); // 加密安全回调

    // 创建一个 BLE 服务
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks()); // 设置服务回调

    // 添加设备标识服务
    BLEService *pIdService = pServer->createService(DEVICE_ID_SERVICE_UUID);
    BLECharacteristic *pIdChar = pIdService->createCharacteristic(
        BLEUUID((uint16_t)0xFFF9),
        BLECharacteristic::PROPERTY_READ);
    pIdChar->setValue(deviceId.c_str());
    pIdService->start(); // 启动设备标识服务

    // 设置广播数据
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    BLEAdvertisementData advertisementData;                                 // 广播数据
    advertisementData.setCompleteServices(BLEUUID(DEVICE_ID_SERVICE_UUID)); // 设置广播服务UUID
    advertisementData.setName(deviceName.c_str());                          // 使用新的设备名称
    pAdvertising->setAdvertisementData(advertisementData);                  // 设置广播数据

    // 在扫描响应中放入主服务 UUID
    BLEAdvertisementData scanResponse;
    scanResponse.setCompleteServices(BLEUUID(SERVICE_UUID));
    pAdvertising->setScanResponseData(scanResponse);

    BLEService *pService = pServer->createService(SERVICE_UUID); // 创建主服务

    // 创建空调参数 BLE 特征
    auto modeChar = pService->createCharacteristic(MODE_CHAR_UUID,
                                                   BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    auto tempChar = pService->createCharacteristic(TEMP_CHAR_UUID,
                                                   BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    auto speedChar = pService->createCharacteristic(SPEED_CHAR_UUID,
                                                    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    auto directionChar = pService->createCharacteristic(DIRECTION_CHAR_UUID,
                                                        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    // 设置特征回调
    static MyCallbacks charCallback;
    modeChar->setCallbacks(&charCallback);
    tempChar->setCallbacks(&charCallback);
    speedChar->setCallbacks(&charCallback);
    directionChar->setCallbacks(&charCallback);

    modeChar->setValue(&mode,1); // 设置初始模式值
    tempChar->setValue(&temperature,1);
    speedChar->setValue(&speed,1);
    directionChar->setValue(&direct, 1);

    pService->start();                  // 开始服务
    pServer->getAdvertising()->start(); // 开始广播
    SerialUSB.println(" 等待一个客户端连接，且发送通知... ");
}

void loop()
{
    displayMode(mode);        // 更新当前模式
    displayspeed(speed);      // 更新当前风速
    displaydirection(direct); // 更新当前风向
    // deviceConnected 已连接
    if (deviceConnected)
    {
        if (mode != 0)
        {
            // 只在参数变化时刷新OLED
            if ((oldMode != current_mode) || (oldtemperature != temperature) || (oldspeed != current_speed) || (olddirect != current_direct))
            {
                oled.clear();
                //  oled.display();
                oled.setFont(ArialMT_Plain_16);                        // 设置字体
                oled.drawString(0, 0, "Mode:" + String(current_mode)); // 显示当前模式
                oled.drawString(0, 15, "Temperature:" + String(temperature));
                oled.drawString(0, 30, "Speed:" + String(current_speed));
                oled.drawString(0, 45, "Direction:" + String(current_direct));
                oled.display();
                oldMode = current_mode;
                oldtemperature = temperature;
                oldspeed = current_speed;
                olddirect = current_direct;
            }
        }
        else
        {
            oled.clear();
            oled.display();
        }
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