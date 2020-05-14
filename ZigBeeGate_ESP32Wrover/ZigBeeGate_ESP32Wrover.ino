/***********************************************************************************************************************************************************
 *  @OPEN SOURCE PROJECT
 *  @AUTHOR: EPCB TECH
 *  @DATE: 2020
 *  @SIMPLE ZIGBEE ESP32 GATEWAY 
 *  @MODEL: AUTOCONNECT WIFI
 ***********************************************************************************************************************************************************/

//WiFi Lib
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#endif
#include <time.h>
#include <AutoConnect.h>

//ZigBee Sheild Lib
#include <zb_znp.h>
#include <zb_zcl.h>
#define DBG_ZB_FRAME
#define UART2_RX  16
#define UART2_TX  17

//Firebase Lib
#include "FirebaseESP32.h"
zb_znp zigbee_network(&Serial2);

FirebaseData firebaseData;

void rootPage();
void startPage();
void EPCBsign();
void connectFirebaseServer();

static const char AUX_TIMEZONE[] PROGMEM = R"(
{
  "title": "TimeZone",
  "uri": "/timezone",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "Sets the time zone to get the current local time.",
      "style": "font-family:Arial;font-weight:bold;text-align:center;margin-bottom:10px;color:DarkSlateBlue"
    },
    {
      "name": "timezone",
      "type": "ACSelect",
      "label": "Select TZ name",
      "option": [],
      "selected": 10
    },
    {
      "name": "newline",
      "type": "ACElement",
      "value": "<br>"
    },
    {
      "name": "start",
      "type": "ACSubmit",
      "value": "OK",
      "uri": "/start"
    }
  ]
}
)";

typedef struct {
  const char* zone;
  const char* ntpServer;
  int8_t      tzoff;
} Timezone_t;

static const Timezone_t TZ[] = {
  { "Europe/London", "europe.pool.ntp.org", 0 },
  { "Europe/Berlin", "europe.pool.ntp.org", 1 },
  { "Europe/Helsinki", "europe.pool.ntp.org", 2 },
  { "Europe/Moscow", "europe.pool.ntp.org", 3 },
  { "Asia/Dubai", "asia.pool.ntp.org", 4 },
  { "Asia/Karachi", "asia.pool.ntp.org", 5 },
  { "Asia/Dhaka", "asia.pool.ntp.org", 6 },
  { "Asia/Jakarta", "asia.pool.ntp.org", 7 },
  { "Asia/Manila", "asia.pool.ntp.org", 8 },
  { "Asia/Tokyo", "asia.pool.ntp.org", 9 },
  { "Australia/Brisbane", "oceania.pool.ntp.org", 10 },
  { "Pacific/Noumea", "oceania.pool.ntp.org", 11 },
  { "Pacific/Auckland", "oceania.pool.ntp.org", 12 },
  { "Atlantic/Azores", "europe.pool.ntp.org", -1 },
  { "America/Noronha", "south-america.pool.ntp.org", -2 },
  { "America/Araguaina", "south-america.pool.ntp.org", -3 },
  { "America/Blanc-Sablon", "north-america.pool.ntp.org", -4},
  { "America/New_York", "north-america.pool.ntp.org", -5 },
  { "America/Chicago", "north-america.pool.ntp.org", -6 },
  { "America/Denver", "north-america.pool.ntp.org", -7 },
  { "America/Los_Angeles", "north-america.pool.ntp.org", -8 },
  { "America/Anchorage", "north-america.pool.ntp.org", -9 },
  { "Pacific/Honolulu", "north-america.pool.ntp.org", -10 },
  { "Pacific/Samoa", "oceania.pool.ntp.org", -11 }
};

/***
 * HƯỚNG DẪN SỬ DỤNG DEMO
 *
 * Sau khi nạp cho Arduino
 * B1: Mở terminal của arduino, chỉnh baudrate về 115200
 * B2: Trên terminal, gửi 1 để yêu cầu cho phép thiết bị mới tham gia mạng.
 * B3: Nhấn giữ nút paring trên thiết bị/cảm biến cho đến khi đèn của thiết bị/cảm biến sáng lên thì nhả nút.
 * B4: Đợi vài giây đến khi nhận đc thông báo từ terminal, lúc này thông tin thiết bị/cảm biến sẽ show lên terminal
 */

/* Biến xử lý điều khiển switch */
uint8_t control_switch_cmd_seq = 0;
uint16_t control_switch_address = 0;

