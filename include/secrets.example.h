// Change this file name to secrets.h

#include <pgmspace.h>
 
#define SECRET
#define THINGNAME "YourThingName"

const char WIFI_SSID[] = "YourSSID";
const char WIFI_PASSWORD[] = "YourPassword";
const char AWS_IOT_ENDPOINT[] = "YourAWSIoTEndpoint";
 
// Amazon Root CA 1
static const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)EOF";
 
// Device Certificate                                               //change this
static const char AWS_CERT_CRT[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)KEY";
 
// Device Private Key                                               //change this
static const char AWS_CERT_PRIVATE[] PROGMEM = R"KEY(
-----BEGIN RSA PRIVATE KEY-----

-----END RSA PRIVATE KEY----- 
)KEY";