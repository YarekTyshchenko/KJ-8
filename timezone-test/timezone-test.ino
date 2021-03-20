/*
  Simple.ino, Example for the AutoConnect library.
  Copyright (c) 2018, Hieromon Ikasamo
  https://github.com/Hieromon/AutoConnect

  This software is released under the MIT License.
  https://opensource.org/licenses/MIT
*/

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
      "name": "js",
      "type": "ACElement",
      "value": "<script type='text/javascript'>
var xhr = new XMLHttpRequest();
xhr.onreadystatechange=function(){
if(this.readyState==4&&xhr.status==200){
var e = document.getElementById('timezone');
for(i of this.responseText.split('\\n').sort()){
e.appendChild(new Option(i, i));
}
}
}
xhr.open('GET', '/timezonestream');
xhr.responseType='text';
xhr.send();
</script>"
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

#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer Server;
#elif defined(ARDUINO_ARCH_ESP32)
WebServer Server;
#endif

AutoConnect       Portal(Server);
AutoConnectConfig Config;       // Enable autoReconnect supported on v0.9.4
AutoConnectAux    Timezone;
const basic::ZoneInfo *zoneInfo = &zonedb::kZoneEtc_UTC;
BasicZoneProcessor zoneProcessor;

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
  
  TimeZone zone = TimeZone::forZoneInfo(zoneInfo, &zoneProcessor);
  auto zdt = ZonedDateTime::forUnixSeconds(time(NULL), zone);
  // Gah, it still fails sometimes
  if (zdt.isError()) {
    Server.send(500, "text/html", F("Error creating ZonedDateTime"));
    return;
  }
  ace_common::PrintStr<64> dateTime;
  zdt.printTo(dateTime);
  
  content.replace("{{DateTime}}", dateTime.getCstr());
  
  Server.send(200, "text/html", content);
}
basic::ZoneRegistrar zoneRegistrar(zonedb::kZoneRegistrySize, zonedb::kZoneRegistry);
void startPage() {
  // Retrieve the value of AutoConnectElement with arg function of WebServer class.
  // Values are accessible with the element name.
  String tz = Server.arg("timezone");
  tz.trim();
  
  // Get Zone by name
  //BasicZoneManager<1> zoneManager(zonedb::kZoneRegistrySize, zonedb::kZoneRegistry);
  //zone = zoneManager.createForZoneName(tz.c_str());
  zoneInfo = zoneRegistrar.getZoneInfoForName(tz.c_str());
  // TODO: Check for null pointer

  configTime("UTC", "pool.ntp.org");

  // The /start page just constitutes timezone,
  // it redirects to the root page without the content response.
  Server.sendHeader("Location", String("http://") + Server.client().localIP().toString() + String("/"));
  Server.send(302, "text/plain", "");
  Server.client().flush();
  Server.client().stop();
}

void streamTimezones() {
  // TODO: What to do if this returns false?
  Server.chunkedResponseModeStart(200, "text/plain");
  for(uint16_t n = 0; n < zonedb::kZoneRegistrySize; n++) {
    auto a = zonedb::kZoneRegistry[n];
    BasicZone basicZone(a);
    ace_common::PrintStr<32> printString;
    basicZone.printNameTo(printString);
    printString.println();
    auto s = printString.getCstr();
    Server.sendContent(s);
  }
  Server.chunkedResponseFinalize();
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();

  // Enable saved past credential by autoReconnect option,
  // even once it is disconnected.
  Config.autoReconnect = true;
  Config.ticker = true;
  Portal.config(Config);

  // Load aux. page
  Timezone.load(AUX_TIMEZONE);
  Portal.join({ Timezone });        // Register aux. page

  // Behavior a root path of ESP8266WebServer.
  Server.on("/", rootPage);
  Server.on("/start", startPage);   // Set NTP server trigger handler
  Server.on("/timezonestream", streamTimezones);

  // Establish a connection with an autoReconnect option.
  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }
}

void loop() {
  Portal.handleClient();
}