/* ký tự tạm để xử lý yêu cầu từ terminal */
char serial_cmd;

#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer Server;
#elif defined(ARDUINO_ARCH_ESP32)
WebServer Server;
#endif

AutoConnect       Portal(Server);
AutoConnectConfig Config;       // Enable autoReconnect supported on v0.9.4
AutoConnectAux    Timezone;


void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  Serial2.setTimeout(100);

  // Enable saved past credential by autoReconnect option,
  // even once it is disconnected.
  Config.autoReconnect = true;
  Config.hostName = "EPCB TECH";
  
  Portal.config(Config);

  // Load aux. page
  Timezone.load(AUX_TIMEZONE);
  // Retrieve the select element that holds the time zone code and
  // register the zone mnemonic in advance.
  AutoConnectSelect&  tz = Timezone["timezone"].as<AutoConnectSelect>();
  for (uint8_t n = 0; n < sizeof(TZ) / sizeof(Timezone_t); n++) {
    tz.add(String(TZ[n].zone));
  }

  Portal.join({ Timezone });        // Register aux. page

  // Behavior a root path of ESP8266WebServer.
  Server.on("/", rootPage);
  Server.on("/start", startPage);   // Set NTP server trigger handler

  Serial.println("Creating portal and trying to connect...");
  // Establish a connection with an autoReconnect option.
  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
    Serial.println(WiFi.getHostname());
  }
  
  EPCBSign();
  connectFirebaseServer();
  
  /* Khởi động coodinatior */
  Serial.println("\nstart_coordinator(0)");
  if (zigbee_network.start_coordinator(0) == 0) {
    Serial.println("OK");
  }
  else {
    Serial.println("NG");
  }
}

void loop() {
  Portal.handleClient();

/* hàm update() phải được gọi trong vòng lặp để xử lý các gói tin nhận được từ ZigBee Shield */
  zigbee_network.update();

  /* Kiểm tra / thực hiện các lệnh từ terminal */
  if (Serial.available()) {
    serial_cmd = Serial.read();

    switch(serial_cmd) {
      /* Cấu hình lại coodinator, đưa các cáu hình về mặc định.
       * Chú ý: list thiết bị đã tham gia vào mạng trước đó sẽ bị mất */
    case '0': {
      Serial.println("\nstart_coordinator(1)");
      if (zigbee_network.start_coordinator(1) == 0) {
        Serial.println("OK");
      }
      else {
        Serial.println("NG");
      }
    }
      break;

      /* Cho phép thiết bị tham gia vào mạng */
    case '1': {
      Serial.println("set_permit_joining_req");
      /* ALL_ROUTER_AND_COORDINATOR -> cho phép thiết bị tham gia mạng từ Coodinator (ZigBee Shield)
       * hoặc qua router (router thường là các thiết bị đc cấp điện, như ổ cắm, công tắc, bóng đèn ...
       * 60, sau 60s nếu không có thiết bị tham gia mạng, coodinator sẽ trở về mode hoạt động bình thường
       * người dùng muốn thêm thiết bị mới phải yêu cấu thêm lần nữa
       * 1 , đợi thiết bị join thành công, mới thoát khỏi hàm, nếu 0, sự kiện có thiết bị mới tham gia mạng
       * sẽ được nhận ở hàm callback int zb_znp::zigbee_message_handler(zigbee_msg_t& zigbee_msg)
       */
      zigbee_network.set_permit_joining_req(ALL_ROUTER_AND_COORDINATOR, 60, 1);
    }
      break;

      /* yêu cầu Toggle công tắc */
    case '3': {
      Serial.println("TOOGLE Switch Req !\n");
      /*
       * Frame Control, Transaction Sequence Number, Value control
       * Value control -> 0x00: off, 0x01: on, 0x02: toogle
      */
      if (control_switch_address) {
        uint8_t st_buffer[3] = { /* Frame control */ 0x01,
                     /* Transaction Sequence Number */0x00,  /* control_switch_cmd_seq++ */
                     /* Value Control */ 0x02}; /* Value Control [ 0x00:OFF , 0x01:ON , 0x02:TOOGLE ] */
        st_buffer[1] = control_switch_cmd_seq++;

        af_data_request_t st_af_data_request;
        st_af_data_request.cluster_id    = ZCL_CLUSTER_ID_PI_GENERIC_TUNNEL;
        st_af_data_request.dst_address   = control_switch_address;
        st_af_data_request.dst_endpoint  = 0x01;
        st_af_data_request.src_endpoint  = 0x01;
        st_af_data_request.trans_id      = 0x00;
        st_af_data_request.options       = 0x10;
        st_af_data_request.radius        = 0x0F;
        st_af_data_request.len           = sizeof(st_buffer);
        st_af_data_request.data          = st_buffer;

        zigbee_network.send_af_data_req(st_af_data_request);
      }
      else {
        Serial.println("Please join Switch !\n");
      }
    }
      break;

      /******************************************************************
       *  Ví dụ:
       * gửi data từ Gateway(coodinator) đến các thiết bị / cảm biến
       * các thông số cần thiết cho quá trình này bao gồm
       * 1. short address, là địa chỉ đc coodinator cấp khi thiết bị / cảm biến join vào mạng
       * 2. độ dài của mảng data cần truyền
       * 3. data

    case 's': {
      uint8_t st_buffer[10];
      af_data_request_t st_af_data_request;
      st_af_data_request.cluster_id    = ZCL_CLUSTER_ID_PI_GENERIC_TUNNEL;
      st_af_data_request.dst_address   = [ Địa chỉ đích của thiết bị / sensor ] ví du: control_switch_address
      st_af_data_request.dst_endpoint  = 0x01;
      st_af_data_request.src_endpoint  = 0x01;
      st_af_data_request.trans_id      = 0x00;
      st_af_data_request.options       = 0x10;
      st_af_data_request.radius        = 0x0F;
      st_af_data_request.len           = [ Độ dài data cần gửi đi ] ví dụ: sizeof(st_buffer)
      st_af_data_request.data          = [ data ] ví dụ: st_buffer
      zigbee_network.send_af_data_req(st_af_data_request);
    }
      break;
      ********************************************************************/

    default:
      break;
    }
  }
}


