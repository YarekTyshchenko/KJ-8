#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#endif
#include <time.h>
#include <AutoConnect.h>

#include <AceTime.h>
#include <sntp.h>
using namespace ace_time;

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
      "label": "Select TimeZone",
      "option": []
    },
    {
      "type": "ACElement",
      "value": "<script type='text/javascript'>
window.onload=function(){
var xhr=new XMLHttpRequest();
xhr.onreadystatechange=function(){
if(this.readyState==4&&xhr.status==200){
var currentZone=xhr.getResponseHeader('x-current-zone-id');
var e=document.getElementById('timezone');
for(var i of this.responseText.trim().split('\\n').sort()){
var [name, id]=i.trim().split('|');
e.appendChild(new Option(name, id));
}
for(var v = 0;v<e.options.length;v++){
var o=e.options[v];
if(o.value==currentZone) o.selected=true;
}
}
}
xhr.open('GET', '/stream_timezones');
xhr.responseType='text';
xhr.send();
}
</script>"
    },
    {
      "name": "start",
      "type": "ACSubmit",
      "value": "Set TimeZone",
      "uri": "/set_timezone"
    }
  ]
}
)";

#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer Server;
#elif defined(ARDUINO_ARCH_ESP32)
WebServer Server;
#endif

AutoConnect       Portal(Server);
AutoConnectConfig Config;
AutoConnectAux    Timezone;

uint32_t zoneId = zonedb::kZoneEtc_UTC.zoneId;
static BasicZoneManager<1> zoneManager(zonedb::kZoneRegistrySize, zonedb::kZoneRegistry);

clock::NtpClock ntpClock("pool.ntp.org");
clock::SystemClockLoop systemClock(&ntpClock, nullptr, 60);

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
    "<h2 align=\"center\" style=\"color:blue;margin:20px;\">Last Sync time:</h2>"
    "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
    "<p style=\"text-align:center;\">Reload the page to update the time.</p>"
    "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
    "</body>"
    "</html>";
  
  TimeZone zone = zoneManager.createForZoneId(zoneId);
  
  acetime_t lastSyncTime = systemClock.getLastSyncTime();
  auto zdt = ZonedDateTime::forEpochSeconds(lastSyncTime, zone);
  if (zdt.isError()) {
    Server.send(500, "text/html", F("Error creating ZonedDateTime"));
    return;
  }
  ace_common::PrintStr<64> dateTime;
  zdt.printTo(dateTime);
  
  content.replace("{{DateTime}}", dateTime.getCstr());
  
  Server.send(200, "text/html", content);
}

acetime_t now;
void showClock() {
  acetime_t newNow = systemClock.getNow();
  if (now == newNow) {
    return;
  }
  now = newNow;
  Serial.println(now);
  Serial.print("Last synced time: ");
  Serial.println(systemClock.getLastSyncTime());

  TimeZone zone = zoneManager.createForZoneId(zoneId);
  auto zdt = ZonedDateTime::forEpochSeconds(now, zone);
  if (zdt.isError()) {
    Serial.println(F("Error creating ZonedDateTime"));
    return;
  }
  ace_common::PrintStr<64> dateTime;
  zdt.printTo(dateTime);

  Serial.println(dateTime.getCstr());
}

void setTimezonePage() {
  // Retrieve the value of AutoConnectElement with arg function of WebServer class.
  // Values are accessible with the element name.
  String tz = Server.arg("timezone");
  tz.trim();
  uint32_t selectedZoneId = tz.toInt();
  if (0) {
    Server.send(500, "text/html", F("Invalid Zone ID"));
    return;
  }
  // Check zone is valid
  //auto index = zoneManager.indexForZoneId(selectedZoneId);
  //if (index == ZoneManager::kInvalidIndex) {
  //  Server.send(500, "text/html", F("Zone ID not found in registrar"));
  //  return;
  //}
  auto selectedZone = zoneManager.createForZoneId(selectedZoneId);
  if (selectedZone.isError()) {
    Server.send(500, "text/html", F("Zone ID not found in registrar"));
    return;
  }
  // Save it in memory
  zoneId = selectedZone.getZoneId();
  
  // Redirect to Root
  Server.sendHeader("Location", String("http://") + Server.client().localIP().toString() + String("/"));
  Server.send(302, "text/plain", "");
  Server.client().flush();
  Server.client().stop();
}

void streamTimezones() {
  // Send current timezone in a header
  Server.sendHeader("x-current-zone-id", String(zoneId));
  
  // TODO: What to do if this returns false?
  Server.chunkedResponseModeStart(200, "text/plain");
  ace_common::PrintStr<128> printer;
  for(uint16_t n = 0; n < zonedb::kZoneRegistrySize; n++) {
    auto a = zonedb::kZoneRegistry[n];
    BasicZone basicZone(a);

    // Format: Zone_Name|<zone_id>\n
    basicZone.printNameTo(printer);
    printer.print('|');
    printer.print(a->zoneId);
    printer.println();

    Server.sendContent(printer.getCstr());
    printer.flush();
  }
  Server.chunkedResponseFinalize();
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();

  ntpClock.setup();
  systemClock.setup();
  // Set clock to 0 to prevent issues with Zones before sync.
  systemClock.setNow(0);

  // Enable saved past credential by autoReconnect option,
  // even once it is disconnected.
  Config.autoReconnect = true;
  Config.ticker = true;
  Portal.config(Config);

  // Load aux. page
  Timezone.load(AUX_TIMEZONE);
  Portal.join(Timezone);        // Register aux. page

  // Behavior a root path of ESP8266WebServer.
  Server.on("/", rootPage);
  // Set NTP server trigger handler
  Server.on("/set_timezone", setTimezonePage);
  Server.on("/stream_timezones", streamTimezones);

  // Establish a connection with an autoReconnect option.
  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }
}

void loop() {
  Portal.handleClient();
  systemClock.loop();

  showClock();
}