void connectFirebaseServer(){
  Firebase.begin("https://vvcisensorhost.firebaseio.com/", "BOxodhdlCVfhZeIX7gaUP3knd197yoYm8BkKqtVf");
  Serial.println("Firebase Connected!");
}

int zb_znp::zigbee_message_handler(zigbee_msg_t& zigbee_msg) {
  /* zigbee start debug message */
  Serial.print("[ZB msg] len: ");
  Serial.print(zigbee_msg.len);
  Serial.print(" cmd0: ");
  Serial.print(zigbee_msg.cmd0, HEX);
  Serial.print(" cmd1: ");
  Serial.print(zigbee_msg.cmd1, HEX);
  Serial.print(" data: ");
  for (int i = 0; i < zigbee_msg.len; i++) {
    Serial.print(zigbee_msg.data[i], HEX);
    Serial.print(" ");
  }
  Serial.println("");
  /* zigbee stop debug message */

  uint16_t zigbee_cmd = BUILD_UINT16(zigbee_msg.cmd1, zigbee_msg.cmd0);

  switch(zigbee_cmd) {
  case ZDO_MGMT_LEAVE_REQ: {
    Serial.println("ZDO_MGMT_LEAVE_REQ");
  }
    break;

  case ZB_RECEIVE_DATA_INDICATION: {
    Serial.println("ZB_RECEIVE_DATA_INDICATION");
  }
    break;

  case ZDO_MGMT_PERMIT_JOIN_RSP: {
    Serial.println("ZDO_MGMT_PERMIT_JOIN_RSP");
    //ZdoMgmtPermitJoinRspInd_t* ZdoMgmtPermitJoinRspInd = (ZdoMgmtPermitJoinRspInd_t*)zigbee_msg.data;
    //Serial.print("\tsrcaddr: ");
    //Serial.println(ZdoMgmtPermitJoinRspInd->srcaddr);
    //Serial.print("\tstatus: ");
    //Serial.println(ZdoMgmtPermitJoinRspInd->status);
  }
    break;

  case ZDO_TC_DEV_IND: {
    Serial.println("ZDO_TC_DEV_IND");
  }
    break;

  case AF_DATA_REQUEST_IND: {
    Serial.println("AF_DATA_REQUEST_IND");
    //uint8_t* status = (uint8_t*)zigbee_msg.data;
    //Serial.print("\tstatus: ");
    //Serial.println(*status);
  }
    break;

  case AF_DATA_CONFIRM: {
    Serial.println("AF_DATA_CONFIRM");
    afDataConfirm_t* afDataConfirm = (afDataConfirm_t*)zigbee_msg.data;
    Serial.print("\tstatus: ");
    Serial.println(afDataConfirm->status);
    Serial.print("\tendpoint: ");
    Serial.println(afDataConfirm->endpoint);
    Serial.print("\ttransID: ");
    Serial.println(afDataConfirm->transID);
  }
    break;

  case AF_INCOMING_MSG: {
    afIncomingMSGPacket_t* st_af_incoming_msg = (afIncomingMSGPacket_t*)zigbee_msg.data;
    Serial.println("AF_INCOMING_MSG");

#if defined (DBG_ZB_FRAME)
    Serial.print("group_id: "); Serial.println(st_af_incoming_msg->group_id, HEX);
    Serial.print("cluster_id: "); Serial.println(st_af_incoming_msg->cluster_id, HEX);
    Serial.print("src_addr: "); Serial.println(st_af_incoming_msg->src_addr, HEX);
    Serial.print("src_endpoint: "); Serial.println(st_af_incoming_msg->src_endpoint, HEX);
    Serial.print("dst_endpoint: "); Serial.println(st_af_incoming_msg->dst_endpoint, HEX);
    Serial.print("was_broadcast: "); Serial.println(st_af_incoming_msg->was_broadcast, HEX);
    Serial.print("link_quality: "); Serial.println(st_af_incoming_msg->link_quality, HEX);
    Serial.print("security_use: "); Serial.println(st_af_incoming_msg->security_use, HEX);
    Serial.print("time_stamp: "); Serial.println(st_af_incoming_msg->time_stamp, HEX);
    Serial.print("trans_seq_num: "); Serial.println(st_af_incoming_msg->trans_seq_num, HEX);
    Serial.print("len: "); Serial.println(st_af_incoming_msg->len);
    Serial.print("data: ");
    for (int i = 0 ; i < st_af_incoming_msg->len ; i++) {
      Serial.print(st_af_incoming_msg->payload[i], HEX);
      Serial.print(" ");
    }
    Serial.println("");
#endif

    switch (st_af_incoming_msg->cluster_id) {
    case ZCL_CLUSTER_ID_MS_RELATIVE_HUMIDITY: {
      Serial.println("ZCL_CLUSTER_ID_MS_RELATIVE_HUMIDITY");
      uint16_t retHum = (uint16_t) ((st_af_incoming_msg->payload[st_af_incoming_msg->len - 1] * 256) + \
          st_af_incoming_msg->payload[st_af_incoming_msg->len - 2]);

      // Ví dụ: retHum = 6789, thì giá trị trả về là 67,89 %
      Serial.print(retHum / 100); // Lấy Trước dấu phẩy -> 67
      Serial.print(",");
      Serial.println(retHum % 100); // Lấy sau dấu phẩy -> 89
    }
      break;

    case ZCL_CLUSTER_ID_MS_TEMPERATURE_MEASUREMENT: {
      Serial.println("ZCL_CLUSTER_ID_MS_TEMPERATURE_MEASUREMENT");
      uint16_t retTemp = (uint16_t) ((st_af_incoming_msg->payload[st_af_incoming_msg->len - 1] * 256) + \
          st_af_incoming_msg->payload[st_af_incoming_msg->len - 2]);

      // Ví dụ: retTemp = 2723, thì giá trị trả về là 27,23 *C
      Serial.print(retTemp/100); // Lấy Trước dấu phẩy -> 27
      Serial.print(",");
      Serial.println(retTemp%100); // Lấy sau dấu phẩy -> 23
    }
      break;

    case ZCL_CLUSTER_ID_MS_OCCUPANCY_SENSING: {
      Serial.println("ZCL_CLUSTER_ID_MS_OCCUPANCY_SENSING");
      uint8_t retOccu = st_af_incoming_msg->payload[st_af_incoming_msg->len - 1];
      Serial.println(retOccu);
    }
      break;

    case ZCL_CLUSTER_ID_GEN_ON_OFF:
      Serial.println("ZCL_CLUSTER_ID_GEN_ON_OFF");
      uint8_t retGenOnOff;
      if (st_af_incoming_msg->len > 9) {
        control_switch_address = st_af_incoming_msg->src_addr;
        retGenOnOff = st_af_incoming_msg->payload[st_af_incoming_msg->len - 8];
        Firebase.setInt(firebaseData, "/Xiaomi Window & Door Sensor Status", retGenOnOff);
        Serial.println(retGenOnOff);
      }
      else {
        retGenOnOff = st_af_incoming_msg->payload[st_af_incoming_msg->len - 1];
        Firebase.setInt(firebaseData, "/Xiaomi Window & Door Sensor Status", retGenOnOff);
        Serial.println(retGenOnOff);
      }
      break;

    default:
      break;
    }
  }
    break;

  case ZDO_MGMT_LEAVE_RSP: {
    Serial.println("ZDO_MGMT_LEAVE_RSP");
  }
    break;

  case ZDO_END_DEVICE_ANNCE_IND: {
    Serial.println("ZDO_END_DEVICE_ANNCE_IND");
    ZDO_DeviceAnnce_t* ZDO_DeviceAnnce = (ZDO_DeviceAnnce_t*)zigbee_msg.data;
    Serial.print("\tSrcAddr: ");
    Serial.println(ZDO_DeviceAnnce->SrcAddr, HEX);
    Serial.print("\tnwkAddr: ");
    Serial.println(ZDO_DeviceAnnce->nwkAddr, HEX);
    Serial.print("\textAddr: ");
    for (int i = 0 ; i < Z_EXTADDR_LEN ; i++) {
      Serial.print(ZDO_DeviceAnnce->extAddr[i], HEX);
    }
    Serial.print("\n");
    /***
     * Specifies the MAC capabilities of the device.
     * Bit: 0 – Alternate PAN Coordinator
     * 1 – Device type: 1- ZigBee Router; 0 – End Device
     * 2 – Power Source: 1 Main powered
     * 3 – Receiver on when idle
     * 4 – Reserved
     * 5 – Reserved
     * 6 – Security capability
     * 7 – Reserved
     */
    Serial.print("\tcapabilities: ");
    Serial.println(ZDO_DeviceAnnce->capabilities);
  }
    break;
  }
}

void rootPage() {
  String  content =
    "<html>"
    "<head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<script type=\"text/javascript\">"
    "setTimeout(\"location.reload()\", 1000);"
    "</script>"
    "</head>"
    "<body>"
    "<h2 align=\"center\" style=\"color:blue;margin:20px;\">Hello, world</h2>"
    "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
    "<p style=\"text-align:center;\">Reload the page to update the time.</p>"
    "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
    "</body>"
    "</html>";
  static const char *wd[7] = { "Sun","Mon","Tue","Wed","Thr","Fri","Sat" };
  struct tm *tm;
  time_t  t;
  char    dateTime[26];

  t = time(NULL);
  tm = localtime(&t);
  sprintf(dateTime, "%04d/%02d/%02d(%s) %02d:%02d:%02d.",
    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
    wd[tm->tm_wday],
    tm->tm_hour, tm->tm_min, tm->tm_sec);
  content.replace("{{DateTime}}", String(dateTime));
  Server.send(200, "text/html", content);
}

void startPage() {
  // Retrieve the value of AutoConnectElement with arg function of WebServer class.
  // Values are accessible with the element name.
  String  tz = Server.arg("timezone");

  for (uint8_t n = 0; n < sizeof(TZ) / sizeof(Timezone_t); n++) {
    String  tzName = String(TZ[n].zone);
    if (tz.equalsIgnoreCase(tzName)) {
      configTime(TZ[n].tzoff * 3600, 0, TZ[n].ntpServer);
      Serial.println("Time zone: " + tz);
      Serial.println("ntp server: " + String(TZ[n].ntpServer));
      break;
    }
  }

  // The /start page just constitutes timezone,
  // it redirects to the root page without the content response.
  Server.sendHeader("Location", String("http://") + Server.client().localIP().toString() + String("/"));
  Server.send(302, "text/plain", "");
  Server.client().flush();
  Server.client().stop();
}


void EPCBSign(){
  Serial.printf("==========================\n");
  Serial.printf("|| EPCBTechEPCBTechEPCB ||\n");
  Serial.printf("|| P                  T ||\n");
  Serial.printf("|| C      .*.         e ||\n");
  Serial.printf("|| B      * *  .*.    c ||\n");
  Serial.printf("|| T***.  * *  * `****h ||\n");
  Serial.printf("|| e   `**' *  *      E ||\n");
  Serial.printf("|| c        `**'      P ||\n");
  Serial.printf("|| h                  C ||\n");
  Serial.printf("|| EPCBTechEPCBTechEPCB ||\n");
  Serial.printf("==========================\n");
}
